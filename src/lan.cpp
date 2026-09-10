#include "lan.h"
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <obs-module.h>
#include <plugin-support.h>

Lan::Lan(QObject *parent) : QObject(parent)
{
	connect(&beacon_, &QTimer::timeout, this, &Lan::send);
	connect(&sock_, &QUdpSocket::readyRead, this, &Lan::read);
}

QString Lan::hostName()
{
	return QHostInfo::localHostName();
}

bool Lan::start(quint16 port)
{
	stop();
	port_ = port;
	if (!sock_.bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
		obs_log(LOG_WARNING, "lan: cannot bind UDP %u (%s)", port, sock_.errorString().toUtf8().constData());
		return false;
	}
	beacon_.start(2000);
	send();
	return true;
}

void Lan::stop()
{
	beacon_.stop();
	if (sock_.state() != QAbstractSocket::UnconnectedState)
		sock_.close();
}

void Lan::setSelf(const QString &name, const QString &host, const QString &ndi, const QString &version)
{
	name_ = name;
	host_ = host;
	ndi_ = ndi;
	version_ = version;
}

void Lan::send()
{
	if (!running())
		return;
	QJsonObject o;
	o["kennel"] = 1;
	o["name"] = name_;
	o["host"] = host_;
	o["ndi"] = ndi_;
	o["version"] = version_;
	QByteArray d = QJsonDocument(o).toJson(QJsonDocument::Compact);
	// broadcast on every interface's subnet, plus the limited broadcast, so it works on VPN-less LANs and Tailscale-style links alike
	for (const QNetworkInterface &nic : QNetworkInterface::allInterfaces()) {
		if (!(nic.flags() & QNetworkInterface::IsUp) || (nic.flags() & QNetworkInterface::IsLoopBack))
			continue;
		for (const QNetworkAddressEntry &e : nic.addressEntries())
			if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.broadcast().isNull())
				sock_.writeDatagram(d, e.broadcast(), port_);
	}
	sock_.writeDatagram(d, QHostAddress::Broadcast, port_);
	// prune peers not heard from for a minute
	bool changed = false;
	for (auto it = peers_.begin(); it != peers_.end();) {
		if (it->second.lastSeen.secsTo(QDateTime::currentDateTime()) > 60) {
			it = peers_.erase(it);
			changed = true;
		} else
			++it;
	}
	if (changed)
		emit peersChanged();
}

void Lan::read()
{
	while (sock_.hasPendingDatagrams()) {
		QByteArray d;
		d.resize((int)sock_.pendingDatagramSize());
		QHostAddress from;
		sock_.readDatagram(d.data(), d.size(), &from);
		QJsonDocument doc = QJsonDocument::fromJson(d);
		if (!doc.isObject())
			continue;
		QJsonObject o = doc.object();
		if (o.value("kennel").toInt() != 1)
			continue;
		QString host = o.value("host").toString();
		if (host.isEmpty() || host == host_)
			continue; // ourselves
		Peer &p = peers_[host];
		bool isNew = p.lastSeen.isNull();
		p.name = o.value("name").toString();
		p.host = host;
		QString a = from.toString();
		if (a.startsWith("::ffff:")) // IPv4 arriving on a dual-stack socket
			a = a.mid(7);
		p.addr = a;
		p.ndi = o.value("ndi").toString();
		p.version = o.value("version").toString();
		p.lastSeen = QDateTime::currentDateTime();
		if (isNew)
			emit peersChanged();
	}
}
