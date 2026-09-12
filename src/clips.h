#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <deque>
#include <QSet>
#include <QTimer>

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

	QString nameTemplate = "{title}_{tags}_{date}_{time}";
	/// Clips made within this many seconds of the last one are a run of rolling highlights: their
	/// files are named "[1 of 3]", "[2 of 3]", "[3 of 3]", earlier ones renamed as the run grows.
	int seriesWindowS = 45;
	QString folder;              // move clips here when set // {date} {time} {title} {tags} {source}
	bool autoStartReplay = true; // start the replay buffer when OBS loads / when a clip is asked for
	bool useReplay = true;       // save OBS's replay buffer
	QStringList hotkeys;         // OBS hotkeys to fire as well (Aitum Backtrack saves, anything else)
	QStringList
		watchFolders; // folders other tools (Aitum Backtrack) write clips into; new files after a trigger are renamed
	static QStringList discoverBacktrackFolders();
	static QList<QPair<QString, QString>> allHotkeys(); // (name, description)
	static bool fireHotkey(const QString &name);
	int minGapMs = 4000; // ignore clip requests closer than this

	/// Ask OBS to save the replay buffer; the rename happens when OBS reports the file.
	QString request(const QString &title, const QStringList &tags, const QString &source);
	void onReplaySaved(); // wire to OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED
	void ensureReplayBuffer();
	enum class ReplayChange { None, Written, NeedsRestart };
	/// Write the replay buffer length into OBS's profile (both output modes).
	ReplayChange setReplaySeconds(int seconds);
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
	struct Watch {
		QDateTime since;
		QString title;
		QStringList tags;
		QSet<QString> seen;
	};
	std::deque<Watch> watches_;
	QTimer watchTimer_;
	void pollWatches();
	QString nameFor(const QDateTime &when, const QString &title, const QStringList &tags,
			const QString &source) const;
	QString logFile() const;
	static QString safe(QString s);
	/// A clip file just got its name: put it in the current run, or start a new one, and renumber.
	void joinSeries(const QString &path, const QDateTime &when);
	struct Series {
		QDateTime last;
		QStringList paths;
	} series_;
};
