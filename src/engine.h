#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <QObject>
#include <QImage>
#include <QTimer>
#include <QDateTime>
#include "bridge.h"
#include "capture.h"
#include "clips.h"
#include "lan.h"
#include "config.h"
#include "detector.h"
#include "switcher.h"

/// The state machine. Lives on the Qt main thread; capture + matching run on a worker per poll.
class Engine : public QObject {
	Q_OBJECT
public:
	explicit Engine(QObject *parent = nullptr);
	~Engine() override;

	Config cfg;
	Switcher sw;
	Bridge bridge;
	Clips clips;
	Lan lan;
	QString ndiShareName() const { return "Kennel POV"; }
	QString playerName() const;
	void applyLan(); // (re)start discovery + NDI share from cfg
	QString appStatus() const { return appStatus_; }
	bool appConnected() const { return bridge.clients() > 0; }
	void onReplaySaved() { clips.onReplaySaved(); }
	void launchApp();
	void closeApp();          // on OBS exit
	void stopApp();           // user pressed Stop
	bool appRunning() const;  // process alive (even if not connected yet)
	QString appState() const; // "connected" | "starting" | "crashed" | "stopped"
	void pushAppConfig();     // send the ClipHound settings to the app

	/// One line of the game's NEARBY list, as ClipHound read it.
	struct NearbyEntry {
		QString name;         // what the OCR read
		QString match;        // the squad mate's in-game name it matched, "" = nobody we know
		int dist = 0;         // metres
		bool unknown = false; // nearby, but the metres could not be read
	};
	QList<NearbyEntry> nearby() const { return nearby_; }
	bool nearbyFresh() const;
	QString nearbyText() const;   // "MasterBaiter 9 m  ·  ChusanDesu 76 m"
	QString nearbyStatus() const; // the same, or why there is no reading
	/// Index of the configured squad mate the game says is nearest, or -1. Fills metres if given.
	int closestFriend(int *metres = nullptr, QString *problem = nullptr) const;
	void nearbyTest(); // ask ClipHound to read the NEARBY area once and say what it saw
	void twitchLogin();
	void twitchLogout();
	QJsonObject twitchStatus() const { return twitch_; }
signals:
	void twitchStatusChanged();
	void appConfigReceived();
	void nearbyTested(const QJsonObject &result);

public:
	bool applied() const { return applied_; }
	bool dualOn() const { return dualOn_; }
	QString vehicleSeat() const { return vehicleSeat_; }
	bool detected() const { return detected_; }
	bool revivingRecent() const;
	double reviveProgress() const { return reviveProgress_; }
	Match lastGame() const { return lastGame_; }
	Match lastRevive() const { return lastRevive_; }
	bool hasTemplate() const { return detGame_.hasTemplate(); }
	bool customTemplate() const { return cfg.customTemplateWidthFrac > 0; }
	QImage lastFrame() const;
	void wantPreview(bool on) { previewWanted_ = on; }
	std::string stateText() const;
	QStringList recentLog() const { return logLines_; }
	QStringList recentEvents() const { return events_; }
	void addEvent(const QString &text);

	void start();
	void stop();
	void reloadConfig(); // after the settings dialog saved
	void loadTemplates();
	bool needsSetup() const { return cfg.gameSource.empty() || cfg.friends.empty(); }
	/// Tick every desktop-audio input once, when nothing was chosen yet.
	void autoPickAudio();

public slots:
	void applyNow(bool on, const QString &why);
	void toggle();
	void toggleDual();
	void setDual(bool on, const QString &why);

	void setActive(int idx);
	void setEnabled(bool on);
	void captureTemplate();
	void useBuiltInTemplate();
	void previewLook(bool on);
	void clipNow(const QString &title = "manual", const QStringList &tags = {"manual"},
		     const QString &source = "hotkey");
	void log(const QString &msg);

signals:
	void stateChanged();
	void logged(const QString &msg);
	void frameUpdated();

private:
	struct Result {
		bool ok = false;
		Match game;
		Match revive;
		double progress = -1;
		std::vector<uint8_t> bgra;
		int w = 0, h = 0, ls = 0;
	};
	void tick();
	void onResult(Result r);
	void frameTick();
	void onBridgeMessage(const QJsonObject &o);
	void onNearby(const QJsonObject &o);
	void onVehicle(const QString &seat);
	void clearNearby();
	void pickClosest(const QString &why, bool decisive = false);
	void switchTo(int idx, const QString &why);
	int friendIndexFor(const QString &gameName) const;
	int nearbyDistanceOf(int friendIdx) const;
	bool feedUsable(const Friend &f) const;
	void askNearbyNow();
	void sendPov(const QString &state);
	void detect(const Match &m);

	QTimer timer_, frameTimer_, downDelay_, upDelay_;
	std::atomic<bool> busy_{false}, stopping_{false}, frameBusy_{false};
	Capture capGame_, capFriend_, capRoi_;
	QString appStatus_;
	QJsonObject twitch_;
	qint64 appPid_ = 0;
	QDateTime appStartedAt_;
	bool appCrashReported_ = false;
	Detector detGame_, detRevive_;
	bool dualOn_ = false, dualAutoOn_ = false, ndiDelayed_ = false;
	QString vehicleSeat_;
	bool applied_ = false, detected_ = false, applying_ = false, lookPreview_ = false, previewWanted_ = false;
	int downRun_ = 0, upRun_ = 0, tickN_ = 0;
	double peakScore_ = 0;
	std::chrono::steady_clock::time_point downSince_, lastReviveSeen_;
	Match lastGame_, lastRevive_;
	double reviveProgress_ = -1;
	mutable std::mutex frameMx_;
	QImage lastFrame_;
	std::string lastWatchError_;
	QStringList logLines_;
	QStringList events_;
	QList<NearbyEntry> nearby_;
	QDateTime nearbyAt_, nearbyEmptySince_;
	QString nearbyLine_, nearbyWho_;
	std::chrono::steady_clock::time_point lastPick_, lastNearbyWarn_;
};
