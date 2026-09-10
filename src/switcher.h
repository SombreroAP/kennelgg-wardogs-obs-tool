#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <obs.h>
#include "capture.h"
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
	void armOne(const Config &cfg, const Friend &f);
	/// The dual-POV window: a squad mate's feed, small, over your own POV. Creates what it needs.
	std::string applyDual(const Config &cfg, bool on);
	void shutdown(); // on OBS exit, before modules unload
	/// Look overlay on/off (also used for preview).
	std::string updateLook(const Config &cfg, bool on);

	static std::vector<std::string> sceneNames();
	/// Scene items in the plugin's scene, topmost first: name and source type.
	std::vector<std::pair<std::string, std::string>> sceneItems(const Config &cfg);
	/// Crop a Discord window capture down to the picture inside it. "" or a problem.
	std::string trimToContent(const Config &cfg, const Friend &f);
	/// Put the streamer's own camera and alerts back over the top of everything we add.
	void raiseOnTop(const Config &cfg);
	/// A first guess at what belongs on top: cameras, and anything that looks like alerts.
	std::vector<std::string> guessOnTop(const Config &cfg);
	/// Hide every scene item of this source in every scene (belt and braces for the way back).
	static int hideEverywhere(const std::string &sourceName);
	int hideAllFriends(const Config &cfg);
	/// Choices a source kind offers for one of its list properties (e.g. window_capture "window").
	/// NDI senders already used by a source in this OBS.
	static std::vector<std::string> ndiSourceNames();
	static std::vector<std::pair<std::string, std::string>> listProperty(const char *kind,
									     const char *prop); // name, value
	static bool kindAvailable(const char *kind);
	/// Create an input of this kind (or reuse one with the name), put it in the scene, optionally full-canvas. Returns "" or an error.
	std::string createInScene(const Config &cfg, const char *kind, const std::string &name, obs_data_t *settings,
				  bool fullCanvas, bool visible, bool toBottom = false);
	/// Sources a squad mate needs, created and placed. Fills f.source / f.audioSource.
	std::string createFriendSources(const Config &cfg, Friend &f);
	std::string createGameCapture(Config &cfg);
	/// Receiving settings for a squad mate's NDI feed (frame sync on, their bandwidth choice).
	static obs_data_t *ndiSettings(const Friend &f); // caller releases
	/// Apply those to the NDI feeds already in OBS.
	void tuneNdiSources(const Config &cfg);
	/// Publish the program feed over NDI (DistroAV's output type) on mixer track 6, with every microphone
	/// input taken off that track so squad mates get game audio only. Returns "" or an error.
	std::string startNdiShare(const std::string &ndiName, int shareHeight, int shareFps);
	void stopNdiShare();
	static bool outputKindAvailable(const char *kind);
	static std::string ndiFullName(const std::string &host, const std::string &ndiName)
	{
		return host + " (" + ndiName + ")";
	}

private:
	int shareHeight_ = 0, shareFps_ = 0; // what the NDI share is scaled to
	Capture trimCap_;                    // renders a frame of a feed to find its borders
	obs_scene_t *dualScene_ = nullptr;   // the private nested scene behind the dual-POV window
	obs_output_t *ndiOut_ = nullptr;
	obs_view_t *ndiView_ = nullptr; // our own render of the program, so the share never taps the main mix
	video_t *ndiVideo_ = nullptr;

public:
	static std::vector<std::pair<std::string, std::string>> inputs(); // name, id
	static std::string webUrl(const Friend &f);
	static std::string vdoPushUrl(const Friend &f);
	static std::string overlayUrl(const Config &cfg, const std::string &friendName);

private:
	std::map<std::string, bool> prevMute_;
	obs_source_t *sceneSource(const Config &cfg); // +ref
	std::string ensureBrowserSource(obs_scene_t *scene, const char *name, const std::string &url,
					bool rerouteAudio);
	std::string ensureHideFilter(obs_source_t *src);
	static void moveToTop(obs_sceneitem_t *item) { obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP); }
};
