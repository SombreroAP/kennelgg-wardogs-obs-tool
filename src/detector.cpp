#include "detector.h"
#include <algorithm>
#include <cmath>
#include <thread>
#include <graphics/image-file.h>
#include <obs-module.h>
#include <plugin-support.h>

namespace {
constexpr int kMinTemplateHeight = 10; // below this NCC on text is noise (8 px scored 0.87 on an alive frame)
constexpr float kScaleStep = 1.06f;    // correlation on small text drops fast when the size is 10 % out

void normalise(std::vector<float> &t)
{
	double mean = 0;
	for (float v : t)
		mean += v;
	mean /= (double)t.size();
	double ss = 0;
	for (float &v : t) {
		v -= (float)mean;
		ss += (double)v * v;
	}
	float inv = (float)(1.0 / (std::sqrt(ss) + 1e-6));
	for (float &v : t)
		v *= inv;
}

std::vector<float> resizeBilinear(const std::vector<float> &src, int sw, int sh, int dw, int dh)
{
	std::vector<float> out((size_t)dw * dh);
	for (int y = 0; y < dh; y++) {
		float fy = ((y + 0.5f) * sh / dh) - 0.5f;
		int y0 = std::clamp((int)std::floor(fy), 0, sh - 1), y1 = std::min(y0 + 1, sh - 1);
		float wy = std::clamp(fy - y0, 0.f, 1.f);
		for (int x = 0; x < dw; x++) {
			float fx = ((x + 0.5f) * sw / dw) - 0.5f;
			int x0 = std::clamp((int)std::floor(fx), 0, sw - 1), x1 = std::min(x0 + 1, sw - 1);
			float wx = std::clamp(fx - x0, 0.f, 1.f);
			float a = src[(size_t)y0 * sw + x0], b = src[(size_t)y0 * sw + x1];
			float c = src[(size_t)y1 * sw + x0], d = src[(size_t)y1 * sw + x1];
			out[(size_t)y * dw + x] = (a * (1 - wx) + b * wx) * (1 - wy) + (c * (1 - wx) + d * wx) * wy;
		}
	}
	return out;
}

void integral(const std::vector<float> &g, int W, int H, std::vector<double> &sum, std::vector<double> &sq)
{
	int iw = W + 1;
	sum.assign((size_t)iw * (H + 1), 0.0);
	sq.assign((size_t)iw * (H + 1), 0.0);
	for (int y = 1; y <= H; y++) {
		double rs = 0, rq = 0;
		for (int x = 1; x <= W; x++) {
			float v = g[(size_t)(y - 1) * W + x - 1];
			rs += v;
			rq += (double)v * v;
			sum[(size_t)y * iw + x] = sum[(size_t)(y - 1) * iw + x] + rs;
			sq[(size_t)y * iw + x] = sq[(size_t)(y - 1) * iw + x] + rq;
		}
	}
}
} // namespace

Frame Detector::fromBGRA(const uint8_t *bgra, int w, int h, int linesize)
{
	Frame f;
	f.w = w;
	f.h = h;
	f.gray.resize((size_t)w * h);
	for (int y = 0; y < h; y++) {
		const uint8_t *row = bgra + (size_t)y * linesize;
		float *out = &f.gray[(size_t)y * w];
		for (int x = 0; x < w; x++)
			out[x] = 0.114f * row[x * 4] + 0.587f * row[x * 4 + 1] + 0.299f * row[x * 4 + 2];
	}
	return f;
}

/// 3x3 Gaussian (sigma ~0.7): widens the correlation peak across sizes without blurring the text away.
std::vector<float> Detector::blur3(const std::vector<float> &g, int W, int H)
{
	const float a = 0.209f, b = 0.582f;
	std::vector<float> tmp(g.size()), out(g.size());
	for (int y = 0; y < H; y++) {
		size_t r = (size_t)y * W;
		tmp[r] = g[r];
		tmp[r + W - 1] = g[r + W - 1];
		for (int x = 1; x < W - 1; x++)
			tmp[r + x] = a * g[r + x - 1] + b * g[r + x] + a * g[r + x + 1];
	}
	std::copy(tmp.begin(), tmp.begin() + W, out.begin());
	std::copy(tmp.end() - W, tmp.end(), out.end() - W);
	for (int y = 1; y < H - 1; y++)
		for (int x = 0; x < W; x++)
			out[(size_t)y * W + x] = a * tmp[(size_t)(y - 1) * W + x] + b * tmp[(size_t)y * W + x] + a * tmp[(size_t)(y + 1) * W + x];
	return out;
}

bool Detector::loadTemplatePng(const std::string &path, float widthFrac)
{
	gs_image_file_t img;
	gs_image_file_init(&img, path.c_str());
	if (!img.loaded || !img.texture_data || img.cx == 0 || img.cy == 0) {
		gs_image_file_free(&img);
		obs_log(LOG_WARNING, "could not load template %s", path.c_str());
		return false;
	}
	// any 4-byte format: the templates are grayscale, so channel order does not matter
	std::vector<float> g((size_t)img.cx * img.cy);
	for (size_t i = 0; i < g.size(); i++) {
		const uint8_t *p = img.texture_data + i * 4;
		g[i] = (p[0] + p[1] + p[2]) / 3.0f;
	}
	setTemplate(g, (int)img.cx, (int)img.cy, widthFrac);
	gs_image_file_free(&img);
	return true;
}

void Detector::setTemplate(const std::vector<float> &gray, int w, int h, float widthFrac)
{
	tpl_ = gray;
	tplW_ = w;
	tplH_ = h;
	widthFrac_ = widthFrac;
	scaled_.clear();
	lockScale_ = -1;
	float baseW = widthFrac * FrameWidth;
	float aspect = (float)h / w;
	for (int k = 0; k < 40; k++) {
		float sc = 0.5f * std::pow(kScaleStep, (float)k);
		if (sc > 1.6f)
			break;
		int sw = std::max(8, (int)std::lround(baseW * sc)), sh = (int)std::lround(baseW * sc * aspect);
		if (sh < kMinTemplateHeight)
			continue;
		Scaled s;
		s.w = sw;
		s.h = sh;
		s.scale = sc;
		s.t = blur3(resizeBilinear(gray, w, h, sw, sh), sw, sh);
		normalise(s.t);
		scaled_.push_back(std::move(s));
	}
}

Detector::Hit Detector::search(const std::vector<float> &g, const std::vector<double> &sum, const std::vector<double> &sq, int W, int H,
			       const std::vector<float> &t, int tw, int th, int x0, int y0, int x1, int y1, int stride)
{
	(void)H;
	Hit best;
	best.score = -2;
	int n = tw * th, iw = W + 1;
	for (int y = y0; y <= y1; y += stride) {
		for (int x = x0; x <= x1; x += stride) {
			size_t a = (size_t)y * iw + x, b = (size_t)y * iw + x + tw, c = (size_t)(y + th) * iw + x, d = (size_t)(y + th) * iw + x + tw;
			double ps = sum[d] - sum[b] - sum[c] + sum[a];
			double pq = sq[d] - sq[b] - sq[c] + sq[a];
			double var = pq - ps * ps / n;
			if (var < 1e-3)
				continue;
			double dot = 0;
			for (int ty = 0; ty < th; ty++) {
				const float *gr = &g[(size_t)(y + ty) * W + x];
				const float *tr = &t[(size_t)ty * tw];
				float acc = 0;
				for (int tx = 0; tx < tw; tx++)
					acc += gr[tx] * tr[tx];
				dot += acc;
			}
			double score = dot / std::sqrt(var); // t is zero-mean unit-norm, so this is the NCC
			if (score > best.score) {
				best.score = score;
				best.x = x;
				best.y = y;
			}
		}
	}
	if (best.score < -1)
		best.score = -1;
	return best;
}

Match Detector::compare(const Frame &f)
{
	Match m;
	if (scaled_.empty() || f.empty())
		return m;
	int W = f.w, H = f.h;
	std::vector<float> g = blur3(f.gray, W, H);
	std::vector<double> sum, sq;
	integral(g, W, H, sum, sq);

	auto refine = [&](const Scaled &s, int cx, int cy, int r) {
		return search(g, sum, sq, W, H, s.t, s.w, s.h, std::max(0, cx - r), std::max(0, cy - r), std::min(W - s.w, cx + r),
			      std::min(H - s.h, cy + r), 1);
	};
	auto fill = [&](const Scaled &s, const Hit &h) {
		m.score = std::max(0.0, h.score);
		m.x = (float)h.x / W;
		m.y = (float)h.y / H;
		m.w = (float)s.w / W;
		m.h = (float)s.h / H;
	};

	// fast path: still where we last saw it?
	if (lockScale_ >= 0 && lockScale_ < (int)scaled_.size()) {
		const Scaled &s = scaled_[lockScale_];
		Hit h = refine(s, lockX_, lockY_, 4);
		if (h.score >= threshold * 0.93) {
			lockX_ = h.x;
			lockY_ = h.y;
			fill(s, h);
			m.locked = h.score >= threshold;
			return m;
		}
		lockScale_ = -1;
	}

	// full search: every size, full resolution, 2 px stride inside the band, then a 1 px refine
	std::vector<Hit> hits(scaled_.size());
	int x0 = (int)(W * fromX), y0 = (int)(H * fromY);
	auto work = [&](size_t i) {
		const Scaled &s = scaled_[i];
		int x1 = std::min(W - s.w, (int)(W * toX)), y1 = std::min(H - s.h, (int)(H * toY));
		if (x1 < x0 || y1 < y0)
			return;
		Hit c = search(g, sum, sq, W, H, s.t, s.w, s.h, x0, y0, x1, y1, 2);
		hits[i] = c.score < 0 ? c : refine(s, c.x, c.y, 2);
	};
	unsigned nt = std::max(1u, std::min(4u, std::thread::hardware_concurrency()));
	std::vector<std::thread> pool;
	for (unsigned t = 0; t < nt; t++)
		pool.emplace_back([&, t]() {
			for (size_t i = t; i < scaled_.size(); i += nt)
				work(i);
		});
	for (auto &t : pool)
		t.join();

	int bi = -1;
	double bs = -1;
	for (size_t i = 0; i < hits.size(); i++)
		if (hits[i].score > bs) {
			bs = hits[i].score;
			bi = (int)i;
		}
	if (bi < 0) {
		m.score = 0;
		return m;
	}
	fill(scaled_[bi], hits[bi]);
	if (bs >= threshold) {
		lockScale_ = bi;
		lockX_ = hits[bi].x;
		lockY_ = hits[bi].y;
		m.locked = true;
	}
	return m;
}
