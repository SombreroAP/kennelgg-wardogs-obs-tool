#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <deque>

/// Replay-buffer clipping: save the buffer on demand, rename the file with tags, keep a log.
class Clips : public QObject {
	Q_OBJECT
public:
	struct Entry {
		QDateTime when;
		QString title;
		QStringList tags;
		QString path;
	};
	explicit Clips(QObject *parent = nullptr);

	QString nameTemplate = "{date}_{time}_{tags}"; // {date} {time} {title} {tags} {source}
	bool autoStartReplay = true; // start the replay buffer when OBS loads / when a clip is asked for
	bool useReplay = true;       // save OBS's replay buffer
	QStringList hotkeys;         // OBS hotkeys to fire as well (Aitum Backtrack saves, anything else)
	static QList<QPair<QString, QString>> allHotkeys(); // (name, description)
	static bool fireHotkey(const QString &name);
	int minGapMs = 4000; // ignore clip requests closer than this

	/// Ask OBS to save the replay buffer; the rename happens when OBS reports the file.
	QString request(const QString &title, const QStringList &tags, const QString &source);
	void onReplaySaved(); // wire to OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED
	void ensureReplayBuffer();
	const std::deque<Entry> &history() const { return history_; }
	QString lastPath() const { return history_.empty() ? QString() : history_.back().path; }

signals:
	void saved(const Entry &e);
	void logged(const QString &msg);

private:
	struct Pending {
		QDateTime when;
		QString title;
		QStringList tags;
		QString source;
	};
	std::deque<Pending> pending_;
	std::deque<Entry> history_;
	QDateTime lastRequest_;
	QString logFile() const;
	static QString safe(QString s);
};
