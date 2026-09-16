#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTimer>
#include <mutex>
#include <vector>
#include <obs.h>

/// A tap on the streamer's microphone source: OBS hands this every block of audio the source
/// produces (muted or not), it is folded to 16 kHz mono 16-bit, and handed on in ~100 ms pieces
/// for ClipHound to listen to. Nothing is recorded to disk here; the audio goes over the local
/// bridge and no further.
class VoiceTap : public QObject {
	Q_OBJECT
public:
	explicit VoiceTap(QObject *parent = nullptr);
	~VoiceTap();

	/// Start listening to this OBS source (by name). An empty name stops. Returns what happened.
	QString attach(const QString &sourceName);
	void detach();
	bool attached() const { return src_ != nullptr; }
	QString sourceName() const { return name_; }
	/// The first microphone-like source in OBS, for the auto choice.
	static QString pickMic();

signals:
	/// 16 kHz mono int16 little-endian PCM, about 100 ms at a time.
	void pcm(const QByteArray &bytes);

private:
	static void onAudio(void *param, obs_source_t *source, const struct audio_data *data, bool muted);
	void flush();
	QString name_;
	obs_source_t *src_ = nullptr;
	obs_weak_source_t *weak_ = nullptr;
	QTimer timer_;
	std::mutex mu_;
	std::vector<int16_t> pending_; // filled on the audio thread, drained on ours
	double phase_ = 0;             // resampler position between input samples
	uint32_t inRate_ = 48000;
	int inFormat_ = 0;
};
