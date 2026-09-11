#pragma once
#include <string>
#include <vector>

enum class FriendKind { Twitch = 0, VdoNinja = 1, ObsSource = 2, Discord = 3, Ndi = 4, Kick = 5, YouTube = 6 };

struct Friend {
	std::string name;
	FriendKind kind = FriendKind::Twitch;
	std::string source;      // OBS source name (ObsSource / Discord / Ndi: the video source)
	std::string audioSource; // Discord: the Application Audio Capture created for them
	std::string channel;     // Twitch login, VDO.Ninja stream id, Discord window, or NDI source name
	int vdoHeight = 1080, vdoFps = 60, vdoKbps = 12000; // VDO.Ninja quality (push and view links)
	std::string vdoCodec = "h264";
	std::string gameName; // their name in the game's NEARBY list ("" = the name above)
	bool trim = true;     // Discord: crop the window's flat borders away, leaving the game picture
	int ndiBw = 0;        // NDI: 0 = full quality, 1 = low bandwidth (a small, gentler stream)
	int ndiSync = 0;      // timing: 0 leave DistroAV alone, 1 frame sync, 2 timestamps, 3 timecode, 4 none
	bool isWeb() const
	{
		return kind == FriendKind::Twitch || kind == FriendKind::VdoNinja || kind == FriendKind::Kick ||
		       kind == FriendKind::YouTube;
	}
	const std::string &nearName() const { return gameName.empty() ? name : gameName; }
	bool ownsSources() const { return kind == FriendKind::Discord || kind == FriendKind::Ndi; }
};

struct Config {
	// what to watch / where to switch
	std::string gameSource;
	std::string sceneName; // "" = the scene that is live
	std::vector<Friend> friends;
	int activeFriend = 0;
	std::vector<std::string> muteWhileDowned;
	/// Sources that stay above everything the plugin adds: the streamer's camera, their alerts.
	/// First in the list is the topmost.
	std::vector<std::string> onTop;
	bool onTopV1 = false; // seeded once from what is in the scene
	bool warmNdi = false; // keep an NDI feed connected while you are alive (constant bandwidth)
	// A second, portrait canvas (Aitum Vertical): the swap and the look overlay happen there too.
	std::string sceneV;           // the vertical scene the swap is applied in ("" = off)
	std::string lookPos = "ml";   // where the POV tag sits: tl tc ml mc bl br
	int ndiShareHeight = 1080;    // what we send over NDI: 0 = the full canvas
	int ndiShareFps = 0;          // always 0 now: the share runs at OBS's own rate
	bool ndiShareV1 = false;      // 0.5.4 scaled the share to 720p30 by default
	bool ndiShareV2 = false;      // ...0.5.6 moved it to 1080p30 and gave every size a 60 as well
	bool lookPosV1 = false;       // one-time move off the bottom-left corner
	bool audioAutoPicked = false; // desktop audio was ticked automatically once
	bool bringToFront = true;
	bool keepWarm = true;
	bool preloadFeeds = false;   // every squad mate's feed loaded and playing, hidden and silent
	bool friendAudio = false;    // play the squad mate's own game audio while showing them
	bool audioDefaults2 = false; // one-time move to "nothing of yours is muted by default"
	bool audioDefaults3 = false; // ...and once more: nothing muted, and no sound taken from their feed
	bool ndiShelved = false;     // NDI and LAN discovery off and hidden until they are ready
	int vdoBitrateKbps = 12000;  // VDO.Ninja video bitrate asked for on both ends (LAN/fibre: 12-20 Mbit/s)

	// dual POV: a squad mate's feed in a small window over your own POV (tank / chopper crews)
	bool dualEnabled = false;
	int dualFriend = -1;                    // index into friends, -1 = none
	std::string dualPreset = "tank-driver"; // tank-driver | tank-gunner | havoc-pilot | havoc-gunner | custom
	double dualX = 0.012, dualY = 0.19, dualW = 0.26; // fractions of the canvas; height keeps 16:9
	int dualOpacity = 100;
	bool dualKeep = false; // leave the window up when you get out of the vehicle
	bool dualAuto = false; // ClipHound reads the vehicle keybind list and turns the window on / off
	double vehX = 0.86, vehY = 0.60, vehW = 0.14, vehH = 0.25; // where that list is (fractions)
	static const char *dualSceneName() { return "Kennel dual"; }
	static const char *dualFeedName() { return "Kennel dual feed"; }
	const Friend *dual() const
	{
		return dualFriend >= 0 && dualFriend < (int)friends.size() ? &friends[dualFriend] : nullptr;
	}

	// update check: a small JSON on kennel.gg, no account and no telemetry
	bool updateCheck = true;
	std::string updateUrl = "https://kennel.gg/obs-tools/latest.json";
	std::string updateSkip; // a version the user asked not to be told about again

	// look overlay
	bool lookName = true, lookPlate = true, lookCam = false, lookGrain = false, lookVignette = false;
	std::string lookLabel = "POV";
	int grainAmount = 40;

	// squad on the LAN
	std::string playerName;  // shown to squad mates; defaults to the PC name
	bool lanEnabled = false; // announce myself and listen for squad mates (UDP 47821) - shelved
	int lanPort = 47821;
	bool ndiShare = false;     // publish my game feed over NDI (DistroAV) for squad mates - shelved
	bool autoAddPeers = false; // squad mates found on the LAN are added to the list by themselves

	// companion app / bridge / clips
	int bridgePort = 47820;
	bool bridgeEnabled = true;
	std::string appPath; // ClipHound (or any companion) to launch when OBS starts
	bool launchApp = false;
	bool closeAppWithObs = true;
	std::string clipFolder; // move renamed replay clips here ("" = leave in OBS's recording folder)
	// ClipHound settings edited in the plugin and pushed to the app over the bridge
	std::string appPlayerName, appLibrary, appBroadcaster;
	bool appTwitchEnabled = false;
	bool appEveryKill = false;
	double feedX = 0.0, feedY = 0.42, feedW = 0.24,
	       feedH = 0.16; // kill-feed area (fractions of the game source) sent to ClipHound
	double appMultikillWindow = 30;
	int appFps = 10; // frames per second ClipHound reads the kill feed at
	// the game's NEARBY list (bottom right), read by ClipHound: show whoever is closest
	bool nearEnabled = false; // pick the squad mate the game says is nearest when you go down
	bool nearFollow = true;   // keep following the nearest one while you are down
	double nearX = 0.80, nearY = 0.79, nearW = 0.19,
	       nearH = 0.14;   // where the NEARBY list is (fractions of the game source)
	int nearMarginM = 15;  // someone must be this many metres closer to take over mid-swap
	int nearMaxM = 50;     // once on screen, only swap over to someone this close or closer
	int nearCooldownS = 4; // shortest gap between two swaps of the feed while down, 1-10 s
	int nearTtlS = 20;     // a reading older than this is stale and ignored
	bool appConfigDirty =
		false; // edited while the app was not connected; push on connect // tell ClipHound to quit when OBS closes (and end it if we started it)
	std::string clipNameTemplate = "{title}_{tags}_{date}_{time}";
	bool autoStartReplay = true;
	bool clipOnDowned = false;            // also clip when you get downed (the moment before is in the buffer)
	bool clipUseReplay = true;            // save OBS's own replay buffer on a clip
	std::vector<std::string> clipHotkeys; // OBS hotkey names fired on every clip (e.g. Aitum Backtrack "save")
	std::string backtrackFolder; // where Aitum Backtrack writes; new files there after a trigger get our name

	// detection
	bool autoDetect = true;
	bool enabled = true;
	double threshold = 0.80;
	bool thresholdV2 = false; // one-time move to 0.80 with the wording-only template
	int pollMs = 100;
	int downFrames = 2, upFrames = 2, minDownMs = 0;
	int downDelayMs =
		2000; // wait this long after the damage log appears before showing the squad mate (cancelled if it goes away)
	int upDelayMs = 0;      // wait this long after it disappears before coming back (0 = instant)
	double holdDrop = 0.15; // while downed the log counts as still there down to (threshold - this)
	bool holdV2 = false;    // one-time move to holding through a washed-out poll
	double releaseDrop =
		0.08; // while downed the score is steady; a drop this big below its peak = the log is fading = revived
	double memScale = 0, memX = 0, memY = 0; // where the damage log was last found (fast re-detect)
	bool watchRevive =
		true; // look for "REVIVING" on the friend's feed and switch back instantly when the damage log goes
	double reviveThreshold = 0.80;
	bool wideSearch = false;            // look over the whole frame at more sizes: slower, for unusual HUDs
	double customTemplateWidthFrac = 0; // 0 = built-in damage-log template
	double boxX = 0.84, boxY = 0.62, boxW = 0.13, boxH = 0.035; // capture box for a custom template

	static const char *webSourceName() { return "Kennel web"; }
	/// Browser source a web feed lives in. With preloading each squad mate gets their own, so
	/// every stream is already playing when the swap happens; otherwise they share one.
	std::string webSourceFor(const Friend &f) const
	{
		return preloadFeeds ? std::string(webSourceName()) + " - " + f.name : webSourceName();
	}
	static const char *overlaySourceName() { return "Kennel look"; }
	static const char *overlaySourceNameV() { return "Kennel look (vertical)"; }
	static const char *hideFilterName() { return "Kennel hide"; }
	std::string sourceFor(const Friend &f) const { return f.isWeb() ? webSourceFor(f) : f.source; }
	const Friend *active() const
	{
		return activeFriend >= 0 && activeFriend < (int)friends.size() ? &friends[activeFriend] : nullptr;
	}
	bool lookEnabled() const { return lookName || lookCam || lookGrain || lookVignette; }

	void load();
	void save() const;
	static std::string configDir();
	static std::string configFile(const char *name);
};
