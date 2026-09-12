#include "config.h"
#include <obs-module.h>
#include <util/platform.h>
#include <plugin-support.h>

std::string Config::configDir()
{
	char *p = obs_module_config_path("");
	std::string s = p ? p : "";
	bfree(p);
	return s;
}

std::string Config::configFile(const char *name)
{
	char *p = obs_module_config_path(name);
	std::string s = p ? p : name;
	bfree(p);
	return s;
}

static std::vector<std::string> getStrings(obs_data_t *d, const char *key)
{
	std::vector<std::string> out;
	obs_data_array_t *arr = obs_data_get_array(d, key);
	if (!arr)
		return out;
	size_t n = obs_data_array_count(arr);
	for (size_t i = 0; i < n; i++) {
		obs_data_t *it = obs_data_array_item(arr, i);
		out.emplace_back(obs_data_get_string(it, "v"));
		obs_data_release(it);
	}
	obs_data_array_release(arr);
	return out;
}

static void setStrings(obs_data_t *d, const char *key, const std::vector<std::string> &v)
{
	obs_data_array_t *arr = obs_data_array_create();
	for (auto &s : v) {
		obs_data_t *it = obs_data_create();
		obs_data_set_string(it, "v", s.c_str());
		obs_data_array_push_back(arr, it);
		obs_data_release(it);
	}
	obs_data_set_array(d, key, arr);
	obs_data_array_release(arr);
}

#define GETS(name) name = obs_data_get_string(d, #name)
#define GETB(name) name = obs_data_get_bool(d, #name)
#define GETI(name) name = (int)obs_data_get_int(d, #name)
#define GETD(name) name = obs_data_get_double(d, #name)
#define DEFS(name) obs_data_set_default_string(d, #name, name.c_str())
#define DEFB(name) obs_data_set_default_bool(d, #name, name)
#define DEFI(name) obs_data_set_default_int(d, #name, name)
#define DEFD(name) obs_data_set_default_double(d, #name, name)
#define SETS(name) obs_data_set_string(d, #name, name.c_str())
#define SETB(name) obs_data_set_bool(d, #name, name)
#define SETI(name) obs_data_set_int(d, #name, name)
#define SETD(name) obs_data_set_double(d, #name, name)

void Config::load()
{
	std::string path = configFile("config.json");
	obs_data_t *d = obs_data_create_from_json_file(path.c_str());
	if (!d) {
		// the module id changed with 0.7.0, and OBS keeps a plugin's settings under that id: pick
		// up the old file, once, so nobody sets everything up again
		std::string dir = configDir();
		size_t cut = dir.find_last_of("/\\", dir.size() - 2);
		std::string oldPath =
			(cut == std::string::npos ? dir : dir.substr(0, cut + 1)) + "kennel-wardogs/config.json";
		d = obs_data_create_from_json_file(oldPath.c_str());
		if (d)
			obs_log(LOG_INFO, "settings carried over from %s", oldPath.c_str());
	}
	if (!d)
		d = obs_data_create();
	// defaults come from the current field values (constructor defaults)
	DEFS(gameSource);
	DEFS(sceneName);
	DEFI(activeFriend);
	DEFB(bringToFront);
	DEFB(keepWarm);
	DEFB(preloadFeeds);
	DEFB(friendAudio);
	DEFB(audioDefaults2);
	DEFB(audioDefaults3);
	DEFB(ndiShelved);
	DEFB(discordShared1);
	DEFB(audioDefaults4);
	DEFB(audioAutoPicked);
	DEFB(rosterEnabled);
	DEFS(rosterUrl);
	DEFS(myDiscord);
	DEFS(rosterChannel);
	DEFI(rosterPollS);
	DEFB(rosterAddSources);
	DEFB(popoutTuck);
	DEFI(popoutMonitor);
	DEFS(sceneV);
	DEFI(vdoBitrateKbps);
	DEFB(dualEnabled);
	DEFI(dualFriend);
	DEFS(dualPreset);
	DEFD(dualX);
	DEFD(dualY);
	DEFD(dualW);
	DEFI(dualOpacity);
	DEFB(dualAuto);
	DEFB(dualKeep);
	DEFB(dualLook);
	DEFD(vehX);
	DEFD(vehY);
	DEFD(vehW);
	DEFD(vehH);
	DEFB(updateCheck);
	DEFS(updateUrl);
	DEFS(updateSkip);
	DEFB(lookName);
	DEFB(lookPlate);
	DEFB(lookCam);
	DEFB(lookGrain);
	DEFB(lookVignette);
	DEFS(lookLabel);
	DEFI(grainAmount);
	DEFB(autoDetect);
	DEFB(enabled);
	DEFD(threshold);
	DEFB(thresholdV2);
	DEFB(onTopV1);
	DEFB(warmNdi);
	DEFS(lookPos);
	DEFI(ndiShareHeight);
	DEFI(ndiShareFps);
	DEFB(ndiShareV1);
	DEFB(ndiShareV2);
	DEFB(lookPosV1);
	DEFD(holdDrop);
	DEFB(holdV2);
	DEFI(pollMs);
	DEFI(downFrames);
	DEFI(upFrames);
	DEFI(minDownMs);
	DEFI(downDelayMs);
	DEFI(upDelayMs);
	DEFD(releaseDrop);
	DEFD(memScale);
	DEFD(memX);
	DEFD(memY);
	DEFB(watchRevive);
	DEFI(bridgePort);
	DEFB(bridgeEnabled);
	DEFS(appPath);
	DEFB(launchApp);
	DEFB(closeAppWithObs);
	DEFS(clipFolder);
	DEFS(appPlayerName);
	DEFS(appLibrary);
	DEFS(appBroadcaster);
	DEFB(appTwitchEnabled);
	DEFB(appEveryKill);
	DEFD(feedX);
	DEFD(feedY);
	DEFD(feedW);
	DEFD(feedH);
	DEFD(appMultikillWindow);
	DEFI(appFps);
	DEFB(nearEnabled);
	DEFB(nearFollow);
	DEFD(nearX);
	DEFD(nearY);
	DEFD(nearW);
	DEFD(nearH);
	DEFI(nearMarginM);
	DEFI(nearCooldownS);
	DEFI(nearMaxM);
	DEFI(nearTtlS);
	DEFB(appConfigDirty);
	DEFS(clipNameTemplate);
	DEFB(autoStartReplay);
	DEFI(replaySeconds);
	DEFB(clipOnDowned);
	DEFB(clipUseReplay);
	DEFS(backtrackFolder);
	DEFS(playerName);
	DEFB(lanEnabled);
	DEFI(lanPort);
	DEFB(ndiShare);
	DEFB(autoAddPeers);
	DEFD(reviveThreshold);
	DEFB(wideSearch);
	DEFD(customTemplateWidthFrac);
	DEFD(boxX);
	DEFD(boxY);
	DEFD(boxW);
	DEFD(boxH);

	GETS(gameSource);
	GETS(sceneName);
	GETI(activeFriend);
	GETB(bringToFront);
	GETB(keepWarm);
	GETB(preloadFeeds);
	GETB(friendAudio);
	GETB(audioDefaults2);
	GETB(audioDefaults3);
	GETB(ndiShelved);
	GETB(discordShared1);
	GETB(audioDefaults4);
	GETB(audioAutoPicked);
	GETB(rosterEnabled);
	GETS(rosterUrl);
	GETS(myDiscord);
	GETS(rosterChannel);
	GETI(rosterPollS);
	GETB(rosterAddSources);
	GETB(popoutTuck);
	GETI(popoutMonitor);
	GETS(sceneV);
	GETI(vdoBitrateKbps);
	GETB(dualEnabled);
	GETI(dualFriend);
	GETS(dualPreset);
	GETD(dualX);
	GETD(dualY);
	GETD(dualW);
	GETI(dualOpacity);
	GETB(dualAuto);
	GETB(dualKeep);
	GETB(dualLook);
	GETD(vehX);
	GETD(vehY);
	GETD(vehW);
	GETD(vehH);
	GETB(updateCheck);
	GETS(updateUrl);
	GETS(updateSkip);
	GETB(lookName);
	GETB(lookPlate);
	GETB(lookCam);
	GETB(lookGrain);
	GETB(lookVignette);
	GETS(lookLabel);
	GETI(grainAmount);
	GETB(autoDetect);
	GETB(enabled);
	GETD(threshold);
	GETB(thresholdV2);
	GETB(onTopV1);
	GETB(warmNdi);
	GETS(lookPos);
	GETI(ndiShareHeight);
	GETI(ndiShareFps);
	GETB(ndiShareV1);
	GETB(ndiShareV2);
	GETB(lookPosV1);
	GETD(holdDrop);
	GETB(holdV2);
	GETI(pollMs);
	GETI(downFrames);
	GETI(upFrames);
	GETI(minDownMs);
	GETI(downDelayMs);
	GETI(upDelayMs);
	GETD(releaseDrop);
	GETD(memScale);
	GETD(memX);
	GETD(memY);
	GETB(watchRevive);
	GETI(bridgePort);
	GETB(bridgeEnabled);
	GETS(appPath);
	GETB(launchApp);
	GETB(closeAppWithObs);
	GETS(clipFolder);
	GETS(appPlayerName);
	GETS(appLibrary);
	GETS(appBroadcaster);
	GETB(appTwitchEnabled);
	GETB(appEveryKill);
	GETD(feedX);
	GETD(feedY);
	GETD(feedW);
	GETD(feedH);
	GETD(appMultikillWindow);
	GETI(appFps);
	GETB(nearEnabled);
	GETB(nearFollow);
	GETD(nearX);
	GETD(nearY);
	GETD(nearW);
	GETD(nearH);
	GETI(nearMarginM);
	GETI(nearCooldownS);
	GETI(nearMaxM);
	GETI(nearTtlS);
	GETB(appConfigDirty);
	GETS(clipNameTemplate);
	GETB(autoStartReplay);
	GETI(replaySeconds);
	GETB(clipOnDowned);
	GETB(clipUseReplay);
	GETS(backtrackFolder);
	GETS(playerName);
	GETB(lanEnabled);
	GETI(lanPort);
	GETB(ndiShare);
	GETB(autoAddPeers);
	GETD(reviveThreshold);
	GETB(wideSearch);
	GETD(customTemplateWidthFrac);
	GETD(boxX);
	GETD(boxY);
	GETD(boxW);
	GETD(boxH);
	muteWhileDowned = getStrings(d, "muteWhileDowned");
	onTop = getStrings(d, "onTop");
	clipHotkeys = getStrings(d, "clipHotkeys");

	friends.clear();
	obs_data_array_t *arr = obs_data_get_array(d, "friends");
	if (arr) {
		size_t n = obs_data_array_count(arr);
		for (size_t i = 0; i < n; i++) {
			obs_data_t *it = obs_data_array_item(arr, i);
			Friend f;
			f.name = obs_data_get_string(it, "name");
			f.kind = (FriendKind)obs_data_get_int(it, "kind");
			f.source = obs_data_get_string(it, "source");
			f.audioSource = obs_data_get_string(it, "audioSource");
			f.channel = obs_data_get_string(it, "channel");
			f.gameName = obs_data_get_string(it, "gameName");
			obs_data_set_default_bool(it, "trim", true);
			f.trim = obs_data_get_bool(it, "trim");
			f.fromRoster = obs_data_get_bool(it, "fromRoster");
			f.handle = obs_data_get_string(it, "handle");
			f.popout = obs_data_get_string(it, "popout");
			f.baseSource = obs_data_get_string(it, "baseSource");
			f.ndiBw = (int)obs_data_get_int(it, "ndiBw");
			f.ndiSync = (int)obs_data_get_int(it, "ndiSync");
			if (obs_data_has_user_value(it, "vdoHeight")) {
				f.vdoHeight = (int)obs_data_get_int(it, "vdoHeight");
				f.vdoFps = (int)obs_data_get_int(it, "vdoFps");
				f.vdoKbps = (int)obs_data_get_int(it, "vdoKbps");
				f.vdoCodec = obs_data_get_string(it, "vdoCodec");
			}
			friends.push_back(f);
			obs_data_release(it);
		}
		obs_data_array_release(arr);
	}
	if (activeFriend >= (int)friends.size())
		activeFriend = 0;
	if (lookLabel.empty())
		lookLabel = "POV";
	// older configs carried the slower first defaults; move them to the responsive ones once
	if (pollMs == 200 && downFrames == 3 && upFrames == 5) {
		pollMs = 100;
		downFrames = 2;
	}
	if (upFrames >= 2 && upFrames <= 5)
		upFrames = 1; // instant return
	if (minDownMs == 2000 || minDownMs == 500)
		minDownMs = 0;
	// the built-in template is the wording only now, which scores lower but stands much further
	// clear of everything else: 0.85 was tuned for the old one and is too strict for this
	if (!thresholdV2) {
		thresholdV2 = true;
		if (threshold >= 0.83 || threshold < 0.75)
			threshold = 0.80;
	}
	// one poll with a bright sky behind the panel used to read as "alive": the log now has to fall
	// well below the threshold, and stay there for two polls, before we say you are up
	if (!holdV2) {
		holdV2 = true;
		if (upFrames < 2)
			upFrames = 2;
	}
	// 0.5.4 dropped the share to 720p30. The size was right; the frame rate was not - it is half the
	// frames, and a mix running at its own rate blacked out other plugins' extra canvases. The share
	// always goes at OBS's rate now, and 1080p is the middle of the sizes.
	ndiShareV1 = true;
	if (!ndiShareV2) {
		ndiShareV2 = true;
		if (ndiShareHeight == 720)
			ndiShareHeight = 1080;
	}
	ndiShareFps = 0;
	// the tag sat bottom-left, over the game's map and score; halfway up the left is clear of both
	if (!lookPosV1) {
		lookPosV1 = true;
		lookPos = "ml";
	}
	// your own game audio is no longer muted by default: the squad mate's feed comes in silent
	// and you turn its sound on if you want it (Settings -> Switch)
	if (!audioDefaults2) {
		audioDefaults2 = true;
		muteWhileDowned.clear();
		audioAutoPicked = true;
	}
	// ClipHound moved with the rename; a saved path to the old folder points at nothing now
	{
		size_t k = appPath.find("Kennel WARDOGS/ClipHound");
		if (k != std::string::npos)
			appPath.replace(k, std::string("Kennel WARDOGS/ClipHound").size(), "Kennel.gg/ClipHound");
		k = appPath.find("Kennel WARDOGS\\ClipHound");
		if (k != std::string::npos)
			appPath.replace(k, std::string("Kennel WARDOGS\\ClipHound").size(), "Kennel.gg\\ClipHound");
	}
	// once more, and this time for everyone: nothing of yours is muted when the POV changes, and no
	// sound is taken from the squad mate's feed. Both are still there to tick on.
	if (!audioDefaults3) {
		audioDefaults3 = true;
		muteWhileDowned.clear();
		audioAutoPicked = true;
		friendAudio = false;
	}
	// The auto-pick's "done that" flag was never written to disk, so on every start with an empty
	// list it ticked the desktop audio again. Anything in the list now may be its doing: clear it
	// once more, for the last time. The auto-pick itself is gone.
	if (!audioDefaults4) {
		audioDefaults4 = true;
		muteWhileDowned.clear();
		audioAutoPicked = true;
	}
	// NDI is shelved: LAN discovery, the share and auto-adding are off and out of the way until
	// they are ready. Squad mates already set up as NDI keep working.
	if (!ndiShelved) {
		ndiShelved = true;
		lanEnabled = false;
		ndiShare = false;
		autoAddPeers = false;
	}
	// the file is named with the same plain-English title as the Twitch clip; only a template
	// someone typed themselves is left alone
	if (clipNameTemplate == "{date}_{time}_{tags}" || clipNameTemplate == "{title}_{tags}_{date}_{time}")
		clipNameTemplate = "{title} - {date} {time}";
	obs_data_release(d);
}

void Config::save() const
{
	std::string dir = configDir();
	if (!dir.empty())
		os_mkdirs(dir.c_str());
	obs_data_t *d = obs_data_create();
	SETS(gameSource);
	SETS(sceneName);
	SETI(activeFriend);
	SETB(bringToFront);
	SETB(keepWarm);
	SETB(preloadFeeds);
	SETB(friendAudio);
	SETB(audioDefaults2);
	SETB(audioDefaults3);
	SETB(ndiShelved);
	SETB(discordShared1);
	SETB(audioDefaults4);
	SETB(audioAutoPicked);
	SETB(rosterEnabled);
	SETS(rosterUrl);
	SETS(myDiscord);
	SETS(rosterChannel);
	SETI(rosterPollS);
	SETB(rosterAddSources);
	SETB(popoutTuck);
	SETI(popoutMonitor);
	SETS(sceneV);
	SETI(vdoBitrateKbps);
	SETB(dualEnabled);
	SETI(dualFriend);
	SETS(dualPreset);
	SETD(dualX);
	SETD(dualY);
	SETD(dualW);
	SETI(dualOpacity);
	SETB(dualAuto);
	SETB(dualKeep);
	SETB(dualLook);
	SETD(vehX);
	SETD(vehY);
	SETD(vehW);
	SETD(vehH);
	SETB(updateCheck);
	SETS(updateUrl);
	SETS(updateSkip);
	SETB(lookName);
	SETB(lookPlate);
	SETB(lookCam);
	SETB(lookGrain);
	SETB(lookVignette);
	SETS(lookLabel);
	SETI(grainAmount);
	SETB(autoDetect);
	SETB(enabled);
	SETD(threshold);
	SETB(thresholdV2);
	SETB(onTopV1);
	SETB(warmNdi);
	SETS(lookPos);
	SETI(ndiShareHeight);
	SETI(ndiShareFps);
	SETB(ndiShareV1);
	SETB(ndiShareV2);
	SETB(lookPosV1);
	SETD(holdDrop);
	SETB(holdV2);
	SETI(pollMs);
	SETI(downFrames);
	SETI(upFrames);
	SETI(minDownMs);
	SETI(downDelayMs);
	SETI(upDelayMs);
	SETD(releaseDrop);
	SETD(memScale);
	SETD(memX);
	SETD(memY);
	SETB(watchRevive);
	SETI(bridgePort);
	SETB(bridgeEnabled);
	SETS(appPath);
	SETB(launchApp);
	SETB(closeAppWithObs);
	SETS(clipFolder);
	SETS(appPlayerName);
	SETS(appLibrary);
	SETS(appBroadcaster);
	SETB(appTwitchEnabled);
	SETB(appEveryKill);
	SETD(feedX);
	SETD(feedY);
	SETD(feedW);
	SETD(feedH);
	SETD(appMultikillWindow);
	SETI(appFps);
	SETB(nearEnabled);
	SETB(nearFollow);
	SETD(nearX);
	SETD(nearY);
	SETD(nearW);
	SETD(nearH);
	SETI(nearMarginM);
	SETI(nearCooldownS);
	SETI(nearMaxM);
	SETI(nearTtlS);
	SETB(appConfigDirty);
	SETS(clipNameTemplate);
	SETB(autoStartReplay);
	SETI(replaySeconds);
	SETB(clipOnDowned);
	SETB(clipUseReplay);
	SETS(backtrackFolder);
	SETS(playerName);
	SETB(lanEnabled);
	SETI(lanPort);
	SETB(ndiShare);
	SETB(autoAddPeers);
	SETD(reviveThreshold);
	SETB(wideSearch);
	SETD(customTemplateWidthFrac);
	SETD(boxX);
	SETD(boxY);
	SETD(boxW);
	SETD(boxH);
	setStrings(d, "muteWhileDowned", muteWhileDowned);
	setStrings(d, "onTop", onTop);
	setStrings(d, "clipHotkeys", clipHotkeys);
	obs_data_array_t *arr = obs_data_array_create();
	for (auto &f : friends) {
		obs_data_t *it = obs_data_create();
		obs_data_set_string(it, "name", f.name.c_str());
		obs_data_set_int(it, "kind", (int)f.kind);
		obs_data_set_string(it, "source", f.source.c_str());
		obs_data_set_string(it, "audioSource", f.audioSource.c_str());
		obs_data_set_string(it, "channel", f.channel.c_str());
		obs_data_set_string(it, "gameName", f.gameName.c_str());
		obs_data_set_bool(it, "trim", f.trim);
		obs_data_set_bool(it, "fromRoster", f.fromRoster);
		obs_data_set_string(it, "handle", f.handle.c_str());
		obs_data_set_string(it, "popout", f.popout.c_str());
		obs_data_set_string(it, "baseSource", f.baseSource.c_str());
		obs_data_set_int(it, "ndiBw", f.ndiBw);
		obs_data_set_int(it, "ndiSync", f.ndiSync);
		obs_data_set_int(it, "vdoHeight", f.vdoHeight);
		obs_data_set_int(it, "vdoFps", f.vdoFps);
		obs_data_set_int(it, "vdoKbps", f.vdoKbps);
		obs_data_set_string(it, "vdoCodec", f.vdoCodec.c_str());
		obs_data_array_push_back(arr, it);
		obs_data_release(it);
	}
	obs_data_set_array(d, "friends", arr);
	obs_data_array_release(arr);
	std::string path = configFile("config.json");
	if (!obs_data_save_json_safe(d, path.c_str(), "tmp", "bak"))
		obs_log(LOG_WARNING, "could not save %s", path.c_str());
	obs_data_release(d);
}
