#include "speed.h"
#include <QElapsedTimer>

namespace {
constexpr int kBlock = 256 * 1024;         // one write
constexpr qint64 kQueue = 2 * 1024 * 1024; // keep this much in flight so the pipe is never idle
const char *kHello = "KENNEL-SPEED\n";
} // namespace

bool Speed::listen(quint16 port)
{
	if (srv_.isListening())
		return true;
	if (!srv_.listen(QHostAddress::Any, port))
		return false;
	connect(&srv_, &QTcpServer::newConnection, this, [this]() {
		while (QTcpSocket *c = srv_.nextPendingConnection()) {
			auto *bytes = new qint64(0);
			auto *clock = new QElapsedTimer();
			auto *quiet = new QTimer(c);
			quiet->setSingleShot(true);
			quiet->setInterval(500); // they have stopped sending: tell them what landed
			connect(c, &QTcpSocket::readyRead, c, [c, bytes, clock, quiet]() {
				if (!clock->isValid())
					clock->start();
				*bytes += c->readAll().size(); // counted and thrown away
				quiet->start();
			});
			connect(quiet, &QTimer::timeout, c, [c, bytes, clock]() {
				qint64 ms = clock->isValid() ? clock->elapsed() : 0;
				c->write(QString("%1 %2\n").arg(*bytes).arg(ms).toUtf8());
				c->flush();
				c->disconnectFromHost();
			});
			connect(c, &QTcpSocket::disconnected, c, [c, bytes, clock]() {
				delete bytes;
				delete clock;
				c->deleteLater();
			});
		}
	});
	return true;
}

void Speed::stop()
{
	srv_.close();
	if (out_) {
		out_->abort();
		out_->deleteLater();
		out_ = nullptr;
	}
	sendFor_.stop();
	idle_.stop();
}

Speed::Speed(QObject *parent) : QObject(parent)
{
	// once, here: connecting these inside measure() would stack a fresh connection on every test
	sendFor_.setSingleShot(true);
	idle_.setSingleShot(true);
	connect(&sendFor_, &QTimer::timeout, this, [this]() {
		sending_ = false; // the far end goes quiet for half a second, then reports what landed
		idle_.start(4000);
	});
	connect(&idle_, &QTimer::timeout, this, [this]() { finishOut(-1, "the squad mate's OBS did not answer"); });
}

void Speed::pump()
{
	while (sending_ && out_ && out_->bytesToWrite() < kQueue)
		out_->write(block_);
}

void Speed::finishOut(double mbps, const QString &note)
{
	sending_ = false;
	sendFor_.stop();
	idle_.stop();
	if (out_) {
		out_->disconnect(this);
		out_->abort();
		out_->deleteLater();
		out_ = nullptr;
	}
	emit done(mbps, note);
}

void Speed::measure(const QString &host, quint16 port, int seconds)
{
	if (out_) {
		emit done(-1, "a link test is already running");
		return;
	}
	block_ = QByteArray(kBlock, 'K');
	sending_ = true;
	out_ = new QTcpSocket(this);
	connect(out_, &QTcpSocket::connected, this, [this, seconds]() {
		out_->write(kHello);
		pump();
		sendFor_.start(seconds * 1000);
	});
	connect(out_, &QTcpSocket::bytesWritten, this, [this](qint64) { pump(); });
	connect(out_, &QTcpSocket::readyRead, this, [this]() {
		QList<QByteArray> parts = out_->readAll().trimmed().split(' ');
		if (parts.size() != 2) {
			finishOut(-1, "unexpected answer from the squad mate's OBS");
			return;
		}
		double bytes = parts[0].toDouble(), ms = parts[1].toDouble();
		if (ms < 200) {
			finishOut(-1, "it was over too quickly to measure");
			return;
		}
		finishOut(bytes * 8.0 / (ms / 1000.0) / 1e6,
			  QString("%1 MB in %2 s").arg(bytes / 1e6, 0, 'f', 0).arg(ms / 1000.0, 0, 'f', 1));
	});
	connect(out_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		finishOut(-1, out_ ? out_->errorString() : QString("could not connect"));
	});
	out_->connectToHost(host, port);
}
