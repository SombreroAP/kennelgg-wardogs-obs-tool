#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QByteArray>
#include <QRectF>
#include <vector>

/// The local control channel: a tiny WebSocket server on 127.0.0.1 that a companion app (ClipHound) talks to.
/// Text frames carry JSON {"type": ...}; binary frames carry JPEG crops of the game source with a 16-byte header.
/// Deliberately dependency-free: the RFC 6455 handshake and framing are a few dozen lines with Qt's SHA-1.
class Bridge : public QObject {
	Q_OBJECT
public:
	explicit Bridge(QObject *parent = nullptr);
	bool listen(quint16 port);
	void close();
	bool listening() const { return server_.isListening(); }
	int clients() const { return (int)clients_.size(); }
	quint16 port() const { return server_.serverPort(); }

	/// A client asked for frames of this region (fractions of the game source) at this rate. 0 = nobody.
	double wantedFps() const { return frameFps_; }
	QRectF wantedRoi() const { return roi_; }
	int wantedWidth() const { return roiWidth_; }
	/// Several regions at their own rates: the kill feed ten times a second, the whole frame once
	/// a second. Each has an id the frames carry back. Empty = the single legacy stream above.
	struct Stream {
		int id = 0;
		double fps = 4;
		QRectF roi{0, 0, 1, 1};
		int width = 0;
		qint64 nextMs = 0; // when it is next due
	};
	std::vector<Stream> &streams() { return streams_; }

	void sendJson(const QJsonObject &o);
	void sendFrame(const QByteArray &jpeg, int w, int h, qint64 tsMs);
	/// "KWF2": uint8 stream id, uint16 w, uint16 h, uint64 ts ms, then JPEG.
	void sendFrame2(int id, const QByteArray &jpeg, int w, int h, qint64 tsMs);
	/// Microphone audio for ClipHound: "KWA1" then 16 kHz mono int16 PCM.
	void sendAudio(const QByteArray &pcm);

signals:
	void clientConnected();
	void clientDisconnected();
	void message(const QJsonObject &o); // JSON from the app

private:
	struct Client {
		QTcpSocket *sock = nullptr;
		bool upgraded = false;
		QByteArray buf;
	};
	QTcpServer server_;
	std::vector<Client *> clients_;
	double frameFps_ = 0;
	QRectF roi_{0, 0, 1, 1};
	int roiWidth_ = 0;
	std::vector<Stream> streams_;

	void onNewConnection();
	void onReadyRead(Client *c);
	void onDisconnected(Client *c);
	bool handshake(Client *c);
	void parseFrames(Client *c);
	void sendRaw(Client *c, int opcode, const QByteArray &payload);
	void handle(Client *c, const QJsonObject &o);
	void recomputeWants();
};
