#include "roster.h"
#include "plugin-support.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

Roster::Roster(QObject *parent) : QObject(parent)
{
	connect(&timer_, &QTimer::timeout, this, &Roster::poll);
}

void Roster::configure(const QString &url, int seconds, const QString &onlyChannel)
{
	onlyChannel_ = onlyChannel.trimmed();
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
		for (const QJsonValue &cv : o.value("channels").toArray()) {
			QJsonObject c = cv.toObject();
			QString chan = c.value("channel").toString();
			if (!onlyChannel_.isEmpty() && chan.compare(onlyChannel_, Qt::CaseInsensitive) != 0)
				continue;
			for (const QJsonValue &mv : c.value("members").toArray()) {
				QJsonObject m = mv.toObject();
				Member e;
				e.name = m.value("name").toString().trimmed();
				if (e.name.isEmpty())
					continue;
				e.streaming = m.value("streaming").toBool();
				e.camera = m.value("camera").toBool();
				e.channel = chan;
				found.append(e);
			}
		}
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
