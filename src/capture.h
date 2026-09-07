#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <obs.h>

/// Renders an OBS source into a small BGRA buffer (targetWidth wide) from any thread.
/// The same pattern obs-websocket uses for GetSourceScreenshot: texrender + stage surface.
class Capture {
public:
	~Capture();
	bool grab(obs_source_t *source, int targetWidth, std::vector<uint8_t> &bgra, int &w, int &h, int &linesize);

private:
	gs_texrender_t *tr_ = nullptr;
	gs_stagesurf_t *st_ = nullptr;
	int stW_ = 0, stH_ = 0;
};
