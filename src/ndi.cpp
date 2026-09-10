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
	void *finder = nullptr;
};
Runtime g;
std::mutex mx;

#ifdef _WIN32
HMODULE openLib()
{
	// DistroAV has almost always loaded it already; then this costs nothing and adds no reference
	// to a library the user did not install.
	if (HMODULE h = GetModuleHandleA("Processing.NDI.Lib.x64.dll"))
		return h;
	if (HMODULE h = LoadLibraryA("Processing.NDI.Lib.x64.dll"))
		return h;
	// the runtime installer sets one of these, per NDI's own documented lookup
	for (const char *var : {"NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5", "NDI_RUNTIME_DIR_V4"}) {
		char dir[MAX_PATH];
		DWORD n = GetEnvironmentVariableA(var, dir, MAX_PATH);
		if (n == 0 || n >= MAX_PATH)
			continue;
		std::string p = std::string(dir) + "\\Processing.NDI.Lib.x64.dll";
		if (HMODULE h = LoadLibraryA(p.c_str()))
			return h;
	}
	return nullptr;
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
	if (auto init = (initFn)GetProcAddress(h, "NDIlib_initialize"))
		init();
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
	if (!g.finder) {
		NDIfindCreate c{true, nullptr, nullptr};
		g.finder = g.create(&c);
		if (!g.finder)
			return out;
		// only the first look waits; the finder keeps listening in the background after that
		if (g.wait && waitMs > 0)
			g.wait(g.finder, (uint32_t)waitMs);
	}
	uint32_t n = 0;
	const NDIsource *s = g.get(g.finder, &n);
	for (uint32_t i = 0; s && i < n; i++)
		if (s[i].name && *s[i].name)
			out.emplace_back(s[i].name);
	return out;
}

void shutdown()
{
	std::lock_guard<std::mutex> lk(mx);
	if (g.finder && g.destroy)
		g.destroy(g.finder);
	g.finder = nullptr;
}

} // namespace kennelNdi
