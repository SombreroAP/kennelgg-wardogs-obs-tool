#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QTimer>
#include <QNetworkAccessManager>

/// Who is sitting in the squad's Discord voice channel, and who is sharing their screen.
///
/// Discord gives a third-party program no way to ask this locally: the client's RPC socket
/// puts voice reads behind a scope Discord hands out by hand, application by application.
/// A bot on the gateway can see it, so the Kennel.gg bot writes the roster out and this
/// polls that file. Nothing about the local machine is sent - it is a plain GET.
class Roster : public QObject {
	Q_OBJECT
public:
	struct Member {
		QString name;           // their Discord display name
		QString handle;         // their Discord username: what a popped-out share is titled with
		bool streaming = false; // they have gone live in the call
		bool camera = false;
		QString channel; // the voice channel they are in
	};

	explicit Roster(QObject *parent = nullptr);

	/// Start polling `url` every `seconds`. An empty url stops it.
	void configure(const QString &url, int seconds, const QString &onlyChannel);
	void poll(); // now, out of turn
	void stop();

	QList<Member> members() const { return members_; }
	/// Just the ones sharing their screen, in the order the bot listed them.
	QList<Member> streamers() const;
	QString status() const { return status_; } // for the settings dialog
	bool running() const { return timer_.isActive(); }

signals:
	/// The roster changed: somebody joined, left, went live or stopped.
	void changed();
	/// Polled and nothing moved, or the poll failed - `status()` says which.
	void polled();

private:
	QNetworkAccessManager *net_ = nullptr;
	QTimer timer_;
	QString url_, onlyChannel_, status_ = "off";
	QList<Member> members_;
	bool inFlight_ = false;
};
