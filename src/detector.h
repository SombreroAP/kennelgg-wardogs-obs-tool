#pragma once
#include <cstdint>
#include <string>
#include <vector>

/// A grayscale frame, 0..255 as float.
struct Frame {
	int w = 0, h = 0;
	std::vector<float> gray;
	bool empty() const { return w == 0 || h == 0; }
};

struct Match {
	double score = -1;                // best normalised cross-correlation, 0..1; -1 = no template
	float x = 0, y = 0, w = 0, h = 0; // where, as fractions of the frame
	bool locked = false;
};

/// Looks for a small piece of HUD (a template) anywhere inside a band of the frame, at several sizes,
/// with normalised cross-correlation on lightly blurred grayscale. Once found, only the found spot is
/// checked until it stops matching. See POVBridge's notes for the measurements behind the constants.
class Detector {
public:
	/// Band of the frame to search, as fractions.
	float fromX = 0.60f, toX = 1.0f, fromY = 0.25f, toY = 0.85f;
	double threshold = 0.85;

	/// Template as grayscale pixels, plus its width as a fraction of the frame it was cut from.
	void setTemplate(const std::vector<float> &gray, int w, int h, float widthFrac);
	bool loadTemplatePng(const std::string &path, float widthFrac);
	bool hasTemplate() const { return !scaled_.empty(); }
	int templateW() const { return tplW_; }
	int templateH() const { return tplH_; }
	const std::vector<float> &templateGray() const { return tpl_; }
	float templateWidthFrac() const { return widthFrac_; }

	Match compare(const Frame &f);
	void unlock() { lockScale_ = -1; }

	/// Frame width the detector expects (what the source is rendered to).
	static constexpr int FrameWidth = 800;
	static Frame fromBGRA(const uint8_t *bgra, int w, int h, int linesize);
	static std::vector<float> blur3(const std::vector<float> &g, int w, int h);

private:
	struct Scaled {
		std::vector<float> t;
		int w = 0, h = 0;
		float scale = 1;
	};
	std::vector<float> tpl_;
	int tplW_ = 0, tplH_ = 0;
	float widthFrac_ = 0;
	std::vector<Scaled> scaled_;
	int lockScale_ = -1, lockX_ = 0, lockY_ = 0;

	struct Hit {
		double score = -1;
		int x = 0, y = 0;
	};
	static Hit search(const std::vector<float> &g, const std::vector<double> &sum, const std::vector<double> &sq,
			  int W, int H, const std::vector<float> &t, int tw, int th, int x0, int y0, int x1, int y1,
			  int stride);
};
