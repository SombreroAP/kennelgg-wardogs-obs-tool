#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

/// How much the network between two squad mates actually carries.
///
/// NDI's full-quality stream is barely compressed, so the only question that matters is whether the
/// link carries the bitrate a given size needs. Guessing from "it's gigabit" is how you end up with
/// a juddering feed: a cable at 100 Mbit, a powerline adapter or a Wi-Fi hop all look like gigabit
/// from the desk. Every plugin listens on a port and swallows whatever is sent to it, counting as it
/// goes; the far end sends for a few seconds and is told what actually landed.
class Speed : public QObject {
	Q_OBJECT
public:
	explicit Speed(QObject *parent = nullptr);
	bool listen(quint16 port); // the sink: what squad mates measure against
	void stop();
	bool listening() const { return srv_.isListening(); }
	/// Send flat out for `seconds` and report what the far end received. One at a time.
	void measure(const QString &host, quint16 port, int seconds = 4);
	bool busy() const { return out_ != nullptr; }

	/// What an NDI stream of this size and rate needs, in Mbit/s. NDI high bandwidth runs at about
	/// one bit per pixel: 1440p60 measures ~240, which is what people see on the wire.
	static double needMbps(int w, int h, double fps) { return w * (double)h * fps * 1.1 / 1e6; }

signals:
	/// mbps < 0 means it could not be measured; note says why, or carries the reading's detail.
	void done(double mbps, const QString &note);

private:
	QTcpServer srv_;
	QTcpSocket *out_ = nullptr;
	QTimer sendFor_, idle_;
	QByteArray block_;
	bool sending_ = false;
	void pump();
	void finishOut(double mbps, const QString &note);
};
