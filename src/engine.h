#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <QObject>
#include <QImage>
#include <QTimer>
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
	void closeApp();      // on OBS exit
	void pushAppConfig(); // send the ClipHound settings to the app
	void twitchLogin();
	void twitchLogout();
	QJsonObject twitchStatus() const { return twitch_; }
signals:
	void twitchStatusChanged();
	void appConfigReceived();

public:
	bool applied() const { return applied_; }
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
	void sendPov(const QString &state);
	void detect(const Match &m);

	QTimer timer_, frameTimer_;
	std::atomic<bool> busy_{false}, stopping_{false}, frameBusy_{false};
	Capture capGame_, capFriend_, capRoi_;
	QString appStatus_;
	QJsonObject twitch_;
	qint64 appPid_ = 0;
	Detector detGame_, detRevive_;
	bool applied_ = false, detected_ = false, applying_ = false, lookPreview_ = false, previewWanted_ = false;
	int downRun_ = 0, upRun_ = 0, tickN_ = 0;
	std::chrono::steady_clock::time_point downSince_, lastReviveSeen_;
	Match lastGame_, lastRevive_;
	double reviveProgress_ = -1;
	mutable std::mutex frameMx_;
	QImage lastFrame_;
	std::string lastWatchError_;
	QStringList logLines_;
};
