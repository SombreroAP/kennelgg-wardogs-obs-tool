#include "clips.h"
#include "config.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <obs-module.h>
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
	if (!obs_frontend_replay_buffer_active()) {
		obs_frontend_replay_buffer_start();
		emit logged("Replay buffer started (Kennel needs it for clips).");
	}
}

QString Clips::request(const QString &title, const QStringList &tags, const QString &source)
{
	QDateTime now = QDateTime::currentDateTime();
	if (lastRequest_.isValid() && lastRequest_.msecsTo(now) < minGapMs)
		return "ignored: too soon after the last clip";
	lastRequest_ = now;
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
	QString target = fi.dir().filePath(name + "." + fi.suffix());
	int n = 2;
	while (QFile::exists(target) && target != path)
		target = fi.dir().filePath(name + QString("_%1.").arg(n++) + fi.suffix());
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
