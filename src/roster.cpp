#include "roster.h"
#include "plugin-support.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QCryptographicHash>

Roster::Roster(QObject *parent) : QObject(parent)
{
	connect(&timer_, &QTimer::timeout, this, &Roster::poll);
}

void Roster::configure(const QString &url, int seconds, const QString &onlyChannel, const QString &onlyGuild)
{
	onlyChannel_ = onlyChannel.trimmed();
	onlyGuild_ = onlyGuild.trimmed();
	QString u = url.trimmed();
	if (u.isEmpty()) {
		stop();
		return;
	}
	bool fresh = u != url_;
	url_ = u;
	timer_.setInterval(qMax(3, seconds) * 1000);
	if (!timer_.isActive() || fresh) {
		timer_.start();
		poll();
	}
}

void Roster::stop()
{
	timer_.stop();
	url_.clear();
	if (!members_.isEmpty()) {
		members_.clear();
		emit changed();
	}
	status_ = "off";
}

QList<Roster::Member> Roster::streamers() const
{
	QList<Member> out;
	for (const auto &m : members_)
		if (m.streaming)
			out.append(m);
	return out;
}

bool Roster::isMember(const QString &handle) const
{
	QString h = handle.trimmed().toLower();
	if (h.isEmpty())
		return false;
	QString hash =
		QString::fromLatin1(QCryptographicHash::hash(h.toUtf8(), QCryptographicHash::Sha256).toHex()).left(16);
	return memberHashes_.contains(hash);
}

void Roster::poll()
{
	if (url_.isEmpty() || inFlight_)
		return;
	if (!net_)
		net_ = new QNetworkAccessManager(this);
	QNetworkRequest req{QUrl(url_)};
	req.setHeader(QNetworkRequest::UserAgentHeader, QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION));
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	// the roster changes every few seconds; a cached copy is worse than no copy
	req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
	inFlight_ = true;
	QNetworkReply *r = net_->get(req);
	QTimer::singleShot(6000, r, [r]() {
		if (r->isRunning())
			r->abort();
	});
	connect(r, &QNetworkReply::finished, this, [this, r]() {
		r->deleteLater();
		inFlight_ = false;
		if (r->error() != QNetworkReply::NoError) {
			// 404 is the one worth naming: it means the key in the address does not match the
			// one the bot was given, which is a typo rather than a network problem
			int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			status_ = code == 404 ? "not found - check the address (the key must match the bot's)"
					      : "could not read the roster (" + r->errorString() + ")";
			emit polled();
			return;
		}
		QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
		QList<Member> found;
		QStringList guilds;
		invite_ = o.value("invite").toString();
		home_ = o.value("home").toString();
		join_ = o.value("join").toString();
		membersKnown_ = o.contains("members");
		memberHashes_.clear();
		for (const QJsonValue &hv : o.value("members").toArray())
			memberHashes_.insert(hv.toString().toLower());
		for (const QJsonValue &cv : o.value("channels").toArray()) {
			QJsonObject c = cv.toObject();
			QString chan = c.value("channel").toString();
			QString guild = c.value("guild").toString();
			if (!guild.isEmpty() && !guilds.contains(guild))
				guilds << guild;
			if (!onlyGuild_.isEmpty() && guild.compare(onlyGuild_, Qt::CaseInsensitive) != 0)
				continue;
			if (!onlyChannel_.isEmpty() && chan.compare(onlyChannel_, Qt::CaseInsensitive) != 0)
				continue;
			for (const QJsonValue &mv : c.value("members").toArray()) {
				QJsonObject m = mv.toObject();
				Member e;
				e.name = m.value("name").toString().trimmed();
				if (e.name.isEmpty())
					continue;
				e.handle = m.value("username").toString().trimmed();
				e.streaming = m.value("streaming").toBool();
				e.camera = m.value("camera").toBool();
				e.channel = chan;
				e.guild = guild;
				found.append(e);
			}
		}
		for (const QJsonValue &gv : o.value("guilds").toArray())
			if (!guilds.contains(gv.toString()))
				guilds << gv.toString(); // servers the bot is in with nobody in voice right now
		guilds_ = guilds;
		bool same = found.size() == members_.size();
		for (int i = 0; same && i < found.size(); ++i)
			same = found[i].name == members_[i].name && found[i].streaming == members_[i].streaming &&
			       found[i].channel == members_[i].channel;
		members_ = found;
		int live = streamers().size();
		status_ = members_.isEmpty() ? "nobody in voice"
					     : QString("%1 in voice, %2 sharing").arg(members_.size()).arg(live);
		if (same)
			emit polled();
		else
			emit changed();
	});
}
