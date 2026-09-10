#include "engine.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <thread>
#include <QBuffer>
#include <QJsonArray>
#include <QProcess>
#include <QFileInfo>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QCoreApplication>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <QUrl>
#include <QDir>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QDateTime>
#include <obs-module.h>
#include <plugin-support.h>

using clock_ = std::chrono::steady_clock;

Engine::Engine(QObject *parent) : QObject(parent)
{
	sw.log = [this](const std::string &s) {
		log(QString::fromStdString(s));
	};
	cfg.load();
	loadTemplates();
	detRevive_.fromX = 0.15f;
	detRevive_.toX = 0.60f;
	detRevive_.fromY = 0.55f;
	detRevive_.toY = 0.95f;
	lastReviveSeen_ = clock_::now() - std::chrono::hours(1);
	downSince_ = lastReviveSeen_;
	lastPick_ = lastNearbyWarn_ = lastReviveSeen_;
	connect(&timer_, &QTimer::timeout, this, &Engine::tick);
	connect(&frameTimer_, &QTimer::timeout, this, &Engine::frameTick);
	downDelay_.setSingleShot(true);
	upDelay_.setSingleShot(true);
	connect(&downDelay_, &QTimer::timeout, this, [this]() {
		if (detected_ && !applied_) {
			pickClosest("about to switch", true); // last reading before the feed goes on screen
			applyNow(true, QString("downed for %1 ms").arg(cfg.downDelayMs));
		}
	});
	connect(&upDelay_, &QTimer::timeout, this, [this]() {
		if (!detected_ && applied_)
			applyNow(false, "damage log gone");
	});
	connect(&bridge, &Bridge::message, this, &Engine::onBridgeMessage);
	connect(&bridge, &Bridge::clientConnected, this, [this]() {
		appStatus_ = "connected";
		log("Companion app connected.");
		QJsonObject o;
		o["type"] = "config";
		o["gameSource"] = QString::fromStdString(cfg.gameSource);
		o["povState"] = applied_ ? "downed" : "up";
		bridge.sendJson(o);
		emit stateChanged();
	});
	connect(&bridge, &Bridge::clientDisconnected, this, [this]() {
		appStatus_.clear();
		frameTimer_.stop();
		log("Companion app disconnected.");
		emit stateChanged();
	});
	connect(&clips, &Clips::logged, this, &Engine::log);
	connect(&lan, &Lan::peersChanged, this, [this]() {
		if (cfg.autoAddPeers) {
			bool added = false;
			for (auto &kv : lan.peers()) {
				const Lan::Peer &p = kv.second;
				if (p.ndi.isEmpty())
					continue;
				std::string full = Switcher::ndiFullName(p.host.toStdString(), p.ndi.toStdString());
				bool known = false;
				for (auto &f : cfg.friends)
					if (f.kind == FriendKind::Ndi && f.channel == full)
						known = true;
				if (known)
					continue;
				Friend f;
				f.name = p.name.isEmpty() ? p.host.toStdString() : p.name.toStdString();
				f.kind = FriendKind::Ndi;
				f.channel = full;
				std::string e = sw.createFriendSources(cfg, f);
				if (!e.empty()) {
					log("Squad mate on the LAN (" + p.name +
					    ") found, but: " + QString::fromStdString(e));
					continue;
				}
				cfg.friends.push_back(f);
				added = true;
				log("Squad mate on the LAN added: " + QString::fromStdString(f.name) + " (NDI " +
				    p.ndi + ").");
			}
			if (added) {
				cfg.save();
				if (cfg.keepWarm && !applied_)
					sw.armWarm(cfg);
			}
		}
		emit stateChanged();
	});
	connect(&clips, &Clips::saved, this, [this](const Clips::Entry &e) {
		QJsonObject o;
		o["type"] = "clip_saved";
		o["path"] = e.path;
		o["title"] = e.title;
		o["tags"] = QJsonArray::fromStringList(e.tags);
		bridge.sendJson(o);
		emit stateChanged();
	});
}

QString Engine::playerName() const
{
	return cfg.playerName.empty() ? Lan::hostName() : QString::fromStdString(cfg.playerName);
}

void Engine::applyLan()
{
	lan.setSelf(playerName(), Lan::hostName(), cfg.ndiShare ? ndiShareName() : "", PLUGIN_VERSION);
	if (cfg.lanEnabled) {
		if (!lan.running())
			lan.start((quint16)cfg.lanPort);
	} else
		lan.stop();
	if (cfg.ndiShare) {
		auto start = [this]() {
			if (!cfg.ndiShare || stopping_)
				return;
			std::string e = sw.startNdiShare(ndiShareName().toStdString());
			if (!e.empty())
				log("NDI share: " + QString::fromStdString(e));
		};
		if (!ndiDelayed_) {
			ndiDelayed_ = true; // first time: let every other plugin finish setting up its outputs
			QTimer::singleShot(4000, this, start);
		} else
			start();
	} else
		sw.stopNdiShare();
}

void Engine::launchApp()
{
	if (bridge.clients() > 0) {
		log("ClipHound is already running.");
		return;
	}
	QString p = QString::fromStdString(cfg.appPath);
	const QString def = "C:/ProgramData/Kennel WARDOGS/ClipHound/ClipHound.exe";
	if (p.isEmpty() && QFileInfo::exists(def)) {
		p = def;
		cfg.appPath = def.toStdString();
		cfg.save();
	}
	if (p.isEmpty()) {
		log("ClipHound: no app path set and nothing at " + def + " (Settings → Clips → Browse).");
		return;
	}
	if (!QFileInfo::exists(p)) {
		log("ClipHound not found at " + p + " (Settings → Clips → Browse).");
		return;
	}
	QString dir = QFileInfo(p).absolutePath();
	qint64 pid = 0;
	QProcess proc;
	proc.setProgram(p);
	proc.setWorkingDirectory(dir);
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert("KENNEL_FROM_OBS", "1");
	proc.setProcessEnvironment(env);
	if (proc.startDetached(&pid)) {
		appPid_ = pid;
		appStartedAt_ = QDateTime::currentDateTime();
		appCrashReported_ = false;
		QTimer::singleShot(6000, this, [this]() {
			if (bridge.clients() == 0 && !appRunning() && !appCrashReported_) {
				appCrashReported_ = true;
				log("ClipHound exited right after starting - open Settings → Logs and look at its log (config problem or missing file).");
				emit stateChanged();
			}
		});
		log(QString("Started ClipHound (pid %1): %2").arg(pid).arg(p));
		return;
	}
	// fall back to the shell (handles .bat/.cmd and anything Windows wants to elevate or associate)
	if (QDesktopServices::openUrl(QUrl::fromLocalFile(p)))
		log("Started ClipHound via the shell: " + p);
	else
		log("Could not start ClipHound: " + p + " (try the Start-menu shortcut and send me the Logs).");
}

Engine::~Engine()
{
	stop();
}

void Engine::loadTemplates()
{
	detGame_.threshold = cfg.threshold;
	detRevive_.threshold = cfg.reviveThreshold;
	applySearchWidth();
	bool loaded = false;
	if (cfg.customTemplateWidthFrac > 0) {
		// custom template: raw floats written by captureTemplate()
		std::ifstream in(Config::configFile("template.bin"), std::ios::binary);
		int w = 0, h = 0;
		if (in && in.read((char *)&w, 4) && in.read((char *)&h, 4) && w > 0 && h > 0 && w < 2000 && h < 2000) {
			std::vector<float> g((size_t)w * h);
			if (in.read((char *)g.data(), g.size() * sizeof(float))) {
				detGame_.setTemplate(g, w, h, (float)cfg.customTemplateWidthFrac);
				loaded = true;
			}
		}
	}
	if (!loaded) {
		char *p = obs_module_file("templates/damagelog.png");
		if (p)
			// the wording only: the gap between the "B" key hint and the words is a different
			// fraction of the screen at every resolution, so a template spanning both can only
			// ever be a near miss on somebody else's setup
			detGame_.loadTemplatePng(p, 198.0f / 1704.0f);
		bfree(p);
		cfg.customTemplateWidthFrac = 0;
	}
	if (cfg.memScale > 0)
		detGame_.remember((float)cfg.memScale, (float)cfg.memX, (float)cfg.memY);
	char *r = obs_module_file("templates/reviving.png");
	if (r)
		detRevive_.loadTemplatePng(r, 98.0f / 1875.0f);
	bfree(r);
}

/// How much of the frame, and how many sizes, the damage-log search covers.
void Engine::applySearchWidth()
{
	if (cfg.wideSearch) {
		detGame_.fromX = 0.0f;
		detGame_.toX = 1.0f;
		detGame_.fromY = 0.0f;
		detGame_.toY = 1.0f;
		detGame_.minScale = 0.35f;
		detGame_.maxScale = 2.2f;
	} else {
		detGame_.fromX = 0.45f;
		detGame_.toX = 1.0f;
		detGame_.fromY = 0.15f;
		detGame_.toY = 0.95f;
		detGame_.minScale = 0.5f;
		detGame_.maxScale = 1.6f;
	}
	detGame_.unlock();
}

QImage Engine::grabNative()
{
	if (cfg.gameSource.empty())
		return QImage();
	obs_source_t *src = obs_get_source_by_name(cfg.gameSource.c_str());
	if (!src)
		return QImage();
	int native = (int)obs_source_get_width(src);
	std::vector<uint8_t> bgra;
	int w = 0, h = 0, ls = 0;
	bool ok = native > 0 && capRoi_.grab(src, native, bgra, w, h, ls);
	obs_source_release(src);
	if (!ok)
		return QImage();
	return QImage((const uchar *)bgra.data(), w, h, ls, QImage::Format_ARGB32).copy();
}

/// Cut the template out of this frame at `rect` (fractions), so it is this HUD's own pixels: the
/// key hint, the gap and the wording differ between HUDs, and a template from someone else's
/// screen can only ever be a near miss.
QString Engine::learnTemplate(const QImage &img, QRectF rect)
{
	if (img.isNull() || rect.width() <= 0)
		return "No frame to learn from.";
	int x = (int)std::lround(rect.x() * img.width()), y = (int)std::lround(rect.y() * img.height());
	int w = (int)std::lround(rect.width() * img.width()), h = (int)std::lround(rect.height() * img.height());
	int mx = std::max(2, w / 20), my = std::max(2, h / 5); // a little margin, the match box is tight
	x = std::clamp(x - mx, 0, img.width() - 8);
	y = std::clamp(y - my, 0, img.height() - 8);
	w = std::min(w + 2 * mx, img.width() - x);
	h = std::min(h + 2 * my, img.height() - y);
	if (w < 16 || h < 6)
		return "That is too small to learn from.";
	std::vector<float> g((size_t)w * h);
	for (int yy = 0; yy < h; yy++)
		for (int xx = 0; xx < w; xx++) {
			QRgb p = img.pixel(x + xx, y + yy);
			g[(size_t)yy * w + xx] = 0.299f * qRed(p) + 0.587f * qGreen(p) + 0.114f * qBlue(p);
		}
	cfg.customTemplateWidthFrac = (double)w / img.width();
	detGame_.setTemplate(g, w, h, (float)cfg.customTemplateWidthFrac);
	std::ofstream out(Config::configFile("template.bin"), std::ios::binary);
	out.write((const char *)&w, 4);
	out.write((const char *)&h, 4);
	out.write((const char *)g.data(), g.size() * sizeof(float));
	cfg.memScale = cfg.memX = cfg.memY = 0;
	cfg.save();
	downRun_ = upRun_ = 0;
	log(QString("Learned this HUD's damage log: %1x%2 px, %3 of the width.")
		    .arg(w)
		    .arg(h)
		    .arg(cfg.customTemplateWidthFrac, 0, 'f', 3));
	emit stateChanged();
	return "";
}

/// A PNG of the game source exactly as the plugin sees it, for working out why a HUD is not matched.
QString Engine::saveFrame()
{
	QImage img = grabNative();
	if (img.isNull())
		return "Could not render the game source '" + QString::fromStdString(cfg.gameSource) +
		       "' (is a game source set, and showing something?).";
	QString dir = QString::fromStdString(Config::configDir());
	QDir().mkpath(dir);
	QString path = dir + "/frame-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".png";
	if (!img.copy().save(path, "PNG"))
		return "Could not write " + path;
	log("Saved a frame for diagnosis: " + path);
	return path;
}

void Engine::autoPickAudio()
{
	if (cfg.audioAutoPicked || !cfg.muteWhileDowned.empty())
		return;
	for (auto &i : Switcher::inputs())
		if (i.second == "wasapi_output_capture")
			cfg.muteWhileDowned.push_back(i.first);
	cfg.audioAutoPicked = true;
	if (!cfg.muteWhileDowned.empty())
		log("Ticked your desktop audio to mute while downed (change it in Settings → Switch).");
	cfg.save();
}

void Engine::start()
{
	autoPickAudio();
	if (cfg.appPath.empty()) {
		// the installer puts ClipHound here; adopt it once so the app starts with OBS
		QString def = "C:/ProgramData/Kennel WARDOGS/ClipHound/ClipHound.exe";
		if (QFileInfo::exists(def)) {
			cfg.appPath = def.toStdString();
			cfg.launchApp = true;
			cfg.save();
			log("Found ClipHound from the installer; it will start with OBS (Settings → Clips).");
		}
	}
	clips.nameTemplate = QString::fromStdString(cfg.clipNameTemplate);
	clips.folder = QString::fromStdString(cfg.clipFolder);
	clips.watchFolders = cfg.backtrackFolder.empty() ? QStringList()
							 : QStringList{QString::fromStdString(cfg.backtrackFolder)};
	clips.autoStartReplay = cfg.autoStartReplay && cfg.clipUseReplay;
	clips.useReplay = cfg.clipUseReplay;
	clips.hotkeys.clear();
	for (auto &h : cfg.clipHotkeys)
		clips.hotkeys << QString::fromStdString(h);
	if (cfg.bridgeEnabled)
		bridge.listen((quint16)cfg.bridgePort);
	if (cfg.autoStartReplay && cfg.clipUseReplay)
		clips.ensureReplayBuffer();
	if (cfg.launchApp)
		launchApp();
	applyLan();
	timer_.start(std::max(100, cfg.pollMs));
	if (cfg.keepWarm && !applied_ && cfg.active())
		sw.armWarm(cfg);
	QTimer::singleShot(15000, this, [this]() {
		if (!stopping_)
			checkForUpdate(false); // once per OBS start, well after everything is up
	});
	if (cfg.dualEnabled && cfg.dual())
		QTimer::singleShot(2500, this, [this]() { // after the browser module is fully up
			if (!stopping_ && cfg.dualEnabled && cfg.dual())
				setDual(true, "on at start-up (Dual POV tab)");
		});
	emit stateChanged();
}

void Engine::pushAppConfig()
{
	if (bridge.clients() == 0) {
		cfg.appConfigDirty = true;
		cfg.save();
		return;
	}
	QJsonObject set;
	set["player_name"] = QString::fromStdString(cfg.appPlayerName);
	set["library"] = QString::fromStdString(cfg.appLibrary);
	set["broadcaster"] = QString::fromStdString(cfg.appBroadcaster);
	set["twitch_enabled"] = cfg.appTwitchEnabled;
	set["clip_every_kill"] = cfg.appEveryKill;
	set["roi"] = QJsonArray{cfg.feedX, cfg.feedY, cfg.feedW, cfg.feedH};
	set["multikill_window"] = cfg.appMultikillWindow;
	set["fps"] = cfg.appFps > 0 ? cfg.appFps : 10;
	QJsonObject nb;
	nb["enabled"] = cfg.nearEnabled;
	nb["roi"] = QJsonArray{cfg.nearX, cfg.nearY, cfg.nearW, cfg.nearH};
	QJsonArray names;
	for (const auto &f : cfg.friends)
		if (!f.nearName().empty())
			names.append(QString::fromStdString(f.nearName()));
	nb["names"] = names;
	set["nearby"] = nb;
	QJsonObject vh;
	// read the vehicle corner whenever the window could be turned on by it, or is up and must go
	// when you get out - whichever way it was turned on
	vh["enabled"] = cfg.dual() != nullptr && (cfg.dualAuto || (dualOn_ && !cfg.dualKeep));
	vh["roi"] = QJsonArray{cfg.vehX, cfg.vehY, cfg.vehW, cfg.vehH};
	set["vehicle"] = vh;
	QJsonObject o;
	o["type"] = "app_config";
	o["set"] = set;
	bridge.sendJson(o);
	cfg.appConfigDirty = false;
	cfg.save();
}

// ----- is there a newer build? -----

/// "0.4.10" is newer than "0.4.9": compare the numbers, not the text.
bool Engine::isNewer(const QString &a, const QString &b)
{
	QStringList x = a.split('.'), y = b.split('.');
	for (int i = 0; i < std::max(x.size(), y.size()); i++) {
		int ax = i < x.size() ? x[i].section(QRegularExpression("[^0-9]"), 0, 0).toInt() : 0;
		int by = i < y.size() ? y[i].section(QRegularExpression("[^0-9]"), 0, 0).toInt() : 0;
		if (ax != by)
			return ax > by;
	}
	return false;
}

bool Engine::updateAvailable() const
{
	return !newVersion_.isEmpty() && isNewer(newVersion_, PLUGIN_VERSION) &&
	       newVersion_.toStdString() != cfg.updateSkip;
}

void Engine::checkForUpdate(bool manual)
{
	if (!manual && !cfg.updateCheck)
		return;
	QString url = QString::fromStdString(cfg.updateUrl);
	if (url.isEmpty()) {
		updateState_ = "no update address set";
		emit updateChecked();
		return;
	}
	if (!net_)
		net_ = new QNetworkAccessManager(this);
	updateState_ = "checking...";
	emit updateChecked();
	QNetworkRequest req{QUrl(url)};
	req.setHeader(QNetworkRequest::UserAgentHeader, QString("KennelWardogsOBS/%1").arg(PLUGIN_VERSION));
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply *r = net_->get(req);
	QTimer::singleShot(8000, r, [r]() {
		if (r->isRunning())
			r->abort();
	});
	connect(r, &QNetworkReply::finished, this, [this, r, manual]() {
		r->deleteLater();
		if (r->error() != QNetworkReply::NoError) {
			updateState_ = "could not check (" + r->errorString() + ")";
			if (manual)
				log("Update check: " + updateState_);
			emit updateChecked();
			return;
		}
		QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
		newVersion_ = o.value("version").toString();
		newUrl_ = o.value("url").toString();
		newNotes_ = o.value("notes").toString();
		if (newVersion_.isEmpty())
			updateState_ = "nothing published to check against yet";
		else if (isNewer(newVersion_, PLUGIN_VERSION)) {
			updateState_ = newVersion_ + " is out (you have " + QString(PLUGIN_VERSION) + ")";
			log("A newer build is out: " + newVersion_ + (newNotes_.isEmpty() ? "" : " - " + newNotes_) +
			    (newUrl_.isEmpty() ? "" : "  " + newUrl_));
		} else
			updateState_ = "up to date (" + QString(PLUGIN_VERSION) + ")";
		emit updateChecked();
		emit stateChanged();
	});
}

void Engine::twitchLogin()
{
	if (bridge.clients() == 0) {
		log("Twitch login needs ClipHound running (Settings → Clips → Start now).");
		return;
	}
	QJsonObject o;
	o["type"] = "twitch_login";
	bridge.sendJson(o);
}

void Engine::twitchLogout()
{
	QJsonObject o;
	o["type"] = "twitch_logout";
	bridge.sendJson(o);
}

bool Engine::appRunning() const
{
#ifdef _WIN32
	if (appPid_ <= 0)
		return false;
	HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)appPid_);
	if (!h)
		return false;
	bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
	CloseHandle(h);
	return alive;
#else
	return appPid_ > 0;
#endif
}

QString Engine::appState() const
{
	if (bridge.clients() > 0)
		return "connected";
	if (appRunning())
		return "starting";
	if (appPid_ > 0 && appStartedAt_.isValid() && appStartedAt_.secsTo(QDateTime::currentDateTime()) < 120)
		return "crashed";
	return "stopped";
}

void Engine::stopApp()
{
	if (bridge.clients() > 0) {
		QJsonObject o;
		o["type"] = "shutdown";
		bridge.sendJson(o);
	}
#ifdef _WIN32
	if (appPid_ > 0) {
		HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, (DWORD)appPid_);
		if (h) {
			if (WaitForSingleObject(h, 1500) == WAIT_TIMEOUT)
				TerminateProcess(h, 0);
			CloseHandle(h);
		}
	}
#endif
	appPid_ = 0;
	log("ClipHound stopped.");
	emit stateChanged();
}

void Engine::closeApp()
{
	if (!cfg.closeAppWithObs)
		return;
	if (bridge.clients() > 0) {
		QJsonObject o;
		o["type"] = "shutdown";
		bridge.sendJson(o);
		// give it a moment to exit cleanly before the socket goes away
		QElapsedTimer t;
		t.start();
		while (bridge.clients() > 0 && t.elapsed() < 1500)
			QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
	}
#ifdef _WIN32
	if (appPid_ > 0) {
		HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, (DWORD)appPid_);
		if (h) {
			if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT)
				TerminateProcess(h, 0);
			CloseHandle(h);
		}
		appPid_ = 0;
	}
#endif
}

void Engine::stop()
{
	stopping_ = true;
	closeApp();
	timer_.stop();
	frameTimer_.stop();
	downDelay_.stop();
	upDelay_.stop();
	bridge.close();
	lan.stop();
	sw.shutdown(); // NDI output, the dual-POV scene and its browser page, before obs-browser unloads
	for (int i = 0; i < 50 && busy_; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void Engine::reloadConfig()
{
	clips.nameTemplate = QString::fromStdString(cfg.clipNameTemplate);
	clips.folder = QString::fromStdString(cfg.clipFolder);
	clips.watchFolders = cfg.backtrackFolder.empty() ? QStringList()
							 : QStringList{QString::fromStdString(cfg.backtrackFolder)};
	clips.autoStartReplay = cfg.autoStartReplay && cfg.clipUseReplay;
	clips.useReplay = cfg.clipUseReplay;
	clips.hotkeys.clear();
	for (auto &h : cfg.clipHotkeys)
		clips.hotkeys << QString::fromStdString(h);
	if (cfg.bridgeEnabled && (!bridge.listening() || bridge.port() != cfg.bridgePort))
		bridge.listen((quint16)cfg.bridgePort);
	else if (!cfg.bridgeEnabled && bridge.listening())
		bridge.close();
	applyLan();
	detGame_.threshold = cfg.threshold;
	detRevive_.threshold = cfg.reviveThreshold;
	detGame_.unlock();
	timer_.setInterval(std::max(100, cfg.pollMs));
	if (cfg.keepWarm && !applied_ && cfg.active())
		sw.armWarm(cfg);
	sw.raiseOnTop(cfg); // the camera and alerts list may have just changed
	pushAppConfig();    // areas, names and rules the app reads
	emit stateChanged();
}

bool Engine::revivingRecent() const
{
	return clock_::now() - lastReviveSeen_ < std::chrono::seconds(3);
}

QImage Engine::lastFrame() const
{
	std::lock_guard<std::mutex> lk(frameMx_);
	return lastFrame_;
}

std::string Engine::stateText() const
{
	const Friend *f = cfg.active();
	std::string name = f ? f->name : "friend";
	if (!cfg.enabled)
		return "Paused";
	if (applied_)
		return revivingRecent() ? "Showing " + name + " - being revived" : "Showing " + name + "'s POV";
	if (cfg.gameSource.empty())
		return "No game source set";
	if (!cfg.autoDetect || !detGame_.hasTemplate())
		return "Manual only";
	return "Watching your POV";
}

void Engine::addEvent(const QString &text)
{
	events_ << QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + text;
	while (events_.size() > 30)
		events_.removeFirst();
	log("Event: " + text);
	emit stateChanged();
}

void Engine::log(const QString &msg)
{
	obs_log(LOG_INFO, "%s", msg.toUtf8().constData());
	logLines_ << QDateTime::currentDateTime().toString("HH:mm:ss.zzz") + "  " + msg;
	while (logLines_.size() > 500)
		logLines_.removeFirst();
	emit logged(msg);
}

// ----- the companion app -----

void Engine::onBridgeMessage(const QJsonObject &o)
{
	QString type = o.value("type").toString();
	if (type == "subscribe") {
		double fps = bridge.wantedFps();
		if (fps > 0)
			frameTimer_.start((int)(1000.0 / fps));
		else
			frameTimer_.stop();
	} else if (type == "clip") {
		QStringList tags;
		for (auto v : o.value("tags").toArray())
			tags << v.toString();
		QString err = clips.request(o.value("title").toString(), tags, o.value("source").toString("app"));
		QJsonObject r;
		r["type"] = "clip_result";
		r["ok"] = err.isEmpty();
		r["error"] = err;
		r["id"] = o.value("id");
		bridge.sendJson(r);
		if (!err.isEmpty())
			log("Clip request: " + err);
	} else if (type == "app_config" && o.contains("values")) {
		QJsonObject v = o.value("values").toObject();
		if (cfg.appConfigDirty) {
			pushAppConfig(); // ours wins: the user edited while the app was away
		} else {
			if (v.value("player_name").toString().isEmpty() && !cfg.appPlayerName.empty()) {
				pushAppConfig(); // the app came back with a fresh config: give it ours
				return;
			}
			cfg.appPlayerName = v.value("player_name").toString().toStdString();
			cfg.appLibrary = v.value("library").toString().toStdString();
			cfg.appBroadcaster = v.value("broadcaster").toString().toStdString();
			cfg.appTwitchEnabled = v.value("twitch_enabled").toBool();
			cfg.appEveryKill = v.value("clip_every_kill").toBool();
			cfg.appMultikillWindow = v.value("multikill_window").toDouble(30);
			cfg.save();
			emit appConfigReceived();
			// the kill-feed and NEARBY areas are picked in this window, so ours win
			QJsonArray r = v.value("roi").toArray();
			QJsonObject nb = v.value("nearby").toObject();
			QJsonArray nr = nb.value("roi").toArray();
			auto same = [](const QJsonArray &a, double x, double y, double w, double h) {
				return a.size() == 4 && std::abs(a[0].toDouble() - x) < 1e-4 &&
				       std::abs(a[1].toDouble() - y) < 1e-4 && std::abs(a[2].toDouble() - w) < 1e-4 &&
				       std::abs(a[3].toDouble() - h) < 1e-4;
			};
			if (!same(r, cfg.feedX, cfg.feedY, cfg.feedW, cfg.feedH) ||
			    !same(nr, cfg.nearX, cfg.nearY, cfg.nearW, cfg.nearH) ||
			    nb.value("enabled").toBool() != cfg.nearEnabled ||
			    (int)v.value("fps").toDouble() != cfg.appFps)
				pushAppConfig();
		}
	} else if (type == "twitch_status") {
		twitch_ = o;
		QString st = o.value("state").toString();
		if (st == "ok" && !o.value("login").toString().isEmpty())
			log("Twitch: logged in as " + o.value("login").toString() + ".");
		else if (st == "error")
			log("Twitch login: " + o.value("error").toString());
		emit twitchStatusChanged();
	} else if (type == "nearby") {
		onNearby(o);
	} else if (type == "vehicle") {
		onVehicle(o.value("seat").toString());
	} else if (type == "nearby_test_result") {
		QStringList texts;
		for (auto v : o.value("texts").toArray())
			texts << v.toString();
		QStringList dists;
		for (auto v : o.value("dists").toArray())
			dists << v.toString();
		log(QString("Test read of the NEARBY area: %1 row(s), %2 chip(s); names read [%3]; metres read [%4].")
			    .arg(o.value("rows").toInt())
			    .arg(o.value("chips").toInt())
			    .arg(texts.join(" | "), dists.join(" | ")));
		emit nearbyTested(o);
	} else if (type == "event") {
		addEvent(o.value("text").toString());
	} else if (type == "status") {
		appStatus_ = o.value("text").toString();
		emit stateChanged();
	} else if (type == "pov") {
		QString force = o.value("force").toString();
		if (force == "downed")
			applyNow(true, "companion app");
		else if (force == "up")
			applyNow(false, "companion app");
	}
}

// ----- the game's NEARBY list (bottom right): who is closest -----

void Engine::clearNearby()
{
	if (nearby_.isEmpty() && !nearbyAt_.isValid())
		return;
	nearby_.clear();
	nearbyAt_ = QDateTime();
	nearbyEmptySince_ = QDateTime();
	nearbyLine_.clear();
	nearbyWho_.clear();
	emit stateChanged();
}

void Engine::onNearby(const QJsonObject &o)
{
	if (!detected_ && !applied_) {
		clearNearby(); // you are up: who was near you a moment ago is not relevant
		return;
	}
	QList<NearbyEntry> list;
	for (auto v : o.value("list").toArray()) {
		QJsonObject e = v.toObject();
		NearbyEntry n;
		n.name = e.value("name").toString();
		n.match = e.value("match").toString();
		n.dist = e.value("dist").toInt(-1);
		n.unknown = e.value("unknown").toBool() || n.dist >= 998;
		if (n.dist >= 0)
			list << n;
	}
	// an empty reading is usually one bad frame (ClipHound only reports empty after three in a
	// row), so it never throws away a good list: freshness decides when that list stops counting
	if (!list.isEmpty()) {
		nearby_ = list;
		nearbyAt_ = QDateTime::currentDateTime();
		nearbyEmptySince_ = QDateTime();
	} else if (!nearbyEmptySince_.isValid())
		nearbyEmptySince_ = QDateTime::currentDateTime();
	QString line = nearbyText();
	if (line != nearbyLine_) {
		nearbyLine_ = line;
		emit stateChanged(); // the dock shows the metres; they change constantly
	}
	QStringList who;
	for (const auto &e : nearby_)
		who << (e.match.isEmpty() ? e.name : e.match);
	if (who.join(',') != nearbyWho_) { // only who is there is worth a log line
		nearbyWho_ = who.join(',');
		log("Nearby: " + (line.isEmpty() ? QString("nobody") : line));
	}
	// follow the closest all the time, so the dock always shows who would be used and that feed
	// is the one kept warm; the margin and the cooldown inside pickClosest stop it flapping
	if (cfg.nearEnabled)
		pickClosest(applied_ ? "still down" : detected_ ? "going down" : "nearest");
}

bool Engine::nearbyFresh() const
{
	return nearbyAt_.isValid() && nearbyAt_.secsTo(QDateTime::currentDateTime()) <= std::max(2, cfg.nearTtlS);
}

QString Engine::nearbyText() const
{
	QStringList parts;
	for (const auto &e : nearby_)
		parts << (e.match.isEmpty() ? e.name : e.match) +
				 (e.unknown ? QString(" ? m") : QString(" %1 m").arg(e.dist));
	return parts.join("  ·  ");
}

/// One line for the dock: the reading, or why there is not one.
QString Engine::nearbyStatus() const
{
	if (!cfg.nearEnabled)
		return "off";
	if (bridge.clients() == 0)
		return "ClipHound is NOT running - Closest cannot work until it is (dock → Start ClipHound)";
	if (!detected_ && !applied_)
		return "N/A while you are up";
	if (!nearbyAt_.isValid())
		return nearbyEmptySince_.isValid() ? "nobody matched yet - use Test read on the Detect tab"
						   : (bridge.clients() > 0 ? "read when you go down (nothing read yet)"
									   : "ClipHound is not running");
	QString t = nearbyText();
	if (nearbyFresh())
		return nearbyEmptySince_.isValid() ? t + "  (last seen)" : t;
	return t + QString("  (%1 s old)").arg(nearbyAt_.secsTo(QDateTime::currentDateTime()));
}

int Engine::friendIndexFor(const QString &gameName) const
{
	if (gameName.isEmpty())
		return -1;
	for (size_t i = 0; i < cfg.friends.size(); i++)
		if (QString::fromStdString(cfg.friends[i].nearName()).compare(gameName, Qt::CaseInsensitive) == 0)
			return (int)i;
	for (size_t i = 0; i < cfg.friends.size(); i++)
		if (QString::fromStdString(cfg.friends[i].name).compare(gameName, Qt::CaseInsensitive) == 0)
			return (int)i;
	return -1;
}

bool Engine::feedUsable(const Friend &f) const
{
	if (f.isWeb())
		return !f.channel.empty();
	std::string n = cfg.sourceFor(f);
	if (n.empty())
		return false;
	obs_source_t *src = obs_get_source_by_name(n.c_str());
	if (!src)
		return false;
	obs_source_release(src);
	return true;
}

int Engine::nearbyDistanceOf(int friendIdx) const
{
	if (friendIdx < 0 || friendIdx >= (int)cfg.friends.size())
		return -1;
	for (const auto &e : nearby_)
		if (!e.match.isEmpty() && friendIndexFor(e.match) == friendIdx)
			return e.dist;
	return -1;
}

int Engine::closestFriend(int *metres, QString *problem) const
{
	if (!nearbyFresh())
		return -1;
	int best = -1, bestD = 0;
	for (const auto &e : nearby_) {
		if (e.match.isEmpty() || e.dist < 0)
			continue;
		int i = friendIndexFor(e.match);
		if (i < 0) {
			if (problem)
				*problem = e.match + " is nearby but is not one of your squad mates here";
			continue;
		}
		if (!feedUsable(cfg.friends[i])) {
			if (problem)
				*problem = QString::fromStdString(cfg.friends[i].name) +
					   " is nearby but their feed is not usable (source missing in OBS?)";
			continue;
		}
		if (best < 0 || e.dist < bestD) {
			best = i;
			bestD = e.dist;
		}
	}
	if (best >= 0 && metres)
		*metres = bestD;
	return best;
}

void Engine::nearbyTest()
{
	if (bridge.clients() == 0) {
		log("Test read: ClipHound is not running (dock → Start ClipHound).");
		QJsonObject o;
		o["error"] = "ClipHound is not running";
		emit nearbyTested(o);
		return;
	}
	QJsonObject o;
	o["type"] = "nearby_test";
	bridge.sendJson(o);
}

void Engine::askNearbyNow()
{
	if (!cfg.nearEnabled || bridge.clients() == 0)
		return;
	QJsonObject o;
	o["type"] = "nearby_now";
	bridge.sendJson(o);
}

/// Make the squad mate the game says is nearest the active one. Cheap and safe to call often.
void Engine::pickClosest(const QString &why, bool decisive)
{
	if (!cfg.nearEnabled || cfg.friends.size() < 2)
		return;
	int d = 0;
	QString problem;
	int idx = closestFriend(&d, &problem);
	if (idx < 0) {
		if (clock_::now() - lastNearbyWarn_ > std::chrono::seconds(60)) {
			lastNearbyWarn_ = clock_::now();
			if (!problem.isEmpty())
				log("Closest squad mate: " + problem + ".");
			else
				log(bridge.clients() == 0
					    ? "Closest squad mate: ClipHound is not running, so the NEARBY list cannot be read - keeping the squad mate you picked."
					    : (nearbyFresh()
						       ? "Closest squad mate: nobody in the NEARBY list is one of your squad mates (check their in-game names in Settings → Switch)."
						       : "Closest squad mate: no reading from the NEARBY list yet (check the blue box on the Detect tab)."));
		}
		return;
	}
	if (idx == cfg.activeFriend)
		return;
	if (applied_ && !cfg.nearFollow)
		return; // showing someone already and the user asked not to change mid-swap
	// Going down (not on screen yet): every reading is decisive, the nearest one wins outright.
	// On screen: the "wait between swaps" slider is the only thing holding a swap back.
	if (applied_ && !decisive) {
		// The range rule only protects a squad mate who is still in the list. If the one on
		// screen has left it (dead, far away, not near you), any streaming squad mate in the list
		// is better than them, however far - the ones closest to you may not be streaming at all.
		int cur = nearbyDistanceOf(cfg.activeFriend);
		if (cur >= 0 && cfg.nearMaxM > 0 && d > cfg.nearMaxM)
			return; // too far to be the one coming for you: stay on who is on screen
		auto left = std::chrono::seconds(std::clamp(cfg.nearCooldownS, 1, 10)) - (clock_::now() - lastPick_);
		if (left.count() > 0) {
			if (clock_::now() - lastNearbyWarn_ > std::chrono::seconds(5)) {
				lastNearbyWarn_ = clock_::now();
				log(QString("Closest is %1 at %2 m; keeping %3 for another %4 s (wait between swaps).")
					    .arg(QString::fromStdString(cfg.friends[idx].name))
					    .arg(d)
					    .arg(QString::fromStdString(cfg.active() ? cfg.active()->name : ""))
					    .arg((int)std::chrono::duration_cast<std::chrono::seconds>(left).count() +
						 1));
			}
			return;
		}
	}
	QString nm = QString::fromStdString(cfg.friends[idx].name);
	QString ign = QString::fromStdString(cfg.friends[idx].nearName());
	if (ign.compare(nm, Qt::CaseInsensitive) != 0)
		nm += " (in game " + ign + ")";
	switchTo(idx, QString("%1 is closest at %2 m - %3 - read [%4]").arg(nm).arg(d).arg(why, nearbyText()));
}

/// setActive() without the "you chose this" wording: used by the closest-squad-mate picker.
void Engine::switchTo(int idx, const QString &why)
{
	if (idx < 0 || idx >= (int)cfg.friends.size() || idx == cfg.activeFriend)
		return;
	bool wasOn = applied_;
	if (wasOn)
		applyNow(false, "switching squad mate");
	cfg.activeFriend = idx;
	cfg.save();
	lastPick_ = clock_::now();
	if (wasOn)
		applyNow(true, why);
	else if (cfg.keepWarm)
		sw.armWarm(cfg);
	log("Squad mate: " + why + ".");
	addEvent("Closest: " + QString::fromStdString(cfg.friends[idx].name));
	emit stateChanged();
}

void Engine::sendPov(const QString &state)
{
	if (bridge.clients() == 0)
		return;
	QJsonObject o;
	o["type"] = "pov";
	o["state"] = state;
	const Friend *f = cfg.active();
	o["friend"] = f ? QString::fromStdString(f->name) : "";
	bridge.sendJson(o);
}

/// Native-resolution crop of the game source for the companion app (the kill feed is ~9 px text at 1080p).
void Engine::frameTick()
{
	if (frameBusy_ || stopping_ || cfg.gameSource.empty() || bridge.clients() == 0)
		return;
	frameBusy_ = true;
	QRectF roi = bridge.wantedRoi();
	int width = bridge.wantedWidth();
	std::string name = cfg.gameSource;
	std::thread([this, roi, width, name]() {
		QByteArray jpeg;
		int cw = 0, ch = 0;
		obs_source_t *src = obs_get_source_by_name(name.c_str());
		if (src) {
			int native = (int)obs_source_get_width(src);
			std::vector<uint8_t> bgra;
			int w, h, ls;
			if (native > 0 && capRoi_.grab(src, native, bgra, w, h, ls)) {
				QImage img(bgra.data(), w, h, ls, QImage::Format_ARGB32);
				QRect r((int)(roi.x() * w), (int)(roi.y() * h), (int)(roi.width() * w),
					(int)(roi.height() * h));
				r &= QRect(0, 0, w, h);
				QImage crop = img.copy(r);
				if (width > 0 && width < crop.width())
					crop = crop.scaledToWidth(width, Qt::SmoothTransformation);
				cw = crop.width();
				ch = crop.height();
				QBuffer buf(&jpeg);
				buf.open(QIODevice::WriteOnly);
				crop.save(&buf, "JPG", 88);
			}
			obs_source_release(src);
		}
		qint64 ts = QDateTime::currentMSecsSinceEpoch();
		QMetaObject::invokeMethod(
			this,
			[this, jpeg, cw, ch, ts]() {
				frameBusy_ = false;
				if (!jpeg.isEmpty())
					bridge.sendFrame(jpeg, cw, ch, ts);
			},
			Qt::QueuedConnection);
	}).detach();
}

void Engine::clipNow(const QString &title, const QStringList &tags, const QString &source)
{
	QString err = clips.request(title, tags, source);
	if (!err.isEmpty())
		log("Clip: " + err);
	emit stateChanged();
}

// ----- the poll -----

void Engine::tick()
{
	if (busy_ || stopping_ || cfg.gameSource.empty())
		return;
	// The full-frame search is the expensive path and it runs exactly while you are alive (nothing to lock
	// on to). Doing it on every third poll keeps it near 1-2 % of a core; once locked, every poll is cheap.
	tickN_++;
	bool quick = !lastGame_.locked && !revivingRecent() && (tickN_ % 6) != 0;
	// the friend-feed "REVIVING" search is the expensive one: full search every 5th poll, cheap remembered-spot check otherwise,
	// so the damage-log poll (what switches you back) keeps its 100 ms cadence while the friend is on screen
	bool reviveFull = (tickN_ % 5) == 0;
	busy_ = true;
	bool wantRevive = applied_ && cfg.watchRevive && cfg.active();
	std::string gameName = cfg.gameSource, friendName = wantRevive ? cfg.sourceFor(*cfg.active()) : "";
	bool preview = previewWanted_;

	std::thread([this, gameName, friendName, wantRevive, preview, quick, reviveFull]() {
		Result r;
		obs_source_t *src = obs_get_source_by_name(gameName.c_str());
		if (src) {
			std::vector<uint8_t> bgra;
			int w, h, ls;
			if (capGame_.grab(src, Detector::FrameWidth, bgra, w, h, ls)) {
				Frame f = Detector::fromBGRA(bgra.data(), w, h, ls);
				r.game = detGame_.compare(f, quick);
				r.ok = true;
				if (preview) {
					r.bgra = std::move(bgra);
					r.w = w;
					r.h = h;
					r.ls = ls;
				}
			}
			obs_source_release(src);
		}
		if (wantRevive && (reviveFull || detRevive_.remembers())) {
			obs_source_t *fs = obs_get_source_by_name(friendName.c_str());
			if (fs) {
				std::vector<uint8_t> bgra;
				int w, h, ls;
				if (capFriend_.grab(fs, Detector::FrameWidth, bgra, w, h, ls)) {
					Frame f = Detector::fromBGRA(bgra.data(), w, h, ls);
					r.revive = detRevive_.compare(f, !reviveFull);
					if (r.revive.score >= detRevive_.threshold) {
						// the progress ring sits at a fixed offset below the word (measured on a real frame)
						float tw = r.revive.w * w;
						float cx = r.revive.x * w + 0.47f * tw,
						      cy = r.revive.y * h + 1.31f * tw, rad = 0.39f * tw;
						int lit = 0, n = 72;
						for (int i = 0; i < n; i++) {
							float a = (float)i / n * 6.2831853f;
							bool on = false;
							for (int dr = -1; dr <= 1 && !on; dr++) {
								int px = (int)std::lround(cx +
											  (rad + dr) * std::cos(a)),
								    py = (int)std::lround(cy +
											  (rad + dr) * std::sin(a));
								if (px >= 0 && py >= 0 && px < w && py < h &&
								    f.gray[(size_t)py * w + px] > 170)
									on = true;
							}
							lit += on;
						}
						r.progress = (double)lit / n;
					}
				}
				obs_source_release(fs);
			}
		}
		QMetaObject::invokeMethod(
			this, [this, r = std::move(r)]() mutable { onResult(std::move(r)); }, Qt::QueuedConnection);
	}).detach();
}

void Engine::onResult(Result r)
{
	busy_ = false;
	if (stopping_)
		return;
	if (!r.ok) {
		std::string e = "Watch: cannot render game source '" + cfg.gameSource + "'.";
		if (lastWatchError_ != e) {
			lastWatchError_ = e;
			log(QString::fromStdString(e));
		}
		return;
	}
	lastWatchError_.clear();
	if (r.game.locked && !lastGame_.locked && detGame_.remembers()) {
		cfg.memScale = detGame_.memScale();
		cfg.memX = detGame_.memX();
		cfg.memY = detGame_.memY();
		cfg.save();
	}
	lastGame_ = r.game;
	lastRevive_ = r.revive;
	if (r.revive.score >= cfg.reviveThreshold) {
		bool was = revivingRecent();
		lastReviveSeen_ = clock_::now();
		reviveProgress_ = r.progress;
		if (!was) {
			log("Friend is reviving you - switching back the instant the damage log goes.");
			sendPov("reviving");
		}
	} else if (!revivingRecent())
		reviveProgress_ = -1;
	timer_.setInterval(revivingRecent() ? 100 : std::max(100, cfg.pollMs));
	if (!r.bgra.empty()) {
		QImage img(r.bgra.data(), r.w, r.h, r.ls, QImage::Format_ARGB32);
		std::lock_guard<std::mutex> lk(frameMx_);
		lastFrame_ = img.copy();
	}
	detect(r.game);
	emit frameUpdated();
}

void Engine::detect(const Match &m)
{
	if (!cfg.enabled || !cfg.autoDetect || m.score < 0)
		return;
	bool match = m.score >= cfg.threshold;
	if (detected_) {
		if (m.score > peakScore_)
			peakScore_ = m.score;
		// Hold on. A bright sky behind the translucent panel, smoke or a muzzle flash washes the
		// header out for a poll or two; the log itself has not moved. So while you are down the
		// score only has to stay above the hold level, and the match has to still be in the place
		// the log was found - noise elsewhere in the frame cannot keep you down.
		double hold = std::max(0.50, cfg.threshold - std::max(0.0, cfg.holdDrop));
		bool sameSpot = std::fabs(m.x - downX_) < 0.03f && std::fabs(m.y - downY_) < 0.03f;
		if (m.score >= cfg.threshold && sameSpot)
			fullSince_ = clock_::now();
		// ...but only as a bridge: a washout lasts a moment. If the log has not scored a clean
		// match for kHoldMs the hold lapses, so nothing on screen can pin you down for good.
		bool bridging = clock_::now() - fullSince_ < std::chrono::milliseconds(kHoldMs);
		match = sameSpot && m.score >= (bridging ? hold : cfg.threshold);
		// the fading-away rule stays for the revive case, where switching back a poll sooner shows
		if (match && revivingRecent() && m.score < peakScore_ - cfg.releaseDrop)
			match = false;
	}
	if (match) {
		downRun_++;
		upRun_ = 0;
	} else {
		upRun_++;
		downRun_ = 0;
	}
	bool fast = revivingRecent();
	int needUp = fast ? 1 : cfg.upFrames;
	int minDown = fast ? 0 : cfg.minDownMs;
	if (!detected_ && downRun_ >= cfg.downFrames) {
		detected_ = true;
		peakScore_ = m.score;
		downX_ = m.x;
		downY_ = m.y;
		fullSince_ = clock_::now();
		detGame_.holdThreshold = std::max(0.50, cfg.threshold - std::max(0.0, cfg.holdDrop));
		upDelay_.stop();
		askNearbyNow(); // fresh NEARBY reading while the delay runs
		pickClosest("downed", true);
		if (!applied_) {
			if (cfg.downDelayMs <= 0)
				applyNow(true, QString("downed screen detected (%1)").arg(m.score, 0, 'f', 3));
			else {
				downDelay_.start(
					cfg.downDelayMs); // cancelled if the log goes away first (a blip, or a quick revive)
				log(QString("Downed - showing the squad mate in %1 ms unless you are revived first.")
					    .arg(cfg.downDelayMs));
			}
		}
	} else if (detected_ && upRun_ >= needUp && clock_::now() - downSince_ >= std::chrono::milliseconds(minDown)) {
		detected_ = false;
		detGame_.holdThreshold = 0;
		downDelay_.stop();
		clearNearby();
		if (applied_) {
			if (fast || cfg.upDelayMs <= 0)
				applyNow(false, fast ? "revived (friend's revive seen, damage log gone)"
						     : QString("damage log gone (%1)").arg(m.score, 0, 'f', 3));
			else
				upDelay_.start(cfg.upDelayMs);
		} else
			log("Damage log gone before the delay ended - no switch.");
	}
}

// ----- actions -----

void Engine::applyNow(bool on, const QString &why)
{
	if (applying_)
		return;
	if (!cfg.active()) {
		log("Add a squad mate first.");
		return;
	}
	applying_ = true;
	auto errors = sw.apply(cfg, on);
	if (!on) {
		// your own POV takes priority when you are up: every squad mate and the look overlay go, in every scene
		int n = sw.hideAllFriends(cfg);
		if (n > 0)
			log(QString("Squad mate feeds hidden (%1 item%2).").arg(n).arg(n == 1 ? "" : "s"));
	}
	applied_ = on;
	lookPreview_ = false;
	if (on)
		downSince_ = clock_::now();
	else {
		detRevive_.unlock();
		if (!detected_)
			clearNearby();
	}
	if (dualOn_)
		sw.applyDual(cfg, !on); // the small window makes way for the full-screen swap, and returns
	sendPov(on ? "downed" : "up");
	events_ << QDateTime::currentDateTime().toString("HH:mm:ss") +
			   (on ? "  DOWNED - showing " + QString::fromStdString(cfg.active()->name) : "  back up");
	while (events_.size() > 30)
		events_.removeFirst();
	if (on && cfg.clipOnDowned)
		clips.request("downed", {"downed"}, "pov");
	QString msg = (on ? QString("Showing %1's POV").arg(QString::fromStdString(cfg.active()->name))
			  : QString("Back to your POV")) +
		      " - " + why + ".";
	if (!errors.empty()) {
		msg += "  Problems: ";
		for (size_t i = 0; i < errors.size(); i++)
			msg += QString::fromStdString(errors[i]) + (i + 1 < errors.size() ? "; " : "");
	}
	log(msg);
	applying_ = false;
	emit stateChanged();
}

void Engine::setDual(bool on, const QString &why)
{
	if (on && !cfg.dual()) {
		log("Dual POV: pick a squad mate on the Dual POV tab first.");
		return;
	}
	std::string e = sw.applyDual(cfg, on && !applied_);
	if (!e.empty()) {
		log("Dual POV: " + QString::fromStdString(e));
		if (on)
			return;
	}
	dualOn_ = on;
	pushAppConfig(); // ClipHound watches the vehicle corner while the window is up
	log((on ? "Dual POV on: " + QString::fromStdString(cfg.dual()->name) + " in the small window"
		: QString("Dual POV off")) +
	    " - " + why + ".");
	emit stateChanged();
}

/// ClipHound read the vehicle keybind list: a seat name, "vehicle" (in one, seat unclear) or "none".
void Engine::onVehicle(const QString &seat)
{
	vehicleSeat_ = seat;
	if (!cfg.dual())
		return;
	if (seat == "none") {
		// out of the vehicle: the window goes, however it was turned on - unless asked to stay
		if (dualOn_ && !cfg.dualKeep) {
			dualAutoOn_ = false;
			setDual(false, "out of the vehicle");
		}
		return;
	}
	if (!cfg.dualAuto)
		return; // turning it on by itself is the tick box's job
	if (seat != "vehicle" && seat.toStdString() != cfg.dualPreset) {
		// the seat the game shows wins over the preset chosen by hand
		struct P {
			const char *id;
			double x, y, w;
		};
		static const P presets[] = {{"tank-driver", 0.012, 0.19, 0.26},
					    {"tank-gunner", 0.012, 0.19, 0.26},
					    {"havoc-pilot", 0.012, 0.19, 0.26},
					    {"havoc-gunner", 0.19, 0.075, 0.20}};
		for (const auto &p : presets)
			if (seat == p.id) {
				cfg.dualPreset = p.id;
				cfg.dualX = p.x;
				cfg.dualY = p.y;
				cfg.dualW = p.w;
				cfg.save();
			}
	}
	if (!dualOn_ || seat != "vehicle") {
		dualAutoOn_ = true;
		setDual(true, "in a vehicle: " + seat);
	}
	emit stateChanged();
}

void Engine::toggleDual()
{
	setDual(!dualOn_, "hotkey");
}

void Engine::toggle()
{
	applyNow(!applied_, "hotkey");
}

void Engine::setActive(int idx)
{
	if (idx < 0 || idx >= (int)cfg.friends.size())
		return;
	bool wasOn = applied_;
	if (wasOn)
		applyNow(false, "switching squad mate");
	cfg.activeFriend = idx;
	cfg.save();
	if (wasOn)
		applyNow(true, QString("squad mate is now %1").arg(QString::fromStdString(cfg.friends[idx].name)));
	else if (cfg.keepWarm)
		sw.armWarm(cfg);
	log(QString("Active squad mate: %1.").arg(QString::fromStdString(cfg.friends[idx].name)));
	emit stateChanged();
}

void Engine::setEnabled(bool on)
{
	cfg.enabled = on;
	cfg.save();
	if (!on && applied_)
		applyNow(false, "paused");
	downRun_ = upRun_ = 0;
	detected_ = false;
	detGame_.holdThreshold = 0;
	log(on ? "Resumed." : "Paused - your own POV stays on.");
	emit stateChanged();
}

void Engine::captureTemplate()
{
	QImage img = lastFrame();
	if (img.isNull()) {
		log("No frame from the game source yet (open the settings window so frames are kept).");
		return;
	}
	int x = (int)std::lround(cfg.boxX * img.width()), y = (int)std::lround(cfg.boxY * img.height());
	int w = std::max(8, (int)std::lround(cfg.boxW * img.width())),
	    h = std::max(8, (int)std::lround(cfg.boxH * img.height()));
	x = std::clamp(x, 0, img.width() - 8);
	y = std::clamp(y, 0, img.height() - 8);
	w = std::min(w, img.width() - x);
	h = std::min(h, img.height() - y);
	std::vector<float> g((size_t)w * h);
	for (int yy = 0; yy < h; yy++)
		for (int xx = 0; xx < w; xx++) {
			QRgb p = img.pixel(x + xx, y + yy);
			g[(size_t)yy * w + xx] = 0.299f * qRed(p) + 0.587f * qGreen(p) + 0.114f * qBlue(p);
		}
	cfg.customTemplateWidthFrac = (double)w / img.width();
	detGame_.setTemplate(g, w, h, (float)cfg.customTemplateWidthFrac);
	std::string dir = Config::configDir();
	std::ofstream out(Config::configFile("template.bin"), std::ios::binary);
	out.write((const char *)&w, 4);
	out.write((const char *)&h, 4);
	out.write((const char *)g.data(), g.size() * sizeof(float));
	cfg.save();
	downRun_ = upRun_ = 0;
	log("Custom damage-log template captured from the box.");
	emit stateChanged();
}

void Engine::useBuiltInTemplate()
{
	cfg.customTemplateWidthFrac = 0;
	cfg.save();
	loadTemplates();
	log("Back to the built-in damage-log template.");
	emit stateChanged();
}

void Engine::previewLook(bool on)
{
	lookPreview_ = on && !applied_;
	std::string e = sw.updateLook(cfg, lookPreview_ || applied_);
	log(!e.empty() ? QString::fromStdString("Look: " + e)
		       : (lookPreview_ ? "Look overlay showing in OBS." : "Look overlay hidden."));
}
