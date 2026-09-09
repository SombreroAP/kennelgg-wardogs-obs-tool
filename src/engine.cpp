#include "engine.h"
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
#include <windows.h>
#endif
#include <QUrl>
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
	connect(&timer_, &QTimer::timeout, this, &Engine::tick);
	connect(&frameTimer_, &QTimer::timeout, this, &Engine::frameTick);
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
		std::string e = sw.startNdiShare(ndiShareName().toStdString());
		if (!e.empty())
			log("NDI share: " + QString::fromStdString(e));
	} else
		sw.stopNdiShare();
}

void Engine::launchApp()
{
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
			detGame_.loadTemplatePng(p, 262.0f / 1704.0f);
		bfree(p);
		cfg.customTemplateWidthFrac = 0;
	}
	char *r = obs_module_file("templates/reviving.png");
	if (r)
		detRevive_.loadTemplatePng(r, 98.0f / 1875.0f);
	bfree(r);
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
	bridge.close();
	lan.stop();
	sw.stopNdiShare();
	for (int i = 0; i < 50 && busy_; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void Engine::reloadConfig()
{
	clips.nameTemplate = QString::fromStdString(cfg.clipNameTemplate);
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
	if (!lastGame_.locked && !revivingRecent() && (tickN_ % 3) != 0)
		return;
	busy_ = true;
	bool wantRevive = applied_ && cfg.watchRevive && cfg.active();
	std::string gameName = cfg.gameSource, friendName = wantRevive ? Config::sourceFor(*cfg.active()) : "";
	bool preview = previewWanted_;

	std::thread([this, gameName, friendName, wantRevive, preview]() {
		Result r;
		obs_source_t *src = obs_get_source_by_name(gameName.c_str());
		if (src) {
			std::vector<uint8_t> bgra;
			int w, h, ls;
			if (capGame_.grab(src, Detector::FrameWidth, bgra, w, h, ls)) {
				Frame f = Detector::fromBGRA(bgra.data(), w, h, ls);
				r.game = detGame_.compare(f);
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
		if (wantRevive) {
			obs_source_t *fs = obs_get_source_by_name(friendName.c_str());
			if (fs) {
				std::vector<uint8_t> bgra;
				int w, h, ls;
				if (capFriend_.grab(fs, Detector::FrameWidth, bgra, w, h, ls)) {
					Frame f = Detector::fromBGRA(bgra.data(), w, h, ls);
					r.revive = detRevive_.compare(f);
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
		if (!applied_)
			applyNow(true, QString("downed screen detected (%1)").arg(m.score, 0, 'f', 3));
	} else if (detected_ && upRun_ >= needUp && clock_::now() - downSince_ >= std::chrono::milliseconds(minDown)) {
		detected_ = false;
		if (applied_)
			applyNow(false, fast ? "revived (friend's revive seen, damage log gone)"
					     : QString("damage log gone (%1)").arg(m.score, 0, 'f', 3));
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
		// the look must never outlive the swap, whatever scene we are in now
		int n = Switcher::hideEverywhere(Config::overlaySourceName());
		if (n > 0)
			log(QString("Look overlay hidden (%1 item%2).").arg(n).arg(n == 1 ? "" : "s"));
	}
	applied_ = on;
	lookPreview_ = false;
	if (on)
		downSince_ = clock_::now();
	else
		detRevive_.unlock();
	sendPov(on ? "downed" : "up");
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
