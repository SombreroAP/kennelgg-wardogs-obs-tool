#pragma once
#include <string>
#include <vector>

enum class FriendKind { Twitch = 0, VdoNinja = 1, ObsSource = 2 };

struct Friend {
	std::string name;
	FriendKind kind = FriendKind::Twitch;
	std::string source;  // OBS source name (ObsSource)
	std::string channel; // Twitch login or VDO.Ninja stream id
	bool isWeb() const { return kind != FriendKind::ObsSource; }
};

struct Config {
	// what to watch / where to switch
	std::string gameSource;
	std::string sceneName; // "" = the scene that is live
	std::vector<Friend> friends;
	int activeFriend = 0;
	std::vector<std::string> muteWhileDowned;
	bool bringToFront = true;
	bool keepWarm = true;

	// look overlay
	bool lookName = true, lookPlate = true, lookCam = false, lookGrain = false, lookVignette = false;
	std::string lookLabel = "POV";
	int grainAmount = 40;

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

	static const char *webSourceName() { return "POVBridge web"; }
	static const char *overlaySourceName() { return "POVBridge look"; }
	static const char *hideFilterName() { return "POVBridge hide"; }
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
