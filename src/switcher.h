#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <obs.h>
#include "config.h"

/// Everything POVBridge does to OBS: show/hide the friend, the look overlay, mute game audio.
/// Call on the UI thread.
class Switcher {
public:
	std::function<void(const std::string &)> log;

	/// Show the active squad mate (on) or go back to the streamer's own POV. Returns problems, if any.
	std::vector<std::string> apply(const Config &cfg, bool on);
	/// Warm mode: friend source present in the scene, transparent and muted.
	void armWarm(const Config &cfg);
	/// Look overlay on/off (also used for preview).
	std::string updateLook(const Config &cfg, bool on);

	static std::vector<std::string> sceneNames();
	static std::vector<std::pair<std::string, std::string>> inputs(); // name, id
	static std::string webUrl(const Friend &f);
	static std::string vdoPushUrl(const std::string &id);
	static std::string overlayUrl(const Config &cfg, const std::string &friendName);

private:
	std::map<std::string, bool> prevMute_;
	obs_source_t *sceneSource(const Config &cfg); // +ref
	std::string ensureBrowserSource(obs_scene_t *scene, const char *name, const std::string &url, bool rerouteAudio);
	std::string ensureHideFilter(obs_source_t *src);
	static void moveToTop(obs_sceneitem_t *item) { obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP); }
};
