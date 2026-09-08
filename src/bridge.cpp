#include "bridge.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDataStream>
#include <QtEndian>
#include <obs-module.h>
#include <plugin-support.h>

Bridge::Bridge(QObject *parent) : QObject(parent)
{
	connect(&server_, &QTcpServer::newConnection, this, &Bridge::onNewConnection);
}

bool Bridge::listen(quint16 port)
{
	close();
	if (!server_.listen(QHostAddress::LocalHost, port)) {
		obs_log(LOG_WARNING, "bridge: cannot listen on 127.0.0.1:%u (%s)", port,
			server_.errorString().toUtf8().constData());
		return false;
	}
	obs_log(LOG_INFO, "bridge: listening on ws://127.0.0.1:%u", port);
	return true;
}

void Bridge::close()
{
	for (auto *c : clients_) {
		c->sock->disconnect(this);
		c->sock->deleteLater();
		delete c;
	}
	clients_.clear();
	recomputeWants();
	if (server_.isListening())
		server_.close();
}

void Bridge::onNewConnection()
{
	while (QTcpSocket *s = server_.nextPendingConnection()) {
		auto *c = new Client;
		c->sock = s;
		clients_.push_back(c);
		connect(s, &QTcpSocket::readyRead, this, [this, c]() { onReadyRead(c); });
		connect(s, &QTcpSocket::disconnected, this, [this, c]() { onDisconnected(c); });
	}
}

void Bridge::onDisconnected(Client *c)
{
	auto it = std::find(clients_.begin(), clients_.end(), c);
	if (it != clients_.end())
		clients_.erase(it);
	bool was = c->upgraded;
	c->sock->deleteLater();
	delete c;
	recomputeWants();
	if (was)
		emit clientDisconnected();
}

void Bridge::onReadyRead(Client *c)
{
	c->buf += c->sock->readAll();
	if (!c->upgraded) {
		if (!c->buf.contains("\r\n\r\n"))
			return;
		if (!handshake(c)) {
			c->sock->disconnectFromHost();
			return;
		}
	}
	parseFrames(c);
}

bool Bridge::handshake(Client *c)
{
	int end = c->buf.indexOf("\r\n\r\n");
	QByteArray head = c->buf.left(end);
	c->buf.remove(0, end + 4);
	QByteArray key;
	for (auto &line : head.split('\n')) {
		QByteArray l = line.trimmed();
		if (l.toLower().startsWith("sec-websocket-key:"))
			key = l.mid(l.indexOf(':') + 1).trimmed();
	}
	if (key.isEmpty())
		return false;
	QByteArray accept =
		QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1)
			.toBase64();
	QByteArray resp =
		"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
		accept + "\r\n\r\n";
	c->sock->write(resp);
	c->upgraded = true;
	QJsonObject hello;
	hello["type"] = "hello";
	hello["plugin"] = "kennel-wardogs";
	hello["version"] = PLUGIN_VERSION;
	hello["protocol"] = 1;
	sendRaw(c, 1, QJsonDocument(hello).toJson(QJsonDocument::Compact));
	emit clientConnected();
	return true;
}

void Bridge::parseFrames(Client *c)
{
	for (;;) {
		QByteArray &b = c->buf;
		if (b.size() < 2)
			return;
		const uchar *d = (const uchar *)b.constData();
		bool fin = d[0] & 0x80;
		int opcode = d[0] & 0x0f;
		bool masked = d[1] & 0x80;
		quint64 len = d[1] & 0x7f;
		int pos = 2;
		if (len == 126) {
			if (b.size() < 4)
				return;
			len = qFromBigEndian<quint16>(d + 2);
			pos = 4;
		} else if (len == 127) {
			if (b.size() < 10)
				return;
			len = qFromBigEndian<quint64>(d + 2);
			pos = 10;
		}
		uchar mask[4] = {0, 0, 0, 0};
		if (masked) {
			if (b.size() < pos + 4)
				return;
			memcpy(mask, d + pos, 4);
			pos += 4;
		}
		if ((quint64)b.size() < pos + len)
			return;
		QByteArray payload = b.mid(pos, (int)len);
		if (masked)
			for (int i = 0; i < payload.size(); i++)
				payload[i] = payload[i] ^ mask[i & 3];
		b.remove(0, pos + (int)len);
		(void)fin;
		if (opcode == 8) { // close
			sendRaw(c, 8, QByteArray());
			c->sock->disconnectFromHost();
			return;
		}
		if (opcode == 9) { // ping
			sendRaw(c, 10, payload);
			continue;
		}
		if (opcode == 1) {
			QJsonParseError err;
			QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
			if (doc.isObject())
				handle(c, doc.object());
		}
	}
}

void Bridge::sendRaw(Client *c, int opcode, const QByteArray &payload)
{
	if (!c->sock || c->sock->state() != QAbstractSocket::ConnectedState)
		return;
	QByteArray head;
	head.append((char)(0x80 | opcode));
	quint64 len = (quint64)payload.size();
	if (len < 126)
		head.append((char)len);
	else if (len < 65536) {
		head.append((char)126);
		quint16 l = qToBigEndian<quint16>((quint16)len);
		head.append((const char *)&l, 2);
	} else {
		head.append((char)127);
		quint64 l = qToBigEndian<quint64>(len);
		head.append((const char *)&l, 8);
	}
	c->sock->write(head);
	c->sock->write(payload);
}

void Bridge::handle(Client *c, const QJsonObject &o)
{
	QString type = o.value("type").toString();
	if (type == "subscribe") {
		// {"type":"subscribe","frames":true,"fps":4,"roi":[x,y,w,h],"width":0}
		bool frames = o.value("frames").toBool(true);
		double fps = o.value("fps").toDouble(4);
		QJsonArray r = o.value("roi").toArray();
		if (r.size() == 4)
			roi_ = QRectF(r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
		roiWidth_ = o.value("width").toInt(0);
		frameFps_ = frames ? std::clamp(fps, 0.5, 30.0) : 0;
		(void)c;
	}
	emit message(o);
}

void Bridge::recomputeWants()
{
	if (clients_.empty())
		frameFps_ = 0;
}

void Bridge::sendJson(const QJsonObject &o)
{
	QByteArray payload = QJsonDocument(o).toJson(QJsonDocument::Compact);
	for (auto *c : clients_)
		if (c->upgraded)
			sendRaw(c, 1, payload);
}

void Bridge::sendFrame(const QByteArray &jpeg, int w, int h, qint64 tsMs)
{
	// 16-byte header: "KWF1", uint16 w, uint16 h, uint64 timestamp ms (little endian), then JPEG
	QByteArray payload;
	payload.reserve(16 + jpeg.size());
	payload.append("KWF1", 4);
	quint16 ww = qToLittleEndian<quint16>((quint16)w), hh = qToLittleEndian<quint16>((quint16)h);
	quint64 ts = qToLittleEndian<quint64>((quint64)tsMs);
	payload.append((const char *)&ww, 2);
	payload.append((const char *)&hh, 2);
	payload.append((const char *)&ts, 8);
	payload.append(jpeg);
	for (auto *c : clients_)
		if (c->upgraded)
			sendRaw(c, 2, payload);
}
