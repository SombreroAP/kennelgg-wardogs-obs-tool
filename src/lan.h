#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QDateTime>
#include <map>
#include <string>

/// Squad discovery on the LAN: every plugin broadcasts a small JSON beacon on UDP; peers are listed
/// with the NDI source name they publish, so a squad mate on the same network can be added with no typing.
class Lan : public QObject {
	Q_OBJECT
public:
	struct Peer {
		QString name, host, ndi, version;
		QDateTime lastSeen;
	};
	explicit Lan(QObject *parent = nullptr);
	bool start(quint16 port);
	void stop();
	bool running() const { return sock_.state() == QAbstractSocket::BoundState; }
	void setSelf(const QString &name, const QString &host, const QString &ndi, const QString &version);
	const std::map<QString, Peer> &peers() const { return peers_; }
	static QString hostName();

signals:
	void peersChanged();

private:
	QUdpSocket sock_;
	QTimer beacon_;
	quint16 port_ = 0;
	QString name_, host_, ndi_, version_;
	std::map<QString, Peer> peers_; // key: host
	void send();
	void read();
};
