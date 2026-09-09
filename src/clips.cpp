#include "clips.h"
#include "config.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <obs-module.h>
#include <algorithm>
#include <cstring>
#include <obs-frontend-api.h>
#include <plugin-support.h>

Clips::Clips(QObject *parent) : QObject(parent) {}

QString Clips::safe(QString s)
{
	static const QString bad = "\\/:*?\"<>|";
	for (QChar &c : s)
		if (bad.contains(c) || c.unicode() < 32)
			c = '-';
	return s.trimmed().left(80);
}

QString Clips::logFile() const
{
	return QString::fromStdString(Config::configFile("clips.csv"));
}

void Clips::ensureReplayBuffer()
{
	if (obs_frontend_replay_buffer_active())
		return;
	obs_frontend_replay_buffer_start();
	if (obs_frontend_replay_buffer_active())
		emit logged("Replay buffer started (Kennel needs it for clips).");
	else
		emit logged(
			"REPLAY BUFFER IS OFF and could not be started: enable it in OBS Settings → Output → Replay Buffer (60-120 s), then restart OBS. Until then clips only fire your hotkeys.");
}

QList<QPair<QString, QString>> Clips::allHotkeys()
{
	QList<QPair<QString, QString>> out;
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id, obs_hotkey_t *hk) {
			auto *o = (QList<QPair<QString, QString>> *)data;
			const char *n = obs_hotkey_get_name(hk), *d = obs_hotkey_get_description(hk);
			if (n && *n)
				o->append({QString::fromUtf8(n), QString::fromUtf8(d ? d : "")});
			return true;
		},
		&out);
	std::sort(out.begin(), out.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
		return a.second.toLower() < b.second.toLower();
	});
	return out;
}

bool Clips::fireHotkey(const QString &name)
{
	struct Ctx {
		QByteArray name;
		obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	} ctx{name.toUtf8()};
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *hk) {
			auto *c = (Ctx *)data;
			if (strcmp(obs_hotkey_get_name(hk), c->name.constData()) == 0) {
				c->id = id;
				return false;
			}
			return true;
		},
		&ctx);
	if (ctx.id == OBS_INVALID_HOTKEY_ID)
		return false;
	obs_hotkey_trigger_routed_callback(ctx.id, true);
	obs_hotkey_trigger_routed_callback(ctx.id, false);
	return true;
}

QString Clips::request(const QString &title, const QStringList &tags, const QString &source)
{
	QDateTime now = QDateTime::currentDateTime();
	if (lastRequest_.isValid() && lastRequest_.msecsTo(now) < minGapMs)
		return "ignored: too soon after the last clip";
	lastRequest_ = now;
	QStringList missed;
	for (const QString &hk : hotkeys)
		if (!fireHotkey(hk))
			missed << hk;
	if (!missed.isEmpty())
		emit logged("Clip hotkeys not found in OBS (plugin missing?): " + missed.join(", "));
	else if (!hotkeys.isEmpty())
		emit logged(QString("Fired %1 clip hotkey(s) for '%2'.")
				    .arg(hotkeys.size())
				    .arg(title.isEmpty() ? "(untitled)" : title));
	if (!useReplay)
		return hotkeys.isEmpty()
			       ? "no clip method: turn on the replay buffer or pick a hotkey (Settings → Clips)"
			       : "";
	if (!obs_frontend_replay_buffer_active()) {
		if (!autoStartReplay)
			return "the replay buffer is not running (Settings → Output → Replay Buffer)";
		ensureReplayBuffer();
		return "replay buffer was off; started it - this moment is lost, the next one will save";
	}
	pending_.push_back({now, title, tags, source});
	obs_frontend_replay_buffer_save();
	emit logged(QString("Clip requested: %1 [%2]").arg(title.isEmpty() ? "(untitled)" : title, tags.join(", ")));
	return "";
}

void Clips::onReplaySaved()
{
	char *last = obs_frontend_get_last_replay();
	QString path = last ? QString::fromUtf8(last) : QString();
	bfree(last);
	if (path.isEmpty())
		return;
	Pending p;
	if (!pending_.empty()) {
		p = pending_.front();
		pending_.pop_front();
	} else {
		p.when = QDateTime::currentDateTime();
		p.title = "manual";
	}
	QFileInfo fi(path);
	QString name = nameTemplate;
	name.replace("{date}", p.when.toString("yyyy-MM-dd"));
	name.replace("{time}", p.when.toString("HH-mm-ss"));
	name.replace("{title}", safe(p.title));
	name.replace("{tags}", safe(p.tags.join("_")));
	name.replace("{source}", safe(p.source));
	name = safe(name);
	while (name.contains("__"))
		name.replace("__", "_");
	QDir outDir = fi.dir();
	if (!folder.isEmpty()) {
		QDir want(folder);
		if (want.exists() || want.mkpath("."))
			outDir = want;
		else
			emit logged("Clip folder does not exist and could not be created: " + folder);
	}
	QString target = outDir.filePath(name + "." + fi.suffix());
	int n = 2;
	while (QFile::exists(target) && target != path)
		target = outDir.filePath(name + QString("_%1.").arg(n++) + fi.suffix());
	QString finalPath = path;
	if (!name.isEmpty() && QFile::rename(path, target))
		finalPath = target;
	Entry e{p.when, p.title, p.tags, finalPath};
	history_.push_back(e);
	while (history_.size() > 200)
		history_.pop_front();
	QFile f(logFile());
	if (f.open(QIODevice::Append | QIODevice::Text)) {
		QTextStream ts(&f);
		ts << p.when.toString(Qt::ISODate) << "," << safe(p.title) << "," << p.tags.join("|") << ","
		   << finalPath << "\n";
	}
	emit logged("Clip saved: " + QFileInfo(finalPath).fileName());
	emit saved(e);
}
