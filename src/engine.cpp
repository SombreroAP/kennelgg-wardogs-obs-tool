#include "engine.h"
#include <QSysInfo>
#include "discord-ipc.h"
#include <optional>
#include <obs-frontend-api.h>
#include <QNetworkInterface>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <thread>
#include <QBuffer>
#include <QJsonArray>
#include <QFileInfo>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QUrl>
#include "http.h"
#include <QDateTime>

#ifdef _WIN32
#define NOMINMAX // windows.h defines min and max as macros, which eats every std::min in this file
#include <windows.h>
#endif

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
	connect(&replayTimer_, &QTimer::timeout, this, &Engine::replayTick);
	connect(&healthTimer_, &QTimer::timeout, this, &Engine::sendObsHealth);
	sessionStart_ = QDateTime::currentDateTime();
	connect(&roster, &Roster::changed, this, &Engine::checkAccess);
	connect(&roster, &Roster::polled, this, &Engine::checkAccess);
	connect(&roster, &Roster::changed, this, &Engine::syncRoster);
	popoutTimer_.setInterval(2000);
	connect(&popoutTimer_, &QTimer::timeout, this, &Engine::watchPopouts);
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
	return cfg.playerName.empty() ? QSysInfo::machineHostName() : QString::fromStdString(cfg.playerName);
}

/// Put the clip length into OBS and, if the buffer is already running, restart it so the new length
/// takes - stop first, start a moment later. OBS's stop is not finished when the call returns, and
/// starting immediately leaves the buffer off, which means no clips at all until OBS is restarted.
/// ClipHound reads the bridge port from its own config.yaml, so changing it in the plugin used to
/// orphan the app for good: it went on knocking at the old port for ever while the dock said
/// "starting" and no clips or NEARBY readings arrived. Write the number into its config too.
void Engine::syncAppPort()
{
	if (cfg.appPath.empty())
		return;
	QString yaml = QFileInfo(QString::fromStdString(cfg.appPath)).absolutePath() + "/config.yaml";
	QFile f(yaml);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
	f.close();
	// the port under the "bridge:" block, left exactly as it is written otherwise
	bool inBridge = false, changed = false;
	static const QRegularExpression rxPort("^(\\s+port:\\s*)(\\d+)(.*)$");
	for (QString &l : lines) {
		if (!l.startsWith(' ') && !l.startsWith('\t'))
			inBridge = l.startsWith("bridge:");
		if (!inBridge)
			continue;
		QRegularExpressionMatch m = rxPort.match(l);
		if (m.hasMatch() && m.captured(2).toInt() != cfg.bridgePort) {
			l = m.captured(1) + QString::number(cfg.bridgePort) + m.captured(3);
			changed = true;
		}
	}
	if (!changed)
		return;
	QFile out(yaml);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
		log("Could not write ClipHound's config.yaml, so it still expects the old bridge port - put "
		    "the port back, or edit " +
		    yaml + " by hand.");
		return;
	}
	out.write(lines.join('\n').toUtf8());
	out.close();
	log(QString("ClipHound's port updated to %1; restarting it so it reconnects.").arg(cfg.bridgePort));
	if (cfg.launchApp) {
		stopApp();
		QTimer::singleShot(2000, this, [this]() {
			if (!stopping_ && cfg.launchApp)
				launchApp();
		});
	}
}

void Engine::applyReplaySeconds()
{
	if (!cfg.clipUseReplay)
		return;
	switch (clips.setReplaySeconds(cfg.replaySeconds)) {
	case Clips::ReplayChange::None:
		return;
	case Clips::ReplayChange::Written:
		log(QString("Clip length set to %1 s in OBS.").arg(cfg.replaySeconds));
		return;
	case Clips::ReplayChange::NeedsRestart:
		log(QString("Clip length set to %1 s - restarting OBS's replay buffer so it takes.")
			    .arg(cfg.replaySeconds));
		obs_frontend_replay_buffer_stop();
		QTimer::singleShot(2500, this, [this]() {
			if (stopping_ || obs_frontend_replay_buffer_active())
				return;
			obs_frontend_replay_buffer_start();
			QTimer::singleShot(1500, this, [this]() {
				if (stopping_)
					return;
				log(obs_frontend_replay_buffer_active()
					    ? "Replay buffer is running again."
					    : "The replay buffer did not come back after the length change - start it "
					      "in OBS (Settings -> Output -> Replay Buffer), or clips cannot save.");
				emit stateChanged();
			});
		});
		return;
	}
}

void Engine::launchApp()
{
	if (bridge.clients() > 0) {
		log("ClipHound is already running.");
		return;
	}
	QString p = QString::fromStdString(cfg.appPath);
	const QString def = "C:/ProgramData/Kennel.gg/ClipHound/ClipHound.exe";
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
		QTimer::singleShot(25000, this, [this]() {
			if (stopping_ || bridge.clients() > 0 || !appRunning())
				return;
			// alive but never said hello: its own log is the only thing that knows why
			log("ClipHound has been starting for 25 s without connecting. The last lines of its log:");
			for (const QString &l : appLogTail(12))
				log("  " + l);
			log("If those lines say nothing useful, check that no older copy of this plugin is "
			    "installed (C:\\ProgramData\\obs-studio\\plugins\\kennel-wardogs).");
			emit stateChanged();
		});
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
	// Once: the sound of YOUR game goes on the mute list, so a squad mate's POV comes with their
	// sound and not yours on top. The game source when it carries audio (a capture card does),
	// otherwise every desktop-audio input. Microphones are never touched. Cleared by hand, it stays
	// cleared: the flag is saved.
	if (cfg.audioAutoPicked || cfg.gameSource.empty())
		return;
	auto listed = [this](const std::string &n) {
		return std::find(cfg.muteWhileDowned.begin(), cfg.muteWhileDowned.end(), n) !=
		       cfg.muteWhileDowned.end();
	};
	QStringList picked;
	obs_source_t *game = obs_get_source_by_name(cfg.gameSource.c_str());
	if (!game)
		return; // not in this collection yet: try again next time
	bool gameAudio = (obs_source_get_output_flags(game) & OBS_SOURCE_AUDIO) != 0;
	obs_source_release(game);
	if (gameAudio) {
		if (!listed(cfg.gameSource)) {
			cfg.muteWhileDowned.push_back(cfg.gameSource);
			picked << QString::fromStdString(cfg.gameSource);
		}
	} else {
		for (const auto &in : Switcher::inputs())
			if (in.second == "wasapi_output_capture" && !listed(in.first)) {
				cfg.muteWhileDowned.push_back(in.first);
				picked << QString::fromStdString(in.first);
			}
	}
	cfg.audioAutoPicked = true;
	cfg.save();
	if (!picked.isEmpty())
		log("While a squad mate is on screen, " + picked.join(", ") +
		    " is muted so their sound plays instead of yours (Settings, Switch, to change).");
}

void Engine::start()
{
	autoPickAudio();
	// the Discord app knows who you are; a moment after start, so OBS is up first
	QTimer::singleShot(3000, this, [this]() {
		if (!stopping_)
			detectDiscordUser(false);
	});
	if (cfg.appPath.empty()) {
		// the installer puts ClipHound here; adopt it once so the app starts with OBS
		QString def = "C:/ProgramData/Kennel.gg/ClipHound/ClipHound.exe";
		if (QFileInfo::exists(def)) {
			cfg.appPath = def.toStdString();
			cfg.launchApp = true;
			cfg.save();
			log("Found ClipHound from the installer; it will start with OBS (Settings → Clips).");
		}
	}
	clips.nameTemplate = QString::fromStdString(cfg.clipNameTemplate);
	clips.seriesWindowS = cfg.clipSeriesS;
	clips.folder = QString::fromStdString(cfg.clipFolder);
	clips.watchFolders = cfg.backtrackFolder.empty() ? QStringList()
							 : QStringList{QString::fromStdString(cfg.backtrackFolder)};
	clips.autoStartReplay = cfg.autoStartReplay && cfg.clipUseReplay;
	clips.useReplay = cfg.clipUseReplay;
	clips.hotkeys.clear();
	for (auto &h : cfg.clipHotkeys)
		clips.hotkeys << QString::fromStdString(h);
	if (cfg.bridgeEnabled)
		if (!bridge.listen((quint16)cfg.bridgePort))
			log(QString("ClipHound's bridge could NOT open port %1 - something else is already on it, "
				    "usually an older copy of this plugin still installed. ClipHound will sit at "
				    "\"starting\" and no clips will fire until that is sorted: check for "
				    "C:\\ProgramData\\obs-studio\\plugins\\kennel-wardogs and delete it, then "
				    "restart OBS.")
				    .arg(cfg.bridgePort));
	applyReplaySeconds();
	if (cfg.autoStartReplay && cfg.clipUseReplay)
		clips.ensureReplayBuffer();
	if (cfg.launchApp)
		launchApp();
	sw.migrateNames(cfg); // sources a build before 0.7.0 made, under their old names
	if (!cfg.discordShared1) {
		// 0.7.5 gave every squad mate watching the Discord call their own capture of the same
		// window. Fold them into the one shared capture; a pop-out gets its own again when it appears.
		int folded = 0;
		for (auto &f : cfg.friends) {
			if (!f.sharesDiscordCall() || f.source == Friend::discordCallSourceName())
				continue;
			sw.removeFriendSources(cfg, f);
			f.source.clear();
			f.audioSource.clear();
			std::string e = sw.createFriendSources(cfg, f);
			if (!e.empty())
				log("Could not remake " + QString::fromStdString(f.name) +
				    "'s Discord capture: " + QString::fromStdString(e));
			else
				folded++;
		}
		cfg.discordShared1 = true;
		cfg.save();
		if (folded)
			log(QString("Squad mates watching the Discord call now share one capture (%1 moved over).")
				    .arg(folded));
	}
	if (!cfg.discordAudio1) {
		int n = sw.removeDiscordAudio(cfg);
		cfg.discordAudio1 = true;
		cfg.save();
		if (n)
			log(QString("The plugin no longer captures Discord's sound (%1 audio capture%2 removed). Discord hands "
				    "OBS one mix for the whole call, so it comes through whatever already carries Discord on "
				    "your stream.")
				    .arg(n)
				    .arg(n == 1 ? "" : "s"));
	}
	armPopoutWatch();
	for (const auto &f : cfg.friends)
		if (f.kind == FriendKind::Discord && !f.handle.empty() &&
		    QString::fromStdString(f.handle).compare(QString::fromStdString(f.name), Qt::CaseInsensitive) != 0)
			log("Squad: the slot named " + QString::fromStdString(f.name) + " is set to " +
			    QString::fromStdString(f.handle) +
			    "'s stream. If that is not who it should show, remove it and add them again with Add.");
#ifdef _WIN32
	// Two copies of this plugin both load, and the second one gets no bridge port: ClipHound then
	// connects to the wrong one and everything looks like it is "starting" for ever.
	if (QFileInfo::exists("C:/ProgramData/obs-studio/plugins/kennel-wardogs"))
		log("An older copy of this plugin is still installed at "
		    "C:\\ProgramData\\obs-studio\\plugins\\kennel-wardogs. Close OBS, delete that folder, "
		    "and start OBS again - with both installed they fight over ClipHound's bridge and clips "
		    "never fire.");
#endif
	applyRosterConfig();
	sw.stopMedia(cfg); // the replay source forgets last session's file (it was decoding it at load)
	timer_.start(std::max(100, cfg.pollMs));
	healthTimer_.start(5000);
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
	set["series_window"] = cfg.clipSeriesS; // Twitch titles get "part 2", "part 3" inside this
	set["clip_trim"] = cfg.clipTrim;
	set["clip_trim_lead_s"] = cfg.clipTrimLeadS;
	set["run_merge"] = cfg.runMerge;
	set["run_cut_gaps"] = cfg.runCutGaps;
	set["run_gap_s"] = cfg.runGapS;
	set["replay_chat"] = cfg.replayChat; // "!replay" in chat plays the last highlight
	set["replay_cooldown_s"] = cfg.replayCooldownS;
	set["replay_word"] = QString::fromStdString(cfg.replayWord.empty() ? "!replay" : cfg.replayWord);
	set["chat_kick"] = QString::fromStdString(cfg.chatKick);
	set["chat_youtube"] = QString::fromStdString(cfg.chatYouTube);
	set["fps"] = cfg.appFps > 0 ? cfg.appFps : 10;
	QJsonObject nb;
	nb["enabled"] = cfg.nearEnabled;
	nb["roi"] = QJsonArray{cfg.nearX, cfg.nearY, cfg.nearW, cfg.nearH};
	QJsonArray names;
	for (const auto &f : cfg.friends)
		if (!f.nearName().empty())
			names.append(QString::fromStdString(f.nearName()));
	// their Discord username as well, when it differs: the in-game name is often a guess made from
	// it, and ClipHound's matcher is fuzzy, so either spelling finds them
	for (const auto &f : cfg.friends)
		if (!f.handle.empty() && QString::fromStdString(f.handle).compare(QString::fromStdString(f.nearName()),
										  Qt::CaseInsensitive) != 0)
			names.append(QString::fromStdString(f.handle));
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
	updateState_ = "checking...";
	emit updateChecked();
	Http::getAsync(this, url, 8000, QString("KennelggWardogsOBSTool/%1").arg(PLUGIN_VERSION),
		       [this, manual](Http::Result r) {
			       if (!r.ok) {
				       updateState_ = "could not check (" + r.error + ")";
				       if (manual)
					       log("Update check: " + updateState_);
				       emit updateChecked();
				       return;
			       }
			       QJsonObject o = QJsonDocument::fromJson(r.body).object();
			       newVersion_ = o.value("version").toString();
			       newUrl_ = o.value("url").toString();
			       newNotes_ = o.value("notes").toString();
			       if (newVersion_.isEmpty())
				       updateState_ = "nothing published to check against yet";
			       else if (isNewer(newVersion_, PLUGIN_VERSION)) {
				       updateState_ =
					       newVersion_ + " is out (you have " + QString(PLUGIN_VERSION) + ")";
				       log("A newer build is out: " + newVersion_ +
					   (newNotes_.isEmpty() ? "" : " - " + newNotes_) +
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

/// The last few lines of ClipHound's own log, for when it starts but never says hello.
QStringList Engine::appLogTail(int lines) const
{
	QString dir = cfg.appPath.empty() ? QString("C:/ProgramData/Kennel.gg/ClipHound")
					  : QFileInfo(QString::fromStdString(cfg.appPath)).absolutePath();
	QFile f(dir + "/cliphound.log");
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return {"(no cliphound.log at " + dir + " - it may not have got far enough to write one)"};
	QStringList all = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
	f.close();
	return all.mid(std::max(0, (int)all.size() - lines));
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
	stopReplay("OBS closing");
	sw.shutdown(); // the dual-POV scene and its browser page, before obs-browser unloads
	for (int i = 0; i < 50 && busy_; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void Engine::applyRosterConfig()
{
	if (!cfg.rosterEnabled || cfg.rosterUrl.empty()) {
		roster.stop();
		return;
	}
	roster.configure(QString::fromStdString(cfg.rosterUrl), cfg.rosterPollS,
			 QString::fromStdString(cfg.rosterChannel), QString::fromStdString(cfg.rosterGuild));
}

Engine::Access Engine::rosterAccess() const
{
	if (!roster.running() || !roster.healthy() || !roster.membersKnown())
		return Access::Unknown; // nothing polled yet, or the address cannot be read
	if (roster.homeGuild().compare(Config::kennelHomeGuild(), Qt::CaseInsensitive) != 0)
		return Access::NotMember; // not the Kennel.gg bot
	if (cfg.myDiscord.empty())
		return Access::NoUsername;
	return roster.isMember(QString::fromStdString(cfg.myDiscord)) ? Access::Ok : Access::NotMember;
}

bool Engine::rosterLive() const
{
	if (!cfg.rosterEnabled || !roster.running() || !roster.healthy())
		return false;
	Access a = rosterAccess();
	return a != Access::NotMember && a != Access::NoUsername;
}

bool Engine::rosterOpen() const
{
	Access a = rosterAccess();
	return a == Access::Ok || a == Access::Unknown;
}

QString Engine::rosterStatus() const
{
	switch (rosterAccess()) {
	case Access::NoUsername:
		return "locked - enter your Discord username (Setup) so the bot can see you are in the Kennel.gg Discord";
	case Access::NotMember:
		return "locked - \"" + QString::fromStdString(cfg.myDiscord) +
		       "\" is not in the Kennel.gg Discord. Join it (the Discord button on the dock), and check the "
		       "username is the lower-case one under your display name";
	default:
		return roster.status();
	}
}

QString Engine::discordUrl() const
{
	return roster.joinUrl().isEmpty() ? QString(Config::kennelDiscordUrl()) : roster.joinUrl();
}

void Engine::setMyDiscord(const QString &user)
{
	cfg.myDiscord = user.trimmed().toLower().toStdString();
	cfg.rosterEnabled = true;
	cfg.save();
	applyRosterConfig();
	roster.poll();
	log(cfg.myDiscord.empty() ? QString("Discord username cleared.")
				  : QString("Discord username set to %1; checking the Kennel.gg Discord for it.")
					    .arg(QString::fromStdString(cfg.myDiscord)));
	emit stateChanged();
}

void Engine::detectDiscordUser(bool byHand)
{
	std::thread([this, byHand]() {
		std::optional<DiscordIpc::User> u = DiscordIpc::currentUser(1500);
		QString name = u ? u->username.trimmed().toLower() : QString();
		QMetaObject::invokeMethod(
			this,
			[this, name, byHand]() {
				if (stopping_)
					return;
				QString mine = QString::fromStdString(cfg.myDiscord).toLower();
				if (name.isEmpty()) {
					if (byHand)
						log("Could not ask Discord who you are: is the Discord desktop app running on "
						    "this PC and logged in?");
				} else if (mine.isEmpty() || byHand) {
					if (mine != name) {
						log("Discord is logged in as " + name +
						    ": taken as your Discord username.");
						setMyDiscord(name);
					} else if (byHand)
						log("Discord confirms your username: " + name + ".");
				} else if (mine != name)
					log("Discord on this PC is logged in as " + name +
					    ", but the plugin was given " + mine +
					    ". Detect (Setup, or the dock) switches to " + name + ".");
				emit discordUserDetected(name, byHand);
			},
			Qt::QueuedConnection);
	}).detach();
}

void Engine::checkAccess()
{
	// the roster's own state, said once per change, so the log shows what the dock is working from
	QString rs = roster.status();
	if (cfg.rosterEnabled && rs != lastRosterStatus_) {
		lastRosterStatus_ = rs;
		log("Discord voice: " + rs + ".");
	}
	Access a = rosterAccess();
	if (a == lastAccess_)
		return;
	Access was = lastAccess_;
	lastAccess_ = a;
	if (a == Access::NotMember || a == Access::NoUsername) {
		log("Squad automation is " + rosterStatus() + ".");
		addEvent("squad automation locked - join the Kennel.gg Discord");
	} else if (a == Access::Ok && was != Access::Unknown) {
		log("Squad automation unlocked: " + QString::fromStdString(cfg.myDiscord) +
		    " is in the Kennel.gg Discord.");
		syncRoster();
	}
	emit stateChanged();
}

void Engine::syncRoster()
{
	if (!rosterLive())
		return;
	// Only the people actually sharing get a slot. Everyone in the call would mean a window
	// capture each for feeds that do not exist, and Discord puts every share inside the one
	// window anyway - a slot for somebody who is not live could never show anything.
	QList<Roster::Member> live = roster.streamers();
	// "with you": when your Discord username is known, only the channel you are sitting in counts
	if (!cfg.myDiscord.empty()) {
		QString me = QString::fromStdString(cfg.myDiscord).toLower(), myChan;
		for (const auto &m : roster.members())
			if (m.handle.toLower() == me)
				myChan = m.channel;
		QList<Roster::Member> here;
		for (const auto &m : live)
			if (!myChan.isEmpty() && m.channel == myChan && m.handle.toLower() != me)
				here.append(m);
		live = here;
	}
	auto same = [](const Roster::Member &m, const Friend &f) {
		QString h = m.handle.toLower(), n = m.name.toLower();
		QString fh = QString::fromStdString(f.handle).toLower(), fn = QString::fromStdString(f.name).toLower();
		return (!h.isEmpty() && (h == fh || h == fn)) || n == fn;
	};
	QString activeName = (cfg.activeFriend >= 0 && cfg.activeFriend < (int)cfg.friends.size())
				     ? QString::fromStdString(cfg.friends[cfg.activeFriend].name)
				     : QString();
	bool changed = false;

	// gone: they stopped sharing or left the call
	for (size_t i = cfg.friends.size(); i-- > 0;) {
		Friend &f = cfg.friends[i];
		if (!f.fromRoster)
			continue; // yours, not ours
		bool still = false;
		for (const auto &m : live)
			if (same(m, f))
				still = true;
		if (still)
			continue;
		if (applied_ && (int)i == cfg.activeFriend)
			applyNow(false, "their Discord share ended");
		sw.removeFriendSources(cfg, f);
		log("Squad: " + QString::fromStdString(f.name) + " stopped sharing - slot removed.");
		cfg.friends.erase(cfg.friends.begin() + (long)i);
		changed = true;
	}

	// new: somebody went live in the call
	for (const auto &m : live) {
		bool known = false;
		for (auto &f : cfg.friends)
			if (same(m, f)) {
				known = true;
				if (f.handle.empty() && !m.handle.isEmpty()) {
					f.handle = m.handle.toStdString(); // an older slot learns the username
					changed = true;
				}
			}
		if (known)
			continue; // already there, by hand, from a pop-out, or from an earlier poll
		if (isMe(m.handle))
			continue;
		Friend f;
		// the slot is called what Discord calls them (the username, not "Private Gazreyn"): it is
		// what a pop-out is titled with and what their in-game name is matched against
		f.name = (m.handle.isEmpty() ? m.name : m.handle).toStdString();
		f.handle = m.handle.toStdString();
		f.kind = FriendKind::Discord;
		// matched by executable, so it follows the Go Live window whether or not it is popped out
		f.channel = "Discord:Chrome_WidgetWin_1:Discord.exe";
		f.fromRoster = true;
		if (cfg.rosterAddSources) {
			std::string e = sw.createFriendSources(cfg, f);
			if (!e.empty()) {
				log("Squad: " + m.name +
				    " went live in Discord, but the capture could not be "
				    "made: " +
				    QString::fromStdString(e));
				continue;
			}
		}
		cfg.friends.push_back(f);
		changed = true;
		log("Squad: " + QString::fromStdString(f.name) +
		    " is sharing in Discord voice - slot added. Discord's sound is not handled: Discord hands OBS one mix for the whole call, so it comes through whatever already carries Discord on your stream, and your own game sound stays up while they are shown.");
	}

	if (!changed)
		return;
	// the list moved under it; keep pointing at the same person rather than at whoever slid into
	// that position
	cfg.activeFriend = 0;
	for (size_t i = 0; i < cfg.friends.size(); ++i)
		if (QString::fromStdString(cfg.friends[i].name) == activeName)
			cfg.activeFriend = (int)i;
	cfg.save();
	if (cfg.keepWarm && !applied_)
		sw.armWarm(cfg);
	armPopoutWatch();
	emit stateChanged();
}

/// Is this Discord window somebody's stream, popped out? Discord titles those "<username>'s
/// Stream", and "Discord Popout" for the second before it has drawn. The whole call popped out is
/// titled with the channel name ("General VC"), a camera tile with the bare username: neither is
/// a stream, and neither is ever captured.
static bool isStreamPopout(const std::string &title)
{
	QString t = QString::fromStdString(title).trimmed();
	if (t.compare("Discord Popout", Qt::CaseInsensitive) == 0)
		return true;
	static const QRegularExpression suffix(QStringLiteral("(?:['\u2019\u2018]s?)\\s*stream\\s*$"),
					       QRegularExpression::CaseInsensitiveOption);
	return suffix.match(t).hasMatch();
}

/// Whose window a Discord pop-out is. A Go Live pop-out is titled "<username>'s Stream" (seen
/// on a real PC: "sombrero's Stream"). Lower case.
static QString popoutOwner(const std::string &title)
{
	QString t = QString::fromStdString(title).toLower().trimmed();
	// "<username>'s Stream", with whichever apostrophe Discord's font hands out, and "<name>' Stream"
	// for a name ending in s; whatever is left in front of that is the owner
	static const QRegularExpression suffix(QStringLiteral("\\s*(?:['\u2019\u2018]s?)?\\s*stream\\s*$"));
	QRegularExpressionMatch m = suffix.match(t);
	if (m.hasMatch() && m.capturedStart() > 0)
		t = t.left(m.capturedStart());
	return t.trimmed();
}

bool Engine::isMe(const QString &discordUser) const
{
	QString u = discordUser.toLower();
	if (u.isEmpty())
		return false;
	if (!cfg.myDiscord.empty() && u == QString::fromStdString(cfg.myDiscord).toLower())
		return true;
	if (!cfg.playerName.empty() && u == QString::fromStdString(cfg.playerName).toLower())
		return true;
	QString tw = twitch_.value("login").toString().toLower();
	return !tw.isEmpty() && u == tw;
}

QString Engine::addPopouts(QStringList *addedOut)
{
	std::vector<Switcher::Popout> wins = Switcher::discordPopouts();
	QStringList added, already, failed;
	int unnamed = 0, mine = 0;
	for (const auto &w : wins) {
		if (!isStreamPopout(w.title))
			continue; // the whole call popped out, a camera tile: not a stream, never captured
		QString owner = popoutOwner(w.title);
		if (owner == "discord popout") {
			unnamed++; // Discord has not titled it yet; a second later it will have
			continue;
		}
		if (isMe(owner)) {
			mine++;
			continue;
		}
		bool known = false;
		for (const auto &f : cfg.friends)
			if (owner == QString::fromStdString(f.handle).toLower() ||
			    owner == QString::fromStdString(f.name).toLower())
				known = true;
		if (known) {
			already << owner;
			continue;
		}
		Friend f;
		f.name = owner.toStdString();   // the slot is called what Discord calls them...
		f.handle = owner.toStdString(); // ...and that is also what the game's NEARBY list is matched on
		f.kind = FriendKind::Discord;
		f.channel = Friend::anyDiscordWindow(); // the Discord window is where it goes back to
		std::string err = sw.createFriendSources(cfg, f);
		if (err.empty())
			err = sw.bindPopout(cfg, f, w); // and their own window, by exact title, is what it shows
		if (!err.empty()) {
			failed << owner + " (" + QString::fromStdString(err) + ")";
			continue;
		}
		cfg.friends.push_back(f);
		added << owner;
		if (addedOut)
			*addedOut << owner;
		log("Squad: added " + owner + " from their popped-out Discord stream (\"" +
		    QString::fromStdString(w.title) + "\").");
	}
	if (!added.isEmpty()) {
		if (cfg.friends.size() == added.size())
			cfg.activeFriend = 0;
		cfg.save();
		if (cfg.keepWarm && !applied_)
			sw.armWarm(cfg);
		armPopoutWatch();
		emit stateChanged();
	}
	QStringList out;
	if (!added.isEmpty())
		out << "Added " + added.join(", ") +
				". Mute each of their streams in Discord (right-click the stream, Mute): Discord hands OBS one "
				"mix for the whole call, so an unmuted stream's game sound plays in your headphones and goes out "
				"on your stream through Desktop Audio the whole time. The plugin does not handle it.";
	if (!already.isEmpty())
		out << already.join(", ") + (already.size() == 1 ? " is" : " are") + " already in the squad.";
	if (!failed.isEmpty())
		out << "Could not add " + failed.join("; ") + ".";
	if (unnamed)
		out << QString("%1 pop-out%2 not titled yet - give Discord a second and press Add again.")
				.arg(unnamed)
				.arg(unnamed == 1 ? " is" : "s are");
	if (mine)
		out << "Your own stream is popped out; it is not added.";
	if (out.isEmpty())
		out << "No popped-out Discord stream found. In Discord, right-click a squad mate's stream and "
		       "choose Pop Out, mute the stream (right-click it again), then press Add.";
	// nothing was added: say exactly which Discord windows were seen, so a title that does not look
	// the way this expects can be read straight off the panel
	if (added.isEmpty()) {
		QStringList seen;
		for (const auto &w : wins)
			seen << "\"" + QString::fromStdString(w.title) + "\"";
		out << (seen.isEmpty() ? QString("No Discord window other than the main one is open.")
				       : "Discord windows seen: " + seen.join(", ") + ".");
	}
	log("Squad: Add - " + out.join(" "));
	return out.join(" ");
}

void Engine::releasePopout(const Friend &f)
{
	if (!f.onPopout())
		return;
	for (const auto &p : Switcher::discordPopouts())
		if (p.window == f.popout)
			Switcher::untuckPopout(p);
}

void Engine::releaseAllPopouts()
{
	for (const auto &f : cfg.friends)
		releasePopout(f);
}

void Engine::showPopouts(bool show)
{
	popoutsShown_ = show;
	if (show) {
		releaseAllPopouts();
		log("Squad: pop-outs brought back on screen. They can go black while covered until you tuck "
		    "them again.");
	} else {
		log("Squad: pop-outs tucked away again.");
		watchPopouts();
	}
	emit stateChanged();
}

void Engine::armPopoutWatch()
{
	int n = 0;
	for (const auto &f : cfg.friends)
		if (f.kind == FriendKind::Discord)
			n++;
	if (n && !popoutTimer_.isActive()) {
		popoutTimer_.start();
		log(QString("Pop-out watch on for %1 Discord squad mate%2: pop a share out of Discord and their slot "
			    "takes that window by itself.")
			    .arg(n)
			    .arg(n == 1 ? "" : "s"));
		watchPopouts();
	} else if (!n && popoutTimer_.isActive()) {
		popoutTimer_.stop();
		log("Pop-out watch off: no Discord squad mates.");
	}
}

static const int kPopoutGraceMs = 20000; // a pop-out has to be gone this long before its slot lets go

void Engine::watchPopouts()
{
	if (stopping_)
		return;
	std::vector<Switcher::Popout> wins;
	for (const auto &p : Switcher::discordPopouts())
		if (isStreamPopout(p.title))
			wins.push_back(p); // the call view or a camera tile popped out is not a stream
	auto lower = [](const std::string &s) {
		return QString::fromStdString(s).toLower();
	};
	std::vector<bool> taken(wins.size(), false);
	bool changed = false;
	int parked = 0;               // stacking order on the parking monitor, or down the tucked edge
	int bound = (int)wins.size(); // how many pop-outs there are to place, for the spacing
	const Friend *active = cfg.active();
	std::string activeName = active ? active->name : "";
	int liveShared = 0; // Discord squad mates who are not on a pop-out yet
	for (const auto &f : cfg.friends)
		if (f.kind == FriendKind::Discord && !f.onPopout())
			liveShared++;

	// 1. everyone: is their window still there, or is there one for them now
	for (auto &f : cfg.friends) {
		if (f.kind != FriendKind::Discord)
			continue;
		QString name = lower(f.name), handle = lower(f.handle);
		int hit = -1;
		// exact owner first, so "bryan" can never take "bryanx's Stream"
		for (size_t i = 0; i < wins.size() && hit < 0; ++i) {
			if (taken[i])
				continue;
			QString owner = popoutOwner(wins[i].title);
			if (owner == "discord popout") // not drawn yet, so not named yet
				continue;
			if ((!handle.isEmpty() && owner == handle) || owner == name)
				hit = (int)i;
		}
		// then a looser look for slots named by hand: the slot name inside the owner's username
		for (size_t i = 0; hit < 0 && i < wins.size(); ++i) {
			if (taken[i])
				continue;
			QString owner = popoutOwner(wins[i].title);
			if (owner == "discord popout")
				continue;
			if (name.size() >= 4 && owner.contains(name))
				hit = (int)i;
		}
		// the one pop-out that has no name yet: if exactly one of the squad is on the shared call
		// it can only be theirs. Anything more ambiguous waits for Discord to title it.
		if (hit < 0 && liveShared == 1 && !f.onPopout()) {
			int unnamed = -1, count = 0;
			for (size_t i = 0; i < wins.size(); ++i)
				if (!taken[i] && lower(wins[i].title) == "discord popout") {
					unnamed = (int)i;
					count++;
				}
			if (count == 1)
				hit = unnamed;
		}
		if (hit >= 0) {
			taken[hit] = true;
			f.popoutMissingMs = 0;
			if (!f.onPopout()) {
				std::string e = sw.bindPopout(cfg, f, wins[hit]);
				if (!e.empty()) {
					log("Squad: found " + QString::fromStdString(f.name) +
					    "'s pop-out but could not capture it: " + QString::fromStdString(e));
					continue;
				}
				log("Squad: " + QString::fromStdString(f.name) + "'s share is popped out (\"" +
				    QString::fromStdString(wins[hit].title) + "\") - showing that window for them.");
				changed = true;
				if (applied_ && f.name == activeName) {
					if (!f.baseSource.empty())
						Switcher::hideEverywhere(f.baseSource);
					applyNow(true, "their pop-out appeared");
				}
			}
			if (cfg.popoutTuck && !popoutsShown_ && !wins[hit].minimized) {
				if (cfg.popoutMonitor >= 0) {
					if (Switcher::parkPopout(wins[hit], cfg.popoutMonitor, parked++, bound))
						log("Squad: " + QString::fromStdString(f.name) +
						    QString("'s pop-out parked on monitor %1, on top and fully visible, so "
							    "Discord keeps drawing it and its controls stay in reach.")
							    .arg(cfg.popoutMonitor + 1));
				} else if (Switcher::tuckPopout(wins[hit], parked++))
					log("Squad: " + QString::fromStdString(f.name) +
					    "'s pop-out pinned on top and tucked to the right edge of its screen, so "
					    "Discord keeps drawing it while other windows cover it. Press Show pop-outs "
					    "on the Squad panel to reach its controls.");
			}
			QString minKey = QString::fromStdString(f.name) + "/min";
			if (wins[hit].minimized && popoutNote_ != minKey) {
				popoutNote_ = minKey;
				log("Squad: " + QString::fromStdString(f.name) +
				    "'s pop-out is minimised, so its picture is frozen - restore the window (it can "
				    "sit behind the game, just not minimised).");
			}
		} else if (f.onPopout()) {
			// gone this tick. A stream that hiccups, a pop-out Discord redraws, a title that is
			// blank for a second: none of that is "closed". Give it a while before deciding.
			f.popoutMissingMs += popoutTimer_.interval();
			if (f.popoutMissingMs < kPopoutGraceMs)
				continue;
			sw.unbindPopout(cfg, f);
			log("Squad: " + QString::fromStdString(f.name) +
			    "'s pop-out has been gone for a while - back to the Discord window for them. Pop "
			    "it out again and the slot takes it straight back.");
			changed = true;
			if (applied_ && f.name == activeName)
				applyNow(true, "their pop-out closed");
		}
	}

	// 2. a named pop-out nobody matched: say whose it is, once. Your own stream lands here too.
	for (size_t i = 0; i < wins.size(); ++i) {
		if (taken[i])
			continue;
		QString t = QString::fromStdString(wins[i].title);
		if (t.compare("Discord Popout", Qt::CaseInsensitive) == 0)
			continue;
		if (popoutNote_ != t) {
			popoutNote_ = t;
			QString owner = popoutOwner(wins[i].title);
			bool me = !cfg.playerName.empty() && owner == lower(cfg.playerName);
			log("Squad: a pop-out of Discord user '" + owner + "' is open (\"" + t + "\")" +
			    (me ? ", which is you, so no slot takes it."
				: ", but no slot is named that. Name their slot with their Discord username, or turn "
				  "on Squad from Discord and it fills itself in."));
		}
	}
	if (changed) {
		cfg.save();
		if (cfg.keepWarm && !applied_)
			sw.armWarm(cfg);
		emit stateChanged();
	}
}

void Engine::reloadConfig()
{
	autoPickAudio(); // the game source may have just been chosen
	clips.nameTemplate = QString::fromStdString(cfg.clipNameTemplate);
	clips.seriesWindowS = cfg.clipSeriesS;
	clips.folder = QString::fromStdString(cfg.clipFolder);
	clips.watchFolders = cfg.backtrackFolder.empty() ? QStringList()
							 : QStringList{QString::fromStdString(cfg.backtrackFolder)};
	clips.autoStartReplay = cfg.autoStartReplay && cfg.clipUseReplay;
	clips.useReplay = cfg.clipUseReplay;
	clips.hotkeys.clear();
	for (auto &h : cfg.clipHotkeys)
		clips.hotkeys << QString::fromStdString(h);
	if (cfg.bridgeEnabled && (!bridge.listening() || bridge.port() != cfg.bridgePort)) {
		syncAppPort(); // ClipHound has to be told, or it knocks at the old port for ever
		if (!bridge.listen((quint16)cfg.bridgePort))
			log(QString("ClipHound's bridge could not open port %1 (something else has it).")
				    .arg(cfg.bridgePort));
	} else if (!cfg.bridgeEnabled && bridge.listening())
		bridge.close();
	applyRosterConfig();
	armPopoutWatch();
	applyReplaySeconds();
	detGame_.threshold = cfg.threshold;
	detRevive_.threshold = cfg.reviveThreshold;
	detGame_.unlock();
	timer_.setInterval(std::max(100, cfg.pollMs));
	if (cfg.keepWarm && !applied_ && cfg.active())
		sw.armWarm(cfg);
	sw.raiseOnTop(cfg);                 // the camera and alerts list may have just changed
	sw.applyFriendAudio(cfg, applied_); // the Switch tab's tick box takes effect on the spot
	pushAppConfig();                    // areas, names and rules the app reads
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
		return "Auto switch off - your own POV";
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
		QList<double> moments;
		for (auto v : o.value("moments").toArray())
			moments << v.toDouble();
		QJsonObject info = o.value("info").toObject();
		QString err = clips.request(o.value("title").toString(), tags, o.value("source").toString("app"),
					    moments, info);
		QJsonObject r;
		r["type"] = "clip_result";
		r["ok"] = err.isEmpty();
		r["error"] = err;
		r["id"] = o.value("id");
		bridge.sendJson(r);
		if (!err.isEmpty())
			log("Clip request: " + err);
	} else if (type == "highlights_status") {
		appStatus_ = o.value("text").toString();
		emit stateChanged();
	} else if (type == "highlights_ready") {
		highlightsBuilding_ = false;
		if (o.value("ok").toBool()) {
			QString path = o.value("path").toString();
			log(QString("Highlights ready: %1 (%2 clips).")
				    .arg(QFileInfo(path).fileName())
				    .arg(o.value("clips").toInt()));
			addEvent(QDateTime::currentDateTime().toString("HH:mm:ss") + "  HIGHLIGHTS READY " +
				 QFileInfo(path).fileName());
			if (highlightsThenPlay_) {
				highlightsThenPlay_ = false;
				playCompilation("built");
			}
		} else
			log("Highlights: could not build - " + o.value("error").toString() + ".");
		emit stateChanged();
	} else if (type == "replay") {
		QString who = o.value("who").toString("chat");
		QString err = chatReplay(who);
		QJsonObject r;
		r["type"] = "replay_result";
		r["ok"] = err.isEmpty();
		r["error"] = err;
		r["who"] = who;
		bridge.sendJson(r);
		if (!err.isEmpty())
			log("Chat replay from " + who + " not played: " + err + ".");
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
	for (size_t i = 0; i < cfg.friends.size(); i++)
		if (QString::fromStdString(cfg.friends[i].handle).compare(gameName, Qt::CaseInsensitive) == 0)
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

Engine::Feed Engine::feedState(const Friend &f) const
{
	if (f.kind != FriendKind::Discord)
		return Feed::Unknown;
	// The voice roster is the authority when it knows my channel: a squad mate it does not list
	// there is not streaming to me, whatever windows are still open on this PC - Discord leaves a
	// pop-out up after a stream ends, and a bound window is not proof of a picture.
	if (rosterLive()) {
		QString h = QString::fromStdString(f.handle).toLower(), n = QString::fromStdString(f.name).toLower();
		QString me = QString::fromStdString(cfg.myDiscord).toLower(), myChan;
		if (!me.isEmpty())
			for (const auto &m : roster.members())
				if (m.handle.toLower() == me)
					myChan = m.channel;
		auto isThem = [&](const Roster::Member &m) {
			QString mh = m.handle.toLower(), mn = m.name.toLower();
			if (!h.isEmpty() && (mh == h || mn == h))
				return true;
			if (mh == n || mn == n)
				return true;
			// display names carry a rank ("Recruit Moriar") in front of the name a slot was given
			return n.size() >= 3 &&
			       (mn.endsWith(" " + n) || mn.startsWith(n + " ") || mn.contains(" " + n + " "));
		};
		for (const auto &m : roster.members())
			if (isThem(m)) {
				if (!myChan.isEmpty() && m.channel != myChan)
					return Feed::Off;
				return m.streaming ? Feed::Live : Feed::Off;
			}
		if (!myChan.isEmpty())
			return Feed::Off; // the roster knows my channel and they are not in it
		if (!me.isEmpty())
			return Feed::Off; // I am not in voice at all: nobody is live with me
	}
	if (f.onPopout())
		return Feed::Live; // no roster to ask: a bound window is the best sign there is
	return Feed::Unknown;
}

QString Engine::feedStateText(const Friend &f) const
{
	switch (feedState(f)) {
	case Feed::Live:
		return "live";
	case Feed::Off:
		return "not streaming";
	default:
		return "";
	}
}

int Engine::anyLiveFriend() const
{
	if (cfg.active() && feedState(*cfg.active()) != Feed::Off)
		return cfg.activeFriend;
	for (size_t i = 0; i < cfg.friends.size(); ++i)
		if (feedState(cfg.friends[i]) == Feed::Live)
			return (int)i;
	for (size_t i = 0; i < cfg.friends.size(); ++i)
		if (feedState(cfg.friends[i]) == Feed::Unknown)
			return (int)i;
	return -1;
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
	int best = -1, bestD = 0, bestRank = 9;
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
		// a squad mate with nothing to show is never the one to show, however close: the nearest
		// one who is actually streaming wins, and a slot nobody can vouch for comes after those
		Feed state = feedState(cfg.friends[i]);
		if (state == Feed::Off) {
			if (problem)
				*problem = QString::fromStdString(cfg.friends[i].name) +
					   QString(" is closest at %1 m but is not streaming").arg(e.dist);
			continue;
		}
		int rank = state == Feed::Live ? 0 : 1;
		if (best < 0 || rank < bestRank || (rank == bestRank && e.dist < bestD)) {
			best = i;
			bestD = e.dist;
			bestRank = rank;
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
		// nobody near you has a picture: any live squad mate beats staying on one who does not
		if (cfg.active() && feedState(*cfg.active()) != Feed::Live) {
			int alt = anyLiveFriend();
			if (alt >= 0 && alt != cfg.activeFriend && feedState(cfg.friends[alt]) == Feed::Live) {
				log("Closest squad mate: nobody near you is streaming" +
				    (problem.isEmpty() ? QString() : " (" + problem + ")") + " - showing " +
				    QString::fromStdString(cfg.friends[alt].name) + ", who is live.");
				setActive(alt);
				lastPick_ = clock_::now();
				return;
			}
		}
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
		// whoever is about to be shown must have a picture: a squad mate the roster has in voice
		// but not streaming shows nothing, so a live one takes their place, or you stay on your own
		if (!applied_ && cfg.active() && feedState(*cfg.active()) == Feed::Off) {
			int alt = anyLiveFriend();
			if (alt < 0) {
				log("Downed, but none of the squad is streaming right now - staying on your own POV.");
				return;
			}
			if (alt != cfg.activeFriend) {
				log("Downed: " + QString::fromStdString(cfg.active()->name) +
				    " is not streaming, showing " + QString::fromStdString(cfg.friends[alt].name) +
				    " instead.");
				cfg.activeFriend = alt;
				cfg.save();
				emit stateChanged();
			}
		}
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
	{
		std::string ev = sw.applyVertical(cfg, on); // the same swap on the portrait canvas, if set
		if (!ev.empty())
			errors.push_back("vertical: " + ev);
	}
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
	if (on) {
		// their window has had a moment to draw by now; crop Discord's chrome off what we show
		QTimer::singleShot(500, this, [this]() {
			const Friend *a = cfg.active();
			if (!stopping_ && applied_ && a && a->kind == FriendKind::Discord && a->trim)
				sw.trimToContent(cfg, *a);
		});
	}
	if (dualOn_)
		// the small window makes way for the full-screen swap, and returns. Not re-armed on the way
		// down: the swap has just shown that capture, and warm would make it transparent again.
		sw.applyDual(cfg, !on, false);
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

/// Where the action is in a clip, in ms from the start of the file: from the sidecar offsets
/// when the plugin wrote them, else the usual "about 7 s before the end".
static void replayWindow(const Clips::Entry &e, qint64 durMs, int preS, int postS, qint64 *startMs, qint64 *endMs)
{
	qint64 first = e.firstS >= 0 ? (qint64)(e.firstS * 1000) : (e.momentS >= 0 ? (qint64)(e.momentS * 1000) : 7000);
	qint64 last = e.momentS >= 0 ? (qint64)(e.momentS * 1000) : first;
	*startMs = std::max<qint64>(0, durMs - first - (qint64)preS * 1000);
	*endMs = std::min<qint64>(durMs, durMs - last + (qint64)postS * 1000);
	if (*endMs <= *startMs + 1000)
		*endMs = std::min<qint64>(durMs, *startMs + 8000);
}

void Engine::playReplay(const QString &why)
{
	const Clips::Entry *last = nullptr;
	for (auto it = clips.history().rbegin(); it != clips.history().rend(); ++it)
		if (!it->path.isEmpty() && QFileInfo::exists(it->path)) {
			last = &*it;
			break;
		}
	if (!last) {
		log("Instant replay: no highlight saved yet this session.");
		return;
	}
	if (replaying())
		stopReplay("replaced");
	std::string e = sw.playMedia(cfg, last->path.toStdString(), cfg.replayScale,
				     cfg.replaySound ? cfg.replayVolume : 0, true);
	if (!e.empty()) {
		log("Instant replay: " + QString::fromStdString(e));
		return;
	}
	replayWhat_ = last->title.isEmpty() ? QFileInfo(last->path).fileName() : last->title;
	// the length is known only once the file has loaded: the tick seeks and arms the stop
	pendingReplay_ = *last;
	replayLengthMs_ = 1; // "playing, not yet sought"
	replaySought_ = false;
	replayClock_.start();
	replayTimer_.start(150);
	addEvent(QDateTime::currentDateTime().toString("HH:mm:ss") + "  REPLAY " + replayWhat_);
	log("Instant replay: " + replayWhat_ + " - " + why + ".");
	emit stateChanged();
}

QString Engine::highlightsDir() const
{
	QString folder = QString::fromStdString(cfg.highlightsFolder);
	if (!folder.isEmpty())
		return folder;
	QString base = QString::fromStdString(cfg.clipFolder);
	if (base.isEmpty())
		base = QString::fromStdString(cfg.backtrackFolder);
	if (base.isEmpty()) {
		QStringList bt = Clips::discoverBacktrackFolders();
		if (!bt.isEmpty())
			base = bt.first();
	}
	if (base.isEmpty() && !clips.history().empty())
		base = QFileInfo(clips.history().back().path).absolutePath();
	return base.isEmpty() ? QString() : base + "/highlights";
}

void Engine::onStreaming(bool live)
{
	QJsonObject o;
	o["type"] = "stream";
	o["state"] = live ? "started" : "stopped";
	bridge.sendJson(o);
	if (live) {
		sessionStart_ = QDateTime::currentDateTime();
		log("Streaming: the highlights session starts here.");
	} else if (cfg.highlightsAuto)
		requestHighlights("stream ended", false);
}

void Engine::requestHighlights(const QString &why, bool thenPlay)
{
	if (!appConnected()) {
		log("Highlights: ClipHound is not running, so nothing can be built.");
		return;
	}
	QJsonArray arr;
	for (const auto &e : clips.history()) {
		if (e.when < sessionStart_.addSecs(-5) || e.path.isEmpty() || !QFileInfo::exists(e.path))
			continue;
		QJsonObject c;
		c["path"] = e.path;
		c["title"] = e.title;
		c["tags"] = QJsonArray::fromStringList(e.tags);
		c["when"] = e.when.toString(Qt::ISODate);
		arr.append(c);
	}
	if (arr.isEmpty()) {
		log("Highlights: no clips saved this session yet, nothing to build.");
		return;
	}
	QJsonObject o;
	o["type"] = "highlights_build";
	o["clips"] = arr;
	o["out"] = highlightsDir();
	o["player"] = playerName();
	o["max"] = cfg.highlightsMax;
	bridge.sendJson(o);
	highlightsBuilding_ = true;
	highlightsThenPlay_ = thenPlay;
	log(QString("Highlights: building from %1 clip%2 - %3.")
		    .arg(arr.size())
		    .arg(arr.size() == 1 ? "" : "s")
		    .arg(why));
	emit stateChanged();
}

void Engine::sendObsHealth()
{
#ifdef _WIN32
	// The 0.13.0 start-up freeze ended in "used all of its system allowance of handles for Window
	// Manager objects": something in the OBS process was making USER objects fast. Count them, and
	// say so in the log when the count climbs, so the next one names its culprit by time.
	{
		DWORD user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
		DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
		if (lastUserObjects_ == 0 || user > lastUserObjects_ + 300 || user > 6000)
			log(QString("Windows objects held by OBS: %1 user, %2 GDI (the limit is 10000 each).")
				    .arg(user)
				    .arg(gdi));
		lastUserObjects_ = user;
	}
#endif
	if (!appConnected())
		return;
	QJsonObject o;
	o["type"] = "obs_health";
	o["lagged"] = (double)obs_get_lagged_frames();
	o["total"] = (double)obs_get_total_frames();
	double dropped = 0;
	if (obs_output_t *out = obs_frontend_get_streaming_output()) {
		dropped = (double)obs_output_get_frames_dropped(out);
		obs_output_release(out);
	}
	o["dropped"] = dropped;
	bridge.sendJson(o);
}

void Engine::playCompilation(const QString &why)
{
	QString folder = highlightsDir();
	QFileInfoList files =
		folder.isEmpty() || !QDir(folder).exists()
			? QFileInfoList()
			: QDir(folder).entryInfoList({"*.mp4", "*.mkv", "*.mov"}, QDir::Files, QDir::Time);
	// nothing built yet, or clips saved since the last one: build first, then play
	QDateTime lastClip;
	for (const auto &e : clips.history())
		if (e.when >= sessionStart_.addSecs(-5))
			lastClip = e.when;
	if (files.isEmpty() || (lastClip.isValid() && files.first().lastModified() < lastClip)) {
		if (highlightsBuilding_) {
			highlightsThenPlay_ = true;
			log("Highlights: still building; it will play when it is ready.");
			return;
		}
		requestHighlights(why, true);
		return;
	}
	if (replaying())
		stopReplay("replaced");
	std::string e = sw.playMedia(cfg, files.first().absoluteFilePath().toStdString(), 100,
				     cfg.replaySound ? cfg.replayVolume : 0);
	if (!e.empty()) {
		log("Play highlights: " + QString::fromStdString(e));
		return;
	}
	replayWhat_ = files.first().fileName();
	pendingReplay_ = Clips::Entry{};
	pendingReplay_.path.clear();
	replayLengthMs_ = 1;
	replaySought_ = true; // from the start, to the end
	replayStartMs_ = 0;
	replayEndMs_ = 0; // = the whole file, once its length is known
	replayClock_.start();
	replayTimer_.start(250);
	addEvent(QDateTime::currentDateTime().toString("HH:mm:ss") + "  HIGHLIGHTS " + replayWhat_);
	log("Play highlights: " + replayWhat_ + " - " + why + ".");
	emit stateChanged();
}

void Engine::replayTick()
{
	if (!replaying())
		return;
	qint64 dur = sw.mediaDurationMs();
	int st = sw.mediaState();
	if (!replaySought_) {
		// wait for the file to be open and playing before asking for the seek: a seek sent while
		// it is still opening is dropped, and the whole clip plays from the start
		if (dur <= 0 || st != OBS_MEDIA_STATE_PLAYING) {
			if (replayClock_.elapsed() > 5000) {
				log("Instant replay: the file did not start playing.");
				stopReplay("failed");
			}
			return;
		}
		replayWindow(pendingReplay_, dur, cfg.replayPreS, cfg.replayPostS, &replayStartMs_, &replayEndMs_);
		if (replayStartMs_ > 0)
			sw.seekMedia(replayStartMs_);
		replaySought_ = true;
		replaySeekChecks_ = 0;
		replayLengthMs_ = std::max<qint64>(1, replayEndMs_ - replayStartMs_);
		replayClock_.restart();
		return;
	}
	if (replayEndMs_ == 0 && dur > 0) {
		replayEndMs_ = dur; // the whole file (the compilation)
		replayLengthMs_ = dur;
	}
	qint64 t = sw.mediaTimeMs();
	// did the seek take? If playback is still well before the start after a moment, ask again
	if (replayStartMs_ > 0 && replaySeekChecks_ < 3 && replayClock_.elapsed() > 500 && t < replayStartMs_ - 1500) {
		sw.seekMedia(replayStartMs_);
		replaySeekChecks_++;
		replayClock_.restart();
		return;
	}
	bool pastEnd = replayEndMs_ > 0 && t >= replayEndMs_;
	bool ended = replayClock_.elapsed() > 800 && (st == OBS_MEDIA_STATE_ENDED || st == OBS_MEDIA_STATE_STOPPED);
	bool safety = replayClock_.elapsed() > replayLengthMs_ + 4000; // the clock never lies, the state might
	if (pastEnd || ended || safety)
		stopReplay("finished");
}

void Engine::stopReplay(const QString &why)
{
	replayTimer_.stop();
	bool was = replaying();
	replayLengthMs_ = 0;
	replayStartMs_ = replayEndMs_ = 0;
	sw.stopMedia(cfg);
	if (was)
		log("Replay off - " + why + ".");
	replayWhat_.clear();
	emit stateChanged();
}

QString Engine::chatReplay(const QString &who)
{
	if (!cfg.replayChat)
		return "chat replays are off (Settings, Clips)";
	QDateTime now = QDateTime::currentDateTime();
	if (lastChatReplay_.isValid()) {
		qint64 left = cfg.replayCooldownS - lastChatReplay_.secsTo(now);
		if (left > 0)
			return QString("cooldown: %1 s to go").arg(left);
	}
	bool have = false;
	for (const auto &e : clips.history())
		if (!e.path.isEmpty() && QFileInfo::exists(e.path))
			have = true;
	if (!have)
		return "no highlight saved yet";
	lastChatReplay_ = now;
	playReplay("chat: " + who);
	return "";
}

void Engine::showInDual(int idx, const QString &why)
{
	if (idx < 0 || idx >= (int)cfg.friends.size())
		return;
	if (cfg.dualFriend != idx) {
		cfg.dualFriend = idx;
		cfg.save();
	}
	if (dualOn_)
		sw.applyDual(cfg, false, false); // swap the person inside the window, not just the label
	setDual(true, why);
}

void Engine::setDual(bool on, const QString &why)
{
	if (on && !cfg.dual()) {
		// nobody picked for the small window yet: the first squad mate, which is what the Dual POV
		// drop-down on the dock shows
		if (!cfg.friends.empty()) {
			cfg.dualFriend = 0;
			cfg.save();
		} else {
			log("Dual POV: add a squad mate first.");
			return;
		}
	}
	// turned on by hand it stays on; only a window the vehicle detector opened is its to close
	if (on)
		dualAutoOn_ = false;
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
	    " - " + why + (on && !dualAutoOn_ ? " (forced: stays until you turn it off)." : "."));
	emit stateChanged();
}

/// ClipHound read the vehicle keybind list: a seat name, "vehicle" (in one, seat unclear) or "none".
void Engine::onVehicle(const QString &seat)
{
	vehicleSeat_ = seat;
	if (!cfg.dual())
		return;
	if (seat == "none") {
		// out of the vehicle: a window the detector opened goes - unless asked to stay. One you
		// turned on yourself is yours to turn off.
		if (dualOn_ && dualAutoOn_ && !cfg.dualKeep) {
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
	if (!dualOn_) {
		setDual(true, "in a vehicle: " + seat);
		dualAutoOn_ = dualOn_; // after the call: setDual clears it, and this one was the detector's
	}
	emit stateChanged();
}

void Engine::toggleDual()
{
	setDual(!dualOn_, "hotkey"); // the person is the Dual POV drop-down's pick
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
	log(on ? "Auto switch on: a squad mate takes over when you are downed."
	       : "Auto switch off: your own POV stays up. Show friend's POV still works by hand, and clips keep coming.");
	emit stateChanged();
}

void Engine::setFriendAudio(bool on)
{
	if (cfg.friendAudio != on) {
		cfg.friendAudio = on;
		cfg.save();
	}
	sw.applyFriendAudio(cfg, applied_);
	const Friend *a = cfg.active();
	QString who = applied_ && a ? QString::fromStdString(a->name) : QString();
	if (on)
		log(who.isEmpty()
			    ? QString("Squad mate's sound on: whoever is on screen is the one feed with sound.")
			    : QString("Squad mate's sound on: %1's feed has sound now; every other squad mate stays muted.")
				      .arg(who));
	else
		log(who.isEmpty() ? QString("Squad mate's sound off: their feeds are silent on your stream.")
				  : QString("Squad mate's sound off: %1's feed is muted on your stream.").arg(who));
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
	if (!cfg.sceneV.empty()) {
		// the portrait overlay previews too, on its own, without moving a squad mate's feed about
		bool show = lookPreview_ || applied_;
		std::string ev = sw.applyVertical(cfg, show);
		if (!ev.empty() && e.empty())
			e = ev;
	}
	log(!e.empty() ? QString::fromStdString("Look: " + e)
		       : (lookPreview_ ? "Look overlay showing in OBS." : "Look overlay hidden."));
}
