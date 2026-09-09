#pragma once
#include <string>
#include <vector>

enum class FriendKind { Twitch = 0, VdoNinja = 1, ObsSource = 2, Discord = 3, Ndi = 4 };

struct Friend {
	std::string name;
	FriendKind kind = FriendKind::Twitch;
	std::string source;      // OBS source name (ObsSource / Discord / Ndi: the video source)
	std::string audioSource; // Discord: the Application Audio Capture created for them
	std::string channel;     // Twitch login, VDO.Ninja stream id, Discord window, or NDI source name
	int vdoHeight = 1080, vdoFps = 60, vdoKbps = 12000; // VDO.Ninja quality (push and view links)
	std::string vdoCodec = "h264";
	std::string gameName; // their name in the game's NEARBY list ("" = the name above)
	bool isWeb() const { return kind == FriendKind::Twitch || kind == FriendKind::VdoNinja; }
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
	bool audioAutoPicked = false; // desktop audio was ticked automatically once
	bool bringToFront = true;
	bool keepWarm = true;
	bool preloadFeeds = false;   // every squad mate's feed loaded and playing, hidden and silent
	bool friendAudio = false;    // play the squad mate's own game audio while showing them
	bool audioDefaults2 = false; // one-time move to "nothing of yours is muted by default"
	int vdoBitrateKbps = 12000;  // VDO.Ninja video bitrate asked for on both ends (LAN/fibre: 12-20 Mbit/s)

	// look overlay
	bool lookName = true, lookPlate = true, lookCam = false, lookGrain = false, lookVignette = false;
	std::string lookLabel = "POV";
	int grainAmount = 40;

	// squad on the LAN
	std::string playerName; // shown to squad mates; defaults to the PC name
	bool lanEnabled = true; // announce myself and listen for squad mates (UDP 47821)
	int lanPort = 47821;
	bool ndiShare = true;     // publish my game feed over NDI (DistroAV) for squad mates, without my mic
	bool autoAddPeers = true; // squad mates found on the LAN are added to the list by themselves

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
	double threshold = 0.85;
	int pollMs = 100;
	int downFrames = 2, upFrames = 1, minDownMs = 0;
	int downDelayMs =
		2000; // wait this long after the damage log appears before showing the squad mate (cancelled if it goes away)
	int upDelayMs = 0; // wait this long after it disappears before coming back (0 = instant)
	double releaseDrop =
		0.08; // while downed the score is steady; a drop this big below its peak = the log is fading = revived
	double memScale = 0, memX = 0, memY = 0; // where the damage log was last found (fast re-detect)
	bool watchRevive =
		true; // look for "REVIVING" on the friend's feed and switch back instantly when the damage log goes
	double reviveThreshold = 0.80;
	double customTemplateWidthFrac = 0;                        // 0 = built-in damage-log template
	double boxX = 0.80, boxY = 0.45, boxW = 0.15, boxH = 0.04; // capture box for a custom template

	static const char *webSourceName() { return "Kennel web"; }
	/// Browser source a web feed lives in. With preloading each squad mate gets their own, so
	/// every stream is already playing when the swap happens; otherwise they share one.
	std::string webSourceFor(const Friend &f) const
	{
		return preloadFeeds ? std::string(webSourceName()) + " - " + f.name : webSourceName();
	}
	static const char *overlaySourceName() { return "Kennel look"; }
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
