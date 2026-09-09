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

Clips::Clips(QObject *parent) : QObject(parent)
{
	connect(&watchTimer_, &QTimer::timeout, this, &Clips::pollWatches);
}

QStringList Clips::discoverBacktrackFolders()
{
	QStringList out;
	obs_enum_sources(
		[](void *data, obs_source_t *src) {
			auto *o = (QStringList *)data;
			const char *id = obs_source_get_id(src);
			if (!id || !strstr(id, "backtrack"))
				return true;
			obs_data_t *st = obs_source_get_settings(src);
			for (const char *key : {"path", "directory", "folder", "output_path", "save_path"}) {
				const char *v = obs_data_get_string(st, key);
				if (v && *v && !o->contains(QString::fromUtf8(v)))
					o->append(QString::fromUtf8(v));
			}
			obs_data_release(st);
			return true;
		},
		&out);
	return out;
}

QString Clips::nameFor(const QDateTime &when, const QString &title, const QStringList &tags,
		       const QString &source) const
{
	QString name = nameTemplate;
	name.replace("{date}", when.toString("yyyy-MM-dd"));
	name.replace("{time}", when.toString("HH-mm-ss"));
	name.replace("{title}", safe(title));
	name.replace("{tags}", safe(tags.join("_")));
	name.replace("{source}", safe(source));
	name = safe(name);
	while (name.contains("__"))
		name.replace("__", "_");
	return name;
}

void Clips::pollWatches()
{
	QStringList folders = watchFolders + discoverBacktrackFolders();
	folders.removeDuplicates();
	QDateTime now = QDateTime::currentDateTime();
	for (auto it = watches_.begin(); it != watches_.end();) {
		Watch &w = *it;
		bool done = false;
		for (const QString &folder : folders) {
			QDir d(folder);
			if (!d.exists())
				continue;
			for (const QFileInfo &fi :
			     d.entryInfoList({"*.mkv", "*.mp4", "*.mov", "*.flv", "*.ts"}, QDir::Files, QDir::Time)) {
				if (fi.lastModified() < w.since.addSecs(-2) || w.seen.contains(fi.absoluteFilePath()))
					continue;
				if (fi.lastModified().msecsTo(now) < 2000)
					continue; // still being written
				w.seen.insert(fi.absoluteFilePath());
				QString name = nameFor(w.since, w.title, w.tags, "backtrack");
				QString target = d.filePath(name + "." + fi.suffix());
				int n = 2;
				while (QFile::exists(target))
					target = d.filePath(name + QString("_%1.").arg(n++) + fi.suffix());
				if (QFile::rename(fi.absoluteFilePath(), target)) {
					Entry e{w.since, w.title, w.tags, target};
					history_.push_back(e);
					emit logged("Backtrack clip named: " + QFileInfo(target).fileName());
					emit saved(e);
					done = true;
				}
			}
		}
		if (done || w.since.secsTo(now) > 25)
			it = watches_.erase(it);
		else
			++it;
	}
	if (watches_.empty())
		watchTimer_.stop();
}

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
	if (!hotkeys.isEmpty() && missed.size() < hotkeys.size()) {
		// something else (Backtrack) is writing a file: give it our name when it appears
		Watch w;
		w.since = now;
		w.title = title;
		w.tags = tags;
		QStringList folders = watchFolders + discoverBacktrackFolders();
		for (const QString &folder : folders)
			for (const QFileInfo &fi :
			     QDir(folder).entryInfoList({"*.mkv", "*.mp4", "*.mov", "*.flv", "*.ts"}, QDir::Files))
				w.seen.insert(fi.absoluteFilePath());
		watches_.push_back(w);
		if (!watchTimer_.isActive())
			watchTimer_.start(1000);
	}
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
	QString name = nameFor(p.when, p.title, p.tags, p.source);
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
