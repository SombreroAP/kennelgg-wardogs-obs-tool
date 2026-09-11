#include "switcher.h"
#include "ndi.h"
#include <util/platform.h>
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
	if (f.kind == FriendKind::Kick) {
		std::string ch = lower(f.channel);
		if (!ch.empty() && ch[0] == '@')
			ch.erase(0, 1);
		return "https://player.kick.com/" + urlEncode(ch) + "?autoplay=true&muted=false";
	}
	if (f.kind == FriendKind::YouTube) {
		// channel is a channel ID (UC...) - then the channel's current live stream - or a video ID
		std::string id = f.channel;
		if (id.rfind("UC", 0) == 0 && id.size() >= 20)
			return "https://www.youtube.com/embed/live_stream?channel=" + urlEncode(id) +
			       "&autoplay=1&mute=0";
		return "https://www.youtube.com/embed/" + urlEncode(id) + "?autoplay=1&mute=0";
	}
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
		q += "&pos=" + urlEncode(cfg.lookPos.empty() ? "ml" : cfg.lookPos);
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
	// Cached for a few seconds: this creates a real source of that kind for a moment, and a second
	// window capture of the same window makes the first one flicker while it is up.
	static std::map<std::string, std::pair<uint64_t, std::vector<std::pair<std::string, std::string>>>> cache;
	std::string key = std::string(kind) + "/" + prop;
	uint64_t now = os_gettime_ns();
	auto c = cache.find(key);
	if (c != cache.end() && now - c->second.first < 5000000000ULL)
		return c->second.second;

	// DistroAV's finder keeps hold of whichever source asked for the list and signals it from its
	// own thread; our probe is gone by then and OBS dies on the dangling handler. Use kennelNdi.
	if (strncmp(kind, "ndi_", 4) == 0) {
		obs_log(LOG_WARNING, "listProperty refused for %s - use kennelNdi::sources()", kind);
		return {};
	}

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
	cache[key] = {now, out};
	return out;
}

/// NDI senders already referenced by a source in this OBS. Reads what is set on them; it never
/// creates one, so DistroAV's finder is not involved.
std::vector<std::string> Switcher::ndiSourceNames()
{
	std::vector<std::string> out;
	auto cb = [](void *param, obs_source_t *src) {
		auto *v = (std::vector<std::string> *)param;
		const char *id = obs_source_get_id(src);
		if (id && strncmp(id, "ndi_", 4) == 0) {
			obs_data_t *st = obs_source_get_settings(src);
			const char *n = st ? obs_data_get_string(st, "ndi_source_name") : nullptr;
			if (n && *n && std::find(v->begin(), v->end(), n) == v->end())
				v->emplace_back(n);
			if (st)
				obs_data_release(st);
		}
		return true;
	};
	obs_enum_sources(cb, &out);
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

namespace {
/// True if every key we are about to write already holds that value. Updating a window capture
/// tears its capture down and starts it again - do that on every save and it flickers - and it is
/// pointless when nothing changed.
bool alreadySet(obs_source_t *src, obs_data_t *want)
{
	obs_data_t *have = obs_source_get_settings(src);
	if (!have)
		return false;
	bool same = true;
	for (obs_data_item_t *it = obs_data_first(want); it && same; obs_data_item_next(&it)) {
		const char *k = obs_data_item_get_name(it);
		switch (obs_data_item_gettype(it)) {
		case OBS_DATA_STRING: {
			const char *a = obs_data_item_get_string(it), *b = obs_data_get_string(have, k);
			same = a && b && strcmp(a, b) == 0;
			break;
		}
		case OBS_DATA_NUMBER:
			same = obs_data_item_numtype(it) == OBS_DATA_NUM_INT
				       ? obs_data_item_get_int(it) == obs_data_get_int(have, k)
				       : obs_data_item_get_double(it) == obs_data_get_double(have, k);
			break;
		case OBS_DATA_BOOLEAN:
			same = obs_data_item_get_bool(it) == obs_data_get_bool(have, k);
			break;
		default:
			same = false;
		}
	}
	obs_data_release(have);
	return same;
}
} // namespace

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
		if (settings && !alreadySet(src, settings))
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
	bool fresh = !item;
	if (!item)
		item = obs_scene_add(scene, src);
	if (item) {
		if (obs_sceneitem_visible(item) != visible)
			obs_sceneitem_set_visible(item, visible);
		// Only place it the first time. After that it is the streamer's to move, and re-applying
		// the full-canvas transform on every save snapped it back under them.
		if (fullCanvas && fresh) {
			struct obs_video_info ovi;
			obs_get_video_info(&ovi);
			struct vec2 pos = {0, 0}, bounds = {(float)ovi.base_width, (float)ovi.base_height};
			obs_sceneitem_set_pos(item, &pos);
			obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds(item, &bounds);
		}
		if (toBottom && fresh)
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);
	}
	obs_source_release(src);
	obs_source_release(ss);
	return item ? "" : "could not add '" + name + "' to the scene";
}

/// How a squad mate's NDI feed should be received. Frame sync is the important one: it hands OBS a
/// frame on OBS's own clock instead of whenever the network delivers one, which is what turns a
/// feed that judders and drops on a busy LAN into a steady one.
/// What a squad mate's feed is really called on the network.
///
/// The name we compose from their beacon is "<their computer> (Kennel POV)", but NDI advertises the
/// machine name in its own form - upper case, or the DNS name, or whatever the NDI runtime settled
/// on - and DistroAV matches the string exactly. One letter's difference and the receiver connects
/// to nothing and shows nothing, with no error anywhere. So we ask the NDI runtime what is actually
/// out there and match on the part in brackets, which is the half we control.
std::string Switcher::resolveNdiName(const std::string &wanted, std::vector<std::string> *sawOut)
{
	std::vector<std::string> have = kennelNdi::sources(1200);
	if (sawOut)
		*sawOut = have;
	if (have.empty())
		return wanted; // nothing to check against; leave it alone
	auto lower = [](std::string v) {
		std::transform(v.begin(), v.end(), v.begin(), ::tolower);
		return v;
	};
	auto inner = [](const std::string &v) {
		size_t a = v.rfind('('), b = v.rfind(')');
		return a != std::string::npos && b != std::string::npos && b > a ? v.substr(a + 1, b - a - 1) : v;
	};
	for (const auto &h : have)
		if (h == wanted)
			return h;
	for (const auto &h : have)
		if (lower(h) == lower(wanted))
			return h;
	std::string want = lower(inner(wanted));
	for (const auto &h : have)
		if (lower(inner(h)) == want)
			return h;
	return "";
}

obs_data_t *Switcher::ndiSettings(const Friend &f)
{
	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "ndi_source_name", f.channel.c_str());
	obs_data_set_int(st, "ndi_bw_mode", std::clamp(f.ndiBw, 0, 1));
	// How the feed is timed on the way in. 0 leaves DistroAV's own settings alone, which is the
	// default: 0.5.4 forced frame sync on every feed to smooth it, and on some setups the picture
	// then never appears at all. Frame sync is worth trying by hand, but not behind your back.
	switch (std::clamp(f.ndiSync, 0, 4)) {
	case 1:
		obs_data_set_bool(st, "ndi_framesync", true);
		break;
	case 2:
		obs_data_set_bool(st, "ndi_framesync", false);
		obs_data_set_int(st, "ndi_sync", 1); // network timestamps
		break;
	case 3:
		obs_data_set_bool(st, "ndi_framesync", false);
		obs_data_set_int(st, "ndi_sync", 2); // the sender's timecode
		break;
	case 4:
		obs_data_set_bool(st, "ndi_framesync", false);
		obs_data_set_int(st, "ndi_sync", 0); // internal: show frames as they land
		break;
	default:
		obs_data_set_bool(st, "ndi_framesync", false); // undo what 0.5.4 wrote onto the source
		break;
	}
	return st;
}

/// Put those settings on the NDI feeds that already exist, once, without touching anything else -
/// updating an ndi_source restarts the receiver, so only when something actually differs.
void Switcher::tuneNdiSources(Config &cfg)
{
	bool changed = false;
	for (auto &f : cfg.friends) {
		if (f.kind != FriendKind::Ndi || f.source.empty())
			continue;
		obs_source_t *src = obs_get_source_by_name(f.source.c_str());
		if (!src)
			continue;
		obs_data_t *st = ndiSettings(f);
		if (!alreadySet(src, st))
			obs_source_update(src, st);
		obs_data_release(st);
		obs_source_release(src);
	}
	if (changed)
		cfg.save();
}

/// The sources this plugin made for one squad mate, taken out of every scene and deleted. Only ever
/// ours: a squad mate set up as "an OBS source you already have" keeps their source, and the shared
/// browser source everyone uses when feeds are not preloaded is left alone.
std::vector<std::string> Switcher::friendSourceNames(const Config &cfg, const Friend &f)
{
	std::vector<std::string> names;
	if (f.ownsSources()) { // Discord and NDI: we created these
		if (!f.source.empty())
			names.push_back(f.source);
		if (!f.audioSource.empty())
			names.push_back(f.audioSource);
	}
	if (f.isWeb()) {
		std::string web = cfg.webSourceFor(f);
		if (web != Config::webSourceName()) // the per-squad-mate one, not the shared one
			names.push_back(web);
	}
	return names;
}

int Switcher::removeFriendSources(const Config &cfg, const Friend &f)
{
	int gone = 0;
	for (const auto &name : friendSourceNames(cfg, f)) {
		obs_source_t *src = obs_get_source_by_name(name.c_str());
		if (!src)
			continue;
		struct obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; i++) {
			obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
			if (obs_sceneitem_t *it = scene ? obs_scene_find_source(scene, name.c_str()) : nullptr)
				obs_sceneitem_remove(it);
		}
		obs_frontend_source_list_free(&scenes);
		obs_source_remove(src); // OBS lets it go once nothing holds it
		obs_source_release(src);
		gone++;
		if (log)
			log("Removed the source '" + name + "'.");
	}
	return gone;
}

/// Builds before 0.7.0 named everything "Kennel ..."; it is all "Kennel.gg ..." now. Rename what is
/// there rather than make it again, so nobody's scenes fill with duplicates. Returns how many moved.
int Switcher::migrateNames(Config &cfg)
{
	int moved = 0;
	auto rename = [&](const std::string &from, const std::string &to) {
		if (from == to)
			return;
		obs_source_t *src = obs_get_source_by_name(from.c_str());
		if (!src)
			return;
		obs_source_t *clash = obs_get_source_by_name(to.c_str());
		if (clash) { // both exist: leave the old one alone, the new one wins
			obs_source_release(clash);
			obs_source_release(src);
			return;
		}
		obs_source_set_name(src, to.c_str());
		obs_source_release(src);
		moved++;
	};
	rename("Kennel web", Config::webSourceName());
	rename("Kennel look", Config::overlaySourceName());
	rename("Kennel look (vertical)", Config::overlaySourceNameV());
	rename("Kennel dual", Config::dualSceneName());
	rename("Kennel dual feed", Config::dualFeedName());
	bool cfgChanged = false;
	for (auto &f : cfg.friends) {
		rename("Kennel web - " + f.name, std::string(Config::webSourceName()) + " - " + f.name);
		for (std::string *field : {&f.source, &f.audioSource}) {
			if (field->rfind("Kennel · ", 0) == 0) {
				std::string to = "Kennel.gg · " + field->substr(std::string("Kennel · ").size());
				rename(*field, to);
				*field = to;
				cfgChanged = true;
			}
		}
	}
	if (cfgChanged)
		cfg.save();
	if (moved && log)
		log("Renamed " + std::to_string(moved) + " source(s) from \"Kennel ...\" to \"Kennel.gg ...\".");
	return moved;
}

std::string Switcher::createFriendSources(const Config &cfg, Friend &f)
{
	std::string base = "Kennel.gg · " + (f.name.empty() ? std::string("squad mate") : f.name);
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
		std::vector<std::string> saw;
		std::string real = resolveNdiName(f.channel, &saw);
		if (real.empty()) {
			std::string list;
			for (const auto &h : saw)
				list += (list.empty() ? "" : ", ") + h;
			return "nothing on the network is publishing '" + f.channel + "'" +
			       (list.empty() ? " - and NDI cannot see any feed at all right now"
					     : " - NDI can see: " + list);
		}
		if (real != f.channel) {
			if (log)
				log("NDI: '" + f.channel + "' is really called '" + real + "' - using that.");
			f.channel = real;
		}
		obs_data_t *st = ndiSettings(f);
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

std::string Switcher::startNdiShare(const std::string &ndiName, int shareHeight, int shareFps)
{
	bool sizeChanged = shareHeight_ != shareHeight || shareFps_ != shareFps;
	shareHeight_ = shareHeight;
	shareFps_ = shareFps;
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
		if (curName == ndiName && obs_output_active(ndiOut_) && !sizeChanged)
			return "";
		stopNdiShare();
	}
	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "ndi_name", ndiName.c_str());
	obs_data_set_bool(st, "uses_video", true);
	obs_data_set_bool(st, "uses_audio", true);
	ndiOut_ = obs_output_create("ndi_output", "Kennel.gg NDI share", st, nullptr);
	obs_data_release(st);
	if (!ndiOut_)
		return "could not create the NDI output";
	// Render the share from a view of our own on the program output, never from OBS's main video
	// mix: an output tapped onto the mix broke other plugins' extra canvases (Aitum's vertical
	// canvas went black). The view shows what the program shows, follows scene changes by itself,
	// and only ever carries the main canvas.
	struct obs_video_info ovi;
	obs_get_video_info(&ovi);
	// Send a smaller picture than the canvas. NDI's full-quality stream is barely compressed:
	// 1440p60 is around 200 Mbit, which is more than most networks carry steadily, and that is what
	// makes a squad mate's feed judder. Our own view renders it, so the stream itself pays nothing.
	//
	// Only the output size is changed. The mix keeps the canvas size and - importantly - OBS's own
	// frame rate: a mix running at a different rate to the rest of OBS is what blacked out other
	// plugins' extra canvases (Aitum's vertical canvas) in 0.5.4.
	if (shareHeight_ > 0 && ovi.base_height > 0 && (uint32_t)shareHeight_ < ovi.base_height) {
		ovi.output_height = (uint32_t)shareHeight_;
		ovi.output_width = (uint32_t)(((uint64_t)ovi.base_width * shareHeight_ / ovi.base_height + 1) & ~1u);
		ovi.scale_type = OBS_SCALE_LANCZOS; // sharper than bicubic on a big downscale, costs nothing to send
	}
	ndiView_ = obs_view_create();
	obs_source_t *program = obs_get_output_source(0);
	obs_view_set_source(ndiView_, 0, program);
	if (program)
		obs_source_release(program);
	ndiVideo_ = obs_view_add2(ndiView_, &ovi);
	if (!ndiVideo_) {
		stopNdiShare();
		return "could not create a video output for the NDI share";
	}
	obs_output_set_media(ndiOut_, ndiVideo_, obs_get_audio());
	obs_output_set_mixer(ndiOut_, 5);
	if (!obs_output_start(ndiOut_)) {
		const char *e = obs_output_get_last_error(ndiOut_);
		ndiErr_ = e ? e : "the NDI output would not start";
		stopNdiShare();
		return ndiErr_;
	}
	ndiErr_.clear();
	ndiName_ = ndiName;
	(void)track6;
	if (log)
		log("Sharing your feed over NDI as \"" + ndiName + "\" at " + std::to_string(ovi.output_width) + "x" +
		    std::to_string(ovi.output_height) + " " +
		    std::to_string(ovi.fps_den ? ovi.fps_num / ovi.fps_den : 0) + " fps (game audio only).");
	return "";
}

void Switcher::stopNdiShare()
{
	if (ndiOut_) {
		if (obs_output_active(ndiOut_))
			obs_output_stop(ndiOut_);
		obs_output_release(ndiOut_);
		ndiOut_ = nullptr;
	}
	if (ndiView_) {
		obs_view_set_source(ndiView_, 0, nullptr);
		if (ndiVideo_)
			obs_view_remove(ndiView_);
		obs_view_destroy(ndiView_);
		ndiView_ = nullptr;
		ndiVideo_ = nullptr;
	}
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
					  bool rerouteAudio, int width, int height)
{
	struct obs_video_info ovi;
	obs_get_video_info(&ovi);
	if (width > 0 && height > 0) { // a canvas of another shape
		ovi.base_width = (uint32_t)width;
		ovi.base_height = (uint32_t)height;
	}
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
	if (!f) { // made by a build before 0.7.0: keep it, under its new name
		f = obs_source_get_filter_by_name(src, "Kennel hide");
		if (f)
			obs_source_set_name(f, Config::hideFilterName());
	}
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
	// every scene OBS has, not only the ones in the scene list: a vertical canvas's scenes are
	// real scenes too, they just live in another plugin's dock
	obs_enum_scenes(
		[](void *param, obs_source_t *ss) {
			auto *c = (Ctx *)param;
			obs_scene_t *scene = obs_scene_from_source(ss);
			if (!scene)
				return true;
			obs_scene_enum_items(
				scene,
				[](obs_scene_t *, obs_sceneitem_t *item, void *p) {
					auto *c = (Ctx *)p;
					obs_source_t *src = obs_sceneitem_get_source(item);
					if (src && obs_source_get_name(src) && *c->name == obs_source_get_name(src) &&
					    obs_sceneitem_visible(item)) {
						obs_sceneitem_set_visible(item, false);
						c->hidden++;
					}
					return true;
				},
				c);
			return true;
		},
		&ctx);
	return ctx.hidden;
}

/// Scenes OBS knows about that are not in the scene list: another canvas's scenes. Aitum's
/// vertical canvas keeps its scenes this way, and they are the ones a vertical stream shows.
std::vector<std::string> Switcher::otherCanvasScenes()
{
	std::vector<std::string> front = sceneNames(), out;
	std::pair<std::vector<std::string> *, std::vector<std::string> *> ctx(&front, &out);
	obs_enum_scenes(
		[](void *param, obs_source_t *ss) {
			auto *pr = (std::pair<std::vector<std::string> *, std::vector<std::string> *> *)param;
			const char *n = obs_source_get_name(ss);
			if (n && std::find(pr->first->begin(), pr->first->end(), n) == pr->first->end())
				pr->second->push_back(n);
			return true;
		},
		&ctx);
	std::sort(out.begin(), out.end());
	return out;
}

/// The same swap, in the vertical scene: the squad mate's source full-canvas in portrait, the look
/// overlay over it in its portrait form. The video source is the very same one the main canvas
/// shows - a source can sit in any number of scenes - so nothing is decoded twice.
std::string Switcher::applyVertical(const Config &cfg, bool on)
{
	if (cfg.sceneV.empty())
		return "";
	obs_source_t *ss = obs_get_source_by_name(cfg.sceneV.c_str());
	obs_scene_t *scene = ss ? obs_scene_from_source(ss) : nullptr;
	if (!scene) {
		if (ss)
			obs_source_release(ss);
		return "vertical scene '" + cfg.sceneV + "' is not there";
	}
	// the portrait canvas's size: a scene reports its canvas's base size
	uint32_t cw = obs_source_get_width(ss), ch = obs_source_get_height(ss);
	if (cw == 0 || ch == 0) {
		cw = 1080;
		ch = 1920;
	}
	std::string err;
	const Friend *f = cfg.active();
	if (f) {
		std::string name = cfg.sourceFor(*f);
		obs_source_t *src = obs_get_source_by_name(name.c_str());
		if (src) {
			obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
			bool fresh = !item;
			if (!item)
				item = obs_scene_add(scene, src);
			if (item) {
				if (fresh) {
					// full height of the portrait canvas, centred: the sides of a 16:9 feed fall away
					struct vec2 pos = {0, 0}, bounds = {(float)cw, (float)ch};
					obs_sceneitem_set_pos(item, &pos);
					obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_OUTER);
					obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
					obs_sceneitem_set_bounds(item, &bounds);
				}
				if (on)
					moveToTop(item);
				obs_sceneitem_set_visible(item, on);
			}
			obs_source_release(src);
		} else if (on)
			err = "'" + name + "' does not exist yet";
	}
	// the look overlay, portrait
	const char *lookName = Config::overlaySourceNameV();
	if (on && (cfg.lookName || cfg.lookCam || cfg.lookGrain || cfg.lookVignette)) {
		std::string e = ensureBrowserSource(scene, lookName, overlayUrl(cfg, f ? f->name : "") + "&v=1", false,
						    (int)cw, (int)ch);
		if (!e.empty() && err.empty())
			err = e;
		if (obs_sceneitem_t *it = obs_scene_find_source(scene, lookName)) {
			moveToTop(it);
			obs_sceneitem_set_visible(it, true);
		}
	} else
		hideEverywhere(lookName);
	obs_source_release(ss);
	return err;
}

std::vector<std::pair<std::string, std::string>> Switcher::sceneItems(const Config &cfg)
{
	std::vector<std::pair<std::string, std::string>> out;
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return out;
	obs_scene_enum_items(
		obs_scene_from_source(ss),
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *v = (std::vector<std::pair<std::string, std::string>> *)param;
			obs_source_t *s = obs_sceneitem_get_source(item);
			if (s && obs_source_get_name(s))
				v->emplace_back(obs_source_get_name(s),
						obs_source_get_id(s) ? obs_source_get_id(s) : "");
			return true;
		},
		&out);
	std::reverse(out.begin(), out.end()); // obs enumerates bottom-up
	obs_source_release(ss);
	return out;
}

/// The streamer's own face cam and alerts belong over the top of everything we put in the scene.
/// Called after anything that adds a source or changes the order.
void Switcher::raiseOnTop(const Config &cfg)
{
	if (cfg.onTop.empty())
		return;
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return;
	obs_scene_t *scene = obs_scene_from_source(ss);
	// last first, so the first one in the list ends up the topmost
	for (auto it = cfg.onTop.rbegin(); it != cfg.onTop.rend(); ++it)
		if (obs_sceneitem_t *item = obs_scene_find_source(scene, it->c_str()))
			moveToTop(item);
	obs_source_release(ss);
}

/// A first guess at what the streamer would want kept on top: their camera, and alert overlays.
std::vector<std::string> Switcher::guessOnTop(const Config &cfg)
{
	std::vector<std::string> out;
	for (const auto &[name, id] : sceneItems(cfg)) {
		if (name.rfind("Kennel", 0) == 0) // our own sources are what it sits on top of
			continue;
		std::string n = name, i2 = id;
		std::transform(n.begin(), n.end(), n.begin(), ::tolower);
		bool cam = i2 == "dshow_input" || i2 == "av_capture_input" || i2 == "av_capture_input_v2" ||
			   i2 == "macos-avcapture" || i2 == "v4l2_input";
		bool alert = n.find("alert") != std::string::npos || n.find("streamlabs") != std::string::npos ||
			     n.find("streamelement") != std::string::npos ||
			     n.find("stream element") != std::string::npos;
		if (cam || alert)
			out.push_back(name);
	}
	return out;
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
			raiseOnTop(cfg);
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
		// Every browser feed loaded and playing behind the scenes, so a swap is instant - a Twitch
		// or VDO.Ninja page takes seconds to come up and is worth the wait.
		//
		// NDI is the other way round. A warm NDI feed is a receiver decoding a full stream the whole
		// time, and four of those at 1440p is a stuttering mess for no gain: an NDI receiver is back
		// in well under a second. So only the squad mate we would actually show is kept warm.
		const Friend *act = cfg.active();
		for (const auto &f : cfg.friends) {
			if (f.isWeb() || (act && f.name == act->name))
				armOne(cfg, f);
			else
				hideEverywhere(cfg.sourceFor(f)); // stops it receiving
		}
		raiseOnTop(cfg);
		// the shared browser source is not used in this mode; make sure it is not left showing
		hideEverywhere(Config::webSourceName());
		return;
	}
	const Friend *f = cfg.active();
	if (f)
		armOne(cfg, *f);
	raiseOnTop(cfg);
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
		// A browser feed goes on playing while its scene item is hidden, so hide it and let it play.
		//
		// An NDI feed is the opposite problem. Kept in the scene it stays connected and swaps
		// instantly - but it is then pulling its full stream the whole time you are alive, which is
		// a constant load on the network and on both PCs for something you need only when you go
		// down. Off by default; the tick box on the Switch tab trades that bandwidth for the swap.
		bool keepUp = !f.isWeb() && (cfg.warmNdi || f.kind == FriendKind::Discord);
		obs_sceneitem_set_visible(item, keepUp);
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
	n += hideEverywhere(Config::overlaySourceNameV());
	return n;
}

/// OBS is closing: take the dual-POV window out of the scene and release its private scene and
/// browser page while obs-browser is still loaded.
void Switcher::shutdown()
{
	stopNdiShare();
	kennelNdi::shutdown();
	if (!dualScene_)
		return;
	obs_source_t *dualSrc = obs_scene_get_source(dualScene_);
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
		obs_sceneitem_t *it = scene ? obs_scene_find_source(scene, Config::dualSceneName()) : nullptr;
		if (it)
			obs_sceneitem_remove(it);
	}
	obs_frontend_source_list_free(&scenes);
	(void)dualSrc;
	obs_scene_release(dualScene_);
	dualScene_ = nullptr;
	obs_source_t *b = obs_get_source_by_name(Config::dualFeedName());
	if (b) {
		obs_source_remove(b);
		obs_source_release(b);
	}
}

std::string Switcher::applyDual(const Config &cfg, bool on)
{
	const Friend *f = cfg.dual();
	if (on && !f)
		return "no squad mate chosen for the dual POV";
	obs_source_t *ss = sceneSource(cfg);
	if (!ss)
		return "no scene";
	obs_scene_t *scene = obs_scene_from_source(ss);
	obs_sceneitem_t *item = obs_scene_find_source(scene, Config::dualSceneName());
	if (!on) {
		if (item)
			obs_sceneitem_set_visible(item, false);
		hideEverywhere(Config::dualSceneName());
		obs_source_release(ss);
		return "";
	}
	struct obs_video_info ovi;
	obs_get_video_info(&ovi);
	// a private nested scene holds the feed at full size; the main scene shows that scene small.
	// Private, so it never appears in the scene list and the "hide every squad mate" sweep does
	// not reach inside it.
	// Private scenes cannot be looked up by name, so it is kept here for the life of the plugin and
	// released in shutdown(); creating one per call leaked a browser page each time, and CEF then
	// crashed when OBS closed with those pages still alive.
	if (!dualScene_) {
		dualScene_ = obs_scene_create_private(Config::dualSceneName());
		if (log)
			log("Added the dual-POV window '" + std::string(Config::dualSceneName()) + "'.");
	}
	obs_scene_t *dual = dualScene_;
	obs_source_t *dualSrc = obs_scene_get_source(dual);
	obs_source_get_ref(dualSrc);
	std::string err;
	std::string inner;
	if (f->isWeb()) {
		inner = Config::dualFeedName();
		err = ensureBrowserSource(dual, inner.c_str(), webUrl(*f), true);
		obs_source_t *b = obs_get_source_by_name(inner.c_str());
		if (b) {
			obs_source_set_muted(b, true); // the window is picture only
			obs_source_release(b);
		}
	} else {
		inner = f->source;
		obs_source_t *src = obs_get_source_by_name(inner.c_str());
		if (!src)
			err = "source '" + inner + "' not found";
		else {
			if (!obs_scene_find_source(dual, inner.c_str())) {
				obs_sceneitem_t *it = obs_scene_add(dual, src);
				struct vec2 pos = {0, 0}, bounds = {(float)ovi.base_width, (float)ovi.base_height};
				obs_sceneitem_set_pos(it, &pos);
				obs_sceneitem_set_bounds_type(it, OBS_BOUNDS_SCALE_INNER);
				obs_sceneitem_set_bounds(it, &bounds);
			}
			obs_source_release(src);
		}
	}
	// only the chosen feed is inside the window
	obs_scene_enum_items(
		dual,
		[](obs_scene_t *, obs_sceneitem_t *it, void *want) {
			obs_source_t *s = obs_sceneitem_get_source(it);
			obs_sceneitem_set_visible(it, s && *(std::string *)want == obs_source_get_name(s));
			return true;
		},
		&inner);
	// opacity
	{
		obs_source_t *fl = obs_source_get_filter_by_name(dualSrc, "Kennel.gg dual opacity");
		obs_data_t *st = obs_data_create();
		obs_data_set_double(st, "opacity", std::clamp(cfg.dualOpacity, 10, 100) / 100.0);
		if (!fl) {
			fl = obs_source_create_private("color_filter_v2", "Kennel.gg dual opacity", st);
			if (fl)
				obs_source_filter_add(dualSrc, fl);
		} else
			obs_source_update(fl, st);
		obs_data_release(st);
		if (fl)
			obs_source_release(fl);
	}
	if (!item)
		item = obs_scene_add(scene, dualSrc);
	if (item) {
		float w = (float)(cfg.dualW * ovi.base_width), h = w * 9.0f / 16.0f;
		struct vec2 pos = {(float)(cfg.dualX * ovi.base_width), (float)(cfg.dualY * ovi.base_height)},
			    bounds = {w, h};
		obs_sceneitem_set_pos(item, &pos);
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
		obs_sceneitem_set_bounds(item, &bounds);
		obs_sceneitem_set_visible(item, err.empty());
		moveToTop(item);
		raiseOnTop(cfg);
	} else
		err = "could not add the dual-POV window to the scene";
	obs_source_release(dualSrc);
	obs_source_release(ss);
	return err;
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
			// muted unless "play the squad mate's own game audio" is ticked, in every mode
			obs_source_set_muted(a, !(on && cfg.friendAudio));
			obs_sceneitem_set_visible(ai, cfg.keepWarm || on);
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
	raiseOnTop(cfg); // the streamer's camera and alerts stay over whatever we just showed
	return errors;
}
