#include "ndi.h"
#include <cstdint>
#include <mutex>
#include <obs-module.h>
#include <plugin-support.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
// The two structs we need from the NDI SDK, laid out as the header declares them.
struct NDIfindCreate {
	bool showLocal;
	const char *groups;
	const char *extraIps;
};
struct NDIsource {
	const char *name;
	const char *url;
};

using findCreateFn = void *(*)(const NDIfindCreate *);
using findSourcesFn = const NDIsource *(*)(void *, uint32_t *);
using findWaitFn = bool (*)(void *, uint32_t);
using findDestroyFn = void (*)(void *);
using initFn = bool (*)();

struct Runtime {
	findCreateFn create = nullptr;
	findSourcesFn get = nullptr;
	findWaitFn wait = nullptr;
	findDestroyFn destroy = nullptr;
	bool loaded = false;
	bool tried = false;
	std::string extraIps; // comma separated, as NDI wants them
};
Runtime g;
std::mutex mx;

#ifdef _WIN32
HMODULE openLib()
{
	// ONLY the copy DistroAV has already loaded. Loading a second NDI runtime into the same process
	// - the one NDI Tools installs, say - and initialising it puts two NDI stacks in OBS fighting
	// over the same discovery sockets, and then nothing on that PC can see or be seen. If DistroAV
	// has not loaded it, we simply cannot answer questions about NDI, and say so.
	return GetModuleHandleA("Processing.NDI.Lib.x64.dll");
}

void load()
{
	if (g.tried)
		return;
	g.tried = true;
	HMODULE h = openLib();
	if (!h) {
		obs_log(LOG_INFO, "NDI: runtime not found, the squad-mate scan will list what OBS already has");
		return;
	}
	g.create = (findCreateFn)GetProcAddress(h, "NDIlib_find_create_v2");
	g.get = (findSourcesFn)GetProcAddress(h, "NDIlib_find_get_current_sources");
	g.wait = (findWaitFn)GetProcAddress(h, "NDIlib_find_wait_for_sources");
	g.destroy = (findDestroyFn)GetProcAddress(h, "NDIlib_find_destroy");
	// NDIlib_initialize is deliberately NOT called: DistroAV owns this runtime and has done it.
	g.loaded = g.create && g.get && g.destroy;
	if (!g.loaded)
		obs_log(LOG_WARNING, "NDI: runtime loaded but the finder entry points are missing");
}
#else
void load()
{
	g.tried = true;
}
#endif
} // namespace

namespace kennelNdi {

bool available()
{
	std::lock_guard<std::mutex> lk(mx);
	load();
	return g.loaded;
}

std::vector<std::string> sources(int waitMs)
{
	std::vector<std::string> out;
	std::lock_guard<std::mutex> lk(mx);
	load();
	if (!g.loaded)
		return out;
	// Made for the question and destroyed straight after. A finder held open for the life of OBS is
	// another party on the same discovery sockets as the plugin that actually does the NDI work.
	NDIfindCreate c{true, nullptr, g.extraIps.empty() ? nullptr : g.extraIps.c_str()};
	void *finder = g.create(&c);
	if (!finder)
		return out;
	if (g.wait && waitMs > 0)
		g.wait(finder, (uint32_t)waitMs);
	uint32_t n = 0;
	const NDIsource *s = g.get(finder, &n);
	for (uint32_t i = 0; s && i < n; i++)
		if (s[i].name && *s[i].name)
			out.emplace_back(s[i].name);
	g.destroy(finder);
	return out;
}

void setExtraIps(const std::vector<std::string> &ips)
{
	std::string joined;
	for (const auto &i : ips)
		joined += (joined.empty() ? "" : ",") + i;
	std::lock_guard<std::mutex> lk(mx);
	if (joined == g.extraIps)
		return;
	g.extraIps = joined;
}

void shutdown()
{
	// nothing is held open any more
}

} // namespace kennelNdi
