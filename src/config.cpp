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
	if (!d)
		d = obs_data_create();
	// defaults come from the current field values (constructor defaults)
	DEFS(gameSource);
	DEFS(sceneName);
	DEFI(activeFriend);
	DEFB(bringToFront);
	DEFB(keepWarm);
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
	DEFI(pollMs);
	DEFI(downFrames);
	DEFI(upFrames);
	DEFI(minDownMs);
	DEFB(watchRevive);
	DEFD(reviveThreshold);
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
	GETI(pollMs);
	GETI(downFrames);
	GETI(upFrames);
	GETI(minDownMs);
	GETB(watchRevive);
	GETD(reviveThreshold);
	GETD(customTemplateWidthFrac);
	GETD(boxX);
	GETD(boxY);
	GETD(boxW);
	GETD(boxH);
	muteWhileDowned = getStrings(d, "muteWhileDowned");

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
			friends.push_back(f);
			obs_data_release(it);
		}
		obs_data_array_release(arr);
	}
	if (activeFriend >= (int)friends.size())
		activeFriend = 0;
	if (lookLabel.empty())
		lookLabel = "POV";
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
	SETI(pollMs);
	SETI(downFrames);
	SETI(upFrames);
	SETI(minDownMs);
	SETB(watchRevive);
	SETD(reviveThreshold);
	SETD(customTemplateWidthFrac);
	SETD(boxX);
	SETD(boxY);
	SETD(boxW);
	SETD(boxH);
	setStrings(d, "muteWhileDowned", muteWhileDowned);
	obs_data_array_t *arr = obs_data_array_create();
	for (auto &f : friends) {
		obs_data_t *it = obs_data_create();
		obs_data_set_string(it, "name", f.name.c_str());
		obs_data_set_int(it, "kind", (int)f.kind);
		obs_data_set_string(it, "source", f.source.c_str());
		obs_data_set_string(it, "audioSource", f.audioSource.c_str());
		obs_data_set_string(it, "channel", f.channel.c_str());
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
