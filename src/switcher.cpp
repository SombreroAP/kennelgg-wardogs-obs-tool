#include "switcher.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <algorithm>
#include <cctype>

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
		return "https://vdo.ninja/?view=" + urlEncode(f.channel) + "&solo&cleanoutput&autostart&noaudio=0";
	std::string ch = lower(f.channel);
	if (!ch.empty() && ch[0] == '@')
		ch.erase(0, 1);
	return "https://player.twitch.tv/?channel=" + urlEncode(ch) + "&parent=twitch.tv&muted=false&autoplay=true";
}

std::string Switcher::vdoPushUrl(const std::string &id)
{
	return "https://vdo.ninja/?push=" + urlEncode(id) +
	       "&screenshare&audiodevice=0&quality=0&stereo&label=" + urlEncode(id);
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
		obs_sceneitem_t *item = obs_scene_find_source(scene, Config::overlaySourceName());
		if (item)
			obs_sceneitem_set_visible(item, false);
	}
	obs_source_release(ss);
	return err;
}

void Switcher::armWarm(const Config &cfg)
{
	const Friend *f = cfg.active();
	if (!f)
		return;
	std::string name = Config::sourceFor(*f);
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return;
	obs_scene_t *scene = obs_scene_from_source(ss);
	if (f->isWeb())
		ensureBrowserSource(scene, name.c_str(), webUrl(*f), true);
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
		obs_sceneitem_set_visible(item, true);
	} else if (log)
		log("Warm feed: '" + name + "' is not in the scene.");
	if (src)
		obs_source_release(src);
	obs_source_release(ss);
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
	std::string name = Config::sourceFor(*f);

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
					obs_source_set_muted(src, false);
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
				obs_sceneitem_set_visible(item, on);
			}
		}
		if (src)
			obs_source_release(src);
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
