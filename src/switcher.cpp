#include "switcher.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <algorithm>
#include <cctype>
#include <cstring>

static std::string urlEncode(const std::string &s)
{
	static const char *hex = "0123456789ABCDEF";
	std::string o;
	for (unsigned char c : s) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			o += (char)c;
		else {
			o += '%';
			o += hex[c >> 4];
			o += hex[c & 15];
		}
	}
	return o;
}

static std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
	return s;
}

std::string Switcher::webUrl(const Friend &f)
{
	if (f.kind == FriendKind::VdoNinja)
		// the viewer asks for the friend's chosen quality; WebRTC settles lower by itself on a weak link
		return "https://vdo.ninja/?view=" + urlEncode(f.channel) +
		       "&solo&cleanoutput&autostart&noaudio=0&maxvideobitrate=" + std::to_string(f.vdoKbps) +
		       "&codec=" + f.vdoCodec + "&scale=100&buffer=0&height=" + std::to_string(f.vdoHeight) +
		       "&framerate=" + std::to_string(f.vdoFps);
	std::string ch = lower(f.channel);
	if (!ch.empty() && ch[0] == '@')
		ch.erase(0, 1);
	return "https://player.twitch.tv/?channel=" + urlEncode(ch) + "&parent=twitch.tv&muted=false&autoplay=true";
}

std::string Switcher::vdoPushUrl(const Friend &f)
{
	// what the friend opens: share the game window/screen with system audio, no mic, at the chosen quality
	int w = f.vdoHeight * 16 / 9;
	return "https://vdo.ninja/?push=" + urlEncode(f.channel) +
	       "&screenshare&audiodevice=0&quality=0&stereo&maxvideobitrate=" + std::to_string(f.vdoKbps) +
	       "&height=" + std::to_string(f.vdoHeight) + "&width=" + std::to_string(w) +
	       "&framerate=" + std::to_string(f.vdoFps) + "&codec=" + f.vdoCodec + "&label=" + urlEncode(f.channel);
}

std::string Switcher::overlayUrl(const Config &cfg, const std::string &friendName)
{
	char *p = obs_module_file("overlay/overlay.html");
	std::string path = p ? p : "";
	bfree(p);
	std::replace(path.begin(), path.end(), '\\', '/');
	std::string q;
	if (cfg.lookName) {
		q += "name=" + urlEncode(friendName.empty() ? "friend" : friendName) +
		     "&label=" + urlEncode(cfg.lookLabel);
		if (cfg.lookPlate)
			q += "&plate=1";
	}
	if (cfg.lookCam)
		q += "&cam=1";
	if (cfg.lookGrain)
		q += "&grain=" + std::to_string(std::clamp(cfg.grainAmount, 0, 100));
	if (cfg.lookVignette)
		q += "&vig=1";
	if (!q.empty() && q[0] == '&')
		q.erase(0, 1);
	return "file:///" + path + "?" + q;
}

std::vector<std::string> Switcher::sceneNames()
{
	std::vector<std::string> out;
	char **names = obs_frontend_get_scene_names();
	if (!names)
		return out;
	for (char **n = names; *n; n++)
		out.emplace_back(*n);
	bfree(names);
	return out;
}

std::vector<std::pair<std::string, std::string>> Switcher::inputs()
{
	std::vector<std::pair<std::string, std::string>> out;
	obs_enum_sources(
		[](void *data, obs_source_t *src) {
			auto *o = (std::vector<std::pair<std::string, std::string>> *)data;
			if (obs_source_get_type(src) == OBS_SOURCE_TYPE_INPUT)
				o->emplace_back(obs_source_get_name(src), obs_source_get_id(src));
			return true;
		},
		&out);
	std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return lower(a.first) < lower(b.first); });
	return out;
}

std::vector<std::pair<std::string, std::string>> Switcher::listProperty(const char *kind, const char *prop)
{
	std::vector<std::pair<std::string, std::string>> out;
	obs_source_t *tmp = obs_source_create_private(kind, "kennel-probe", nullptr);
	if (!tmp)
		return out;
	obs_properties_t *props = obs_source_properties(tmp);
	obs_property_t *p = props ? obs_properties_get(props, prop) : nullptr;
	if (p && obs_property_get_type(p) == OBS_PROPERTY_LIST) {
		size_t n = obs_property_list_item_count(p);
		for (size_t i = 0; i < n; i++) {
			const char *name = obs_property_list_item_name(p, i),
				   *val = obs_property_list_item_string(p, i);
			if (name && val && *val)
				out.emplace_back(name, val);
		}
	}
	if (props)
		obs_properties_destroy(props);
	obs_source_release(tmp);
	return out;
}

bool Switcher::kindAvailable(const char *kind)
{
	const char *id;
	for (size_t i = 0; obs_enum_input_types(i, &id); i++)
		if (strcmp(id, kind) == 0)
			return true;
	return false;
}

std::string Switcher::createInScene(const Config &cfg, const char *kind, const std::string &name, obs_data_t *settings,
				    bool fullCanvas, bool visible, bool toBottom)
{
	if (!kindAvailable(kind))
		return std::string("source type '") + kind + "' is not available in this OBS";
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return "no scene";
	obs_scene_t *scene = obs_scene_from_source(ss);
	obs_source_t *src = obs_get_source_by_name(name.c_str());
	if (src) {
		if (settings)
			obs_source_update(src, settings);
	} else {
		src = obs_source_create(kind, name.c_str(), settings, nullptr);
		if (!src) {
			obs_source_release(ss);
			return "could not create '" + name + "'";
		}
		if (log)
			log("Added '" + name + "' to OBS.");
	}
	obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
	if (!item)
		item = obs_scene_add(scene, src);
	if (item) {
		obs_sceneitem_set_visible(item, visible);
		if (fullCanvas) {
			struct obs_video_info ovi;
			obs_get_video_info(&ovi);
			struct vec2 pos = {0, 0}, bounds = {(float)ovi.base_width, (float)ovi.base_height};
			obs_sceneitem_set_pos(item, &pos);
			obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds(item, &bounds);
		}
		if (toBottom)
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);
	}
	obs_source_release(src);
	obs_source_release(ss);
	return item ? "" : "could not add '" + name + "' to the scene";
}

std::string Switcher::createFriendSources(const Config &cfg, Friend &f)
{
	std::string base = "Kennel · " + (f.name.empty() ? std::string("squad mate") : f.name);
	if (f.kind == FriendKind::Discord) {
		// their popped-out Go Live window, captured with the Windows 10 method so it survives being covered
		obs_data_t *st = obs_data_create();
		obs_data_set_string(st, "window", f.channel.c_str());
		obs_data_set_int(st, "method", 2);
		obs_data_set_int(st, "priority",
				 f.channel.find("Discord.exe") != std::string::npos ? 2 : 1); // 2 = match by executable
		obs_data_set_bool(st, "cursor", false);
		obs_data_set_bool(st, "client_area", true);
		std::string e = createInScene(cfg, "window_capture", base, st, true, false);
		obs_data_release(st);
		if (!e.empty())
			return e;
		f.source = base;
		// Discord's audio (their game sound), matched by executable so it follows any Discord window
		obs_data_t *au = obs_data_create();
		obs_data_set_string(au, "window", f.channel.c_str());
		obs_data_set_int(au, "priority", 2);
		e = createInScene(cfg, "wasapi_process_output_capture", base + " audio", au, false, false);
		obs_data_release(au);
		if (!e.empty())
			return e;
		f.audioSource = base + " audio";
		return "";
	}
	if (f.kind == FriendKind::Ndi) {
		if (!kindAvailable("ndi_source"))
			return "the NDI source type is missing: install DistroAV (obs-ndi) first";
		obs_data_t *st = obs_data_create();
		obs_data_set_string(st, "ndi_source_name", f.channel.c_str());
		obs_data_set_int(st, "ndi_bw_mode", 0);
		std::string e = createInScene(cfg, "ndi_source", base, st, true, false);
		obs_data_release(st);
		if (!e.empty())
			return e;
		f.source = base;
		return "";
	}
	return "";
}

std::string Switcher::createGameCapture(Config &cfg)
{
	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "capture_mode", "any_fullscreen");
	obs_data_set_bool(st, "capture_audio", false);
	std::string e = createInScene(cfg, "game_capture", "Game", st, true, true, true);
	obs_data_release(st);
	if (e.empty())
		cfg.gameSource = "Game";
	return e;
}

bool Switcher::outputKindAvailable(const char *kind)
{
	const char *id;
	for (size_t i = 0; obs_enum_output_types(i, &id); i++)
		if (strcmp(id, kind) == 0)
			return true;
	return false;
}

std::string Switcher::startNdiShare(const std::string &ndiName)
{
	if (!outputKindAvailable("ndi_output"))
		return "DistroAV (obs-ndi) is not installed, so the game feed cannot be shared over NDI";
	const uint32_t track6 = 1u << 5;
	// microphones off track 6: squad mates should hear the game, not the streamer
	obs_enum_sources(
		[](void *, obs_source_t *src) {
			const char *id = obs_source_get_id(src);
			if (id && strstr(id, "input_capture")) {
				uint32_t m = obs_source_get_audio_mixers(src);
				if (m & (1u << 5))
					obs_source_set_audio_mixers(src, m & ~(1u << 5));
			}
			return true;
		},
		nullptr);
	if (ndiOut_) {
		obs_data_t *cur = obs_output_get_settings(ndiOut_);
		std::string curName = obs_data_get_string(cur, "ndi_name");
		obs_data_release(cur);
		if (curName == ndiName && obs_output_active(ndiOut_))
			return "";
		stopNdiShare();
	}
	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "ndi_name", ndiName.c_str());
	obs_data_set_bool(st, "uses_video", true);
	obs_data_set_bool(st, "uses_audio", true);
	ndiOut_ = obs_output_create("ndi_output", "Kennel NDI share", st, nullptr);
	obs_data_release(st);
	if (!ndiOut_)
		return "could not create the NDI output";
	obs_output_set_media(ndiOut_, obs_get_video(), obs_get_audio());
	obs_output_set_mixer(ndiOut_, 5);
	if (!obs_output_start(ndiOut_)) {
		const char *e = obs_output_get_last_error(ndiOut_);
		std::string err = e ? e : "NDI output would not start";
		obs_output_release(ndiOut_);
		ndiOut_ = nullptr;
		return err;
	}
	(void)track6;
	if (log)
		log("Sharing your feed over NDI as \"" + ndiName + "\" (game audio only, track 6).");
	return "";
}

void Switcher::stopNdiShare()
{
	if (!ndiOut_)
		return;
	if (obs_output_active(ndiOut_))
		obs_output_stop(ndiOut_);
	obs_output_release(ndiOut_);
	ndiOut_ = nullptr;
}

obs_source_t *Switcher::sceneSource(const Config &cfg)
{
	if (!cfg.sceneName.empty()) {
		obs_source_t *s = obs_get_source_by_name(cfg.sceneName.c_str());
		if (s)
			return s;
	}
	return obs_frontend_get_current_scene();
}

std::string Switcher::ensureBrowserSource(obs_scene_t *scene, const char *name, const std::string &url,
					  bool rerouteAudio)
{
	struct obs_video_info ovi;
	obs_get_video_info(&ovi);
	obs_sceneitem_t *item = obs_scene_find_source(scene, name);
	obs_source_t *src = obs_get_source_by_name(name);
	if (!src) {
		obs_data_t *st = obs_data_create();
		obs_data_set_string(st, "url", url.c_str());
		obs_data_set_int(st, "width", ovi.base_width);
		obs_data_set_int(st, "height", ovi.base_height);
		obs_data_set_bool(st, "reroute_audio", rerouteAudio);
		obs_data_set_bool(st, "shutdown", false);
		obs_data_set_bool(st, "restart_when_active", false);
		src = obs_source_create("browser_source", name, st, nullptr);
		obs_data_release(st);
		if (!src)
			return std::string("could not create browser source '") + name +
			       "' (is the Browser Source available in this OBS?)";
		if (log)
			log(std::string("Added browser source '") + name + "'.");
	} else {
		obs_data_t *cur = obs_source_get_settings(src);
		std::string curUrl = obs_data_get_string(cur, "url");
		obs_data_release(cur);
		if (curUrl != url) {
			obs_data_t *st = obs_data_create();
			obs_data_set_string(st, "url", url.c_str());
			obs_data_set_int(st, "width", ovi.base_width);
			obs_data_set_int(st, "height", ovi.base_height);
			obs_data_set_bool(st, "reroute_audio", rerouteAudio);
			obs_data_set_bool(st, "shutdown", false);
			obs_source_update(src, st);
			obs_data_release(st);
		}
	}
	if (!item) {
		item = obs_scene_add(scene, src);
		if (item) {
			obs_sceneitem_set_visible(item, false);
			struct vec2 pos = {0, 0}, bounds = {(float)ovi.base_width, (float)ovi.base_height};
			obs_sceneitem_set_pos(item, &pos);
			obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds(item, &bounds);
		}
	}
	obs_source_release(src);
	return item ? "" : std::string("could not add '") + name + "' to the scene";
}

std::string Switcher::ensureHideFilter(obs_source_t *src)
{
	obs_source_t *f = obs_source_get_filter_by_name(src, Config::hideFilterName());
	if (f) {
		obs_source_release(f);
		return "";
	}
	obs_data_t *st = obs_data_create();
	obs_data_set_double(st, "opacity", 0.0);
	f = obs_source_create_private("color_filter_v2", Config::hideFilterName(), st);
	obs_data_release(st);
	if (!f)
		return "could not create the hide filter";
	obs_source_filter_add(src, f);
	obs_source_release(f);
	return "";
}

int Switcher::hideEverywhere(const std::string &sourceName)
{
	struct Ctx {
		const std::string *name;
		int hidden = 0;
	} ctx{&sourceName};
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
		if (!scene)
			continue;
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *data) {
				auto *c = (Ctx *)data;
				obs_source_t *src = obs_sceneitem_get_source(item);
				if (src && *c->name == obs_source_get_name(src) && obs_sceneitem_visible(item)) {
					obs_sceneitem_set_visible(item, false);
					c->hidden++;
				}
				return true;
			},
			&ctx);
	}
	obs_frontend_source_list_free(&scenes);
	return ctx.hidden;
}

std::string Switcher::updateLook(const Config &cfg, bool on)
{
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return "no scene";
	obs_scene_t *scene = obs_scene_from_source(ss);
	std::string err;
	if (on) {
		const Friend *f = cfg.active();
		err = ensureBrowserSource(scene, Config::overlaySourceName(), overlayUrl(cfg, f ? f->name : ""), false);
		obs_sceneitem_t *item = obs_scene_find_source(scene, Config::overlaySourceName());
		if (item) {
			moveToTop(item);
			obs_sceneitem_set_visible(item, true);
		}
	} else {
		int n = hideEverywhere(Config::overlaySourceName());
		if (log && n > 1)
			log("Look overlay: hid " + std::to_string(n) + " scene items (it was in more than one place).");
	}
	obs_source_release(ss);
	return err;
}

void Switcher::armWarm(const Config &cfg)
{
	if (cfg.preloadFeeds) {
		// every squad mate's feed loaded and playing behind the scenes, so a swap is instant
		for (const auto &f : cfg.friends)
			armOne(cfg, f);
		// the shared browser source is not used in this mode; make sure it is not left showing
		hideEverywhere(Config::webSourceName());
		return;
	}
	const Friend *f = cfg.active();
	if (f)
		armOne(cfg, *f);
	// per-squad-mate sources from a previous preloading session must not sit on top of the scene
	for (const auto &other : cfg.friends)
		if (other.isWeb())
			hideEverywhere(std::string(Config::webSourceName()) + " - " + other.name);
}

/// One feed in warm mode: in the scene, playing, fully transparent and muted.
void Switcher::armOne(const Config &cfg, const Friend &f)
{
	std::string name = cfg.sourceFor(f);
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return;
	obs_scene_t *scene = obs_scene_from_source(ss);
	if (f.isWeb())
		ensureBrowserSource(scene, name.c_str(), webUrl(f), true);
	obs_source_t *src = obs_get_source_by_name(name.c_str());
	obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
	if (src && item) {
		ensureHideFilter(src);
		obs_source_t *hf = obs_source_get_filter_by_name(src, Config::hideFilterName());
		if (hf) {
			obs_source_set_enabled(hf, true);
			obs_source_release(hf);
		}
		obs_source_set_muted(src, true);
		obs_sceneitem_set_visible(item, false); // browser sources keep running while hidden (shutdown off)
	} else if (log)
		log("Warm feed: '" + name + "' is not in the scene.");
	if (!f.audioSource.empty()) {
		obs_source_t *a = obs_get_source_by_name(f.audioSource.c_str());
		obs_sceneitem_t *ai = obs_scene_find_source(scene, f.audioSource.c_str());
		if (a && ai) {
			obs_source_set_muted(a, true);
			obs_sceneitem_set_visible(ai, false);
		}
		if (a)
			obs_source_release(a);
	}
	if (src)
		obs_source_release(src);
	obs_source_release(ss);
}

/// Your own POV, and nothing else: every squad mate's video and audio hidden in every scene.
int Switcher::hideAllFriends(const Config &cfg)
{
	int n = hideEverywhere(Config::webSourceName());
	for (const auto &f : cfg.friends) {
		if (f.isWeb())
			n += hideEverywhere(std::string(Config::webSourceName()) + " - " + f.name);
		else if (!f.source.empty())
			n += hideEverywhere(f.source);
		if (!f.audioSource.empty())
			n += hideEverywhere(f.audioSource);
		std::string nm = cfg.sourceFor(f);
		obs_source_t *src = obs_get_source_by_name(nm.c_str());
		if (src) {
			obs_source_set_muted(src, true);
			obs_source_release(src);
		}
	}
	n += hideEverywhere(Config::overlaySourceName());
	return n;
}

std::vector<std::string> Switcher::apply(const Config &cfg, bool on)
{
	std::vector<std::string> errors;
	const Friend *f = cfg.active();
	if (!f) {
		errors.push_back("no squad mate set");
		return errors;
	}
	obs_source_t *ss = sceneSource(cfg);
	if (!ss) {
		errors.push_back("no scene");
		return errors;
	}
	obs_scene_t *scene = obs_scene_from_source(ss);
	std::string name = cfg.sourceFor(*f);

	// 1. the friend's video and its audio
	{
		if (f->isWeb() && on) {
			std::string e = ensureBrowserSource(scene, name.c_str(), webUrl(*f), true);
			if (!e.empty())
				errors.push_back(e);
		}
		obs_source_t *src = obs_get_source_by_name(name.c_str());
		obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
		if (!src || !item) {
			errors.push_back("friend source '" + name + "' is not in scene '" + obs_source_get_name(ss) +
					 "'");
		} else {
			if (on && cfg.bringToFront)
				moveToTop(item);
			if (cfg.keepWarm) {
				ensureHideFilter(src);
				obs_source_t *hf = obs_source_get_filter_by_name(src, Config::hideFilterName());
				if (on) {
					if (hf)
						obs_source_set_enabled(hf, false);
					obs_source_set_muted(src, !cfg.friendAudio);
					obs_sceneitem_set_visible(item, true);
				} else {
					obs_source_set_muted(src, true);
					if (hf)
						obs_source_set_enabled(hf, true);
					obs_sceneitem_set_visible(item, true);
				}
				if (hf)
					obs_source_release(hf);
			} else {
				obs_source_t *hf = obs_source_get_filter_by_name(src, Config::hideFilterName());
				if (hf) {
					obs_source_set_enabled(hf, false);
					obs_source_release(hf);
				}
				obs_source_set_muted(src, !(on && cfg.friendAudio));
				obs_sceneitem_set_visible(item, on);
				if (!on)
					hideEverywhere(name);
			}
		}
		if (src)
			obs_source_release(src);
	}

	// 1b. a companion audio source (Discord): follows the same show/hide, mute-based in warm mode
	if (!f->audioSource.empty()) {
		obs_source_t *a = obs_get_source_by_name(f->audioSource.c_str());
		obs_sceneitem_t *ai = obs_scene_find_source(scene, f->audioSource.c_str());
		if (a && ai) {
			if (cfg.keepWarm) {
				obs_sceneitem_set_visible(ai, true);
				obs_source_set_muted(a, !(on && cfg.friendAudio));
			} else
				obs_sceneitem_set_visible(ai, on);
		}
		if (a)
			obs_source_release(a);
	}

	// 2. the look overlay, above the friend
	{
		std::string e = updateLook(cfg, on && cfg.lookEnabled());
		if (!e.empty())
			errors.push_back("look: " + e);
	}

	// 3. game audio: mute on the way down, restore the exact previous state on the way up
	for (auto &input : cfg.muteWhileDowned) {
		obs_source_t *src = obs_get_source_by_name(input.c_str());
		if (!src) {
			errors.push_back("audio '" + input + "' not found");
			continue;
		}
		if (on) {
			if (!prevMute_.count(input))
				prevMute_[input] = obs_source_muted(src);
			obs_source_set_muted(src, true);
		} else {
			bool prev = prevMute_.count(input) ? prevMute_[input] : false;
			obs_source_set_muted(src, prev);
			prevMute_.erase(input);
		}
		obs_source_release(src);
	}
	obs_source_release(ss);
	return errors;
}
