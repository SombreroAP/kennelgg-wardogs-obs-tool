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
	bool isWeb() const { return kind == FriendKind::Twitch || kind == FriendKind::VdoNinja; }
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
	bool appConfigDirty =
		false; // edited while the app was not connected; push on connect // tell ClipHound to quit when OBS closes (and end it if we started it)
	std::string clipNameTemplate = "{date}_{time}_{tags}";
	bool autoStartReplay = true;
	bool clipOnDowned = false;            // also clip when you get downed (the moment before is in the buffer)
	bool clipUseReplay = true;            // save OBS's own replay buffer on a clip
	std::vector<std::string> clipHotkeys; // OBS hotkey names fired on every clip (e.g. Aitum Backtrack "save")

	// detection
	bool autoDetect = true;
	bool enabled = true;
	double threshold = 0.85;
	int pollMs = 200;
	int downFrames = 3, upFrames = 5, minDownMs = 2000;
	bool watchRevive =
		true; // look for "REVIVING" on the friend's feed and switch back instantly when the damage log goes
	double reviveThreshold = 0.80;
	double customTemplateWidthFrac = 0;                        // 0 = built-in damage-log template
	double boxX = 0.80, boxY = 0.45, boxW = 0.15, boxH = 0.04; // capture box for a custom template

	static const char *webSourceName() { return "Kennel web"; }
	static const char *overlaySourceName() { return "Kennel look"; }
	static const char *hideFilterName() { return "Kennel hide"; }
	static std::string sourceFor(const Friend &f) { return f.isWeb() ? webSourceName() : f.source; }
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
