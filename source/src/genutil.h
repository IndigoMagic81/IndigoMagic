// Drawing helpers shared by the generators (gens.cpp, gens2.cpp). Everything is in output
// pixels; fixed sizes are given in design pixels and multiplied by c.px (see gens.h).
#pragma once
#include "gens.h"
#include "noise.h"
#include <functional>

struct Field {
    GenOut& o; const GenCtx& c; int w, h; float px;
    Field(GenOut& o_, const GenCtx& c_) : o(o_), c(c_), w(c_.w), h(c_.h), px(c_.px) { o.f.assign((size_t)w * h, 0.0f); }
    float& at(int x, int y) { return o.f[(size_t)pmod(y, h) * w + pmod(x, w)]; }
    float& atc(int x, int y) { return o.f[(size_t)std::clamp(y, 0, h - 1) * w + std::clamp(x, 0, w - 1)]; }
    // evaluate fn(u,v,x,y) at each pixel; u,v in [0,1)
    template <class F> void each(F fn) {   // runs rows in parallel - fn must be pure
        float* out = o.f.data(); int W = w, H = h;
        ParallelFor(H, [&](int y) { for (int x = 0; x < W; x++) out[(size_t)y * W + x] = fn((x + 0.5f) / W, (y + 0.5f) / H, x, y); });
    }
    void fill(float v) { std::fill(o.f.begin(), o.f.end(), v); }
    // max plotting (wrapped for tiles, clipped for full)
    void plot(int x, int y, float v) {
        if (c.tile) { float& d = at(x, y); d = std::max(d, v); }
        else if (x >= 0 && y >= 0 && x < w && y < h) o.f[(size_t)y * w + x] = std::max(o.f[(size_t)y * w + x], v);
    }
    void add(int x, int y, float v) {
        if (c.tile) at(x, y) += v;
        else if (x >= 0 && y >= 0 && x < w && y < h) o.f[(size_t)y * w + x] += v;
    }
    void set(int x, int y, float v) {
        if (c.tile) at(x, y) = v;
        else if (x >= 0 && y >= 0 && x < w && y < h) o.f[(size_t)y * w + x] = v;
    }
    // blend towards v by coverage a (painter's order, for flat shapes over a background)
    void cover(int x, int y, float v, float a) {
        if (a <= 0) return;
        if (c.tile) { float& d = at(x, y); d = a >= 1 ? v : d + (v - d) * a; }
        else if (x >= 0 && y >= 0 && x < w && y < h) { float& d = o.f[(size_t)y * w + x]; d = a >= 1 ? v : d + (v - d) * a; }
    }
    void disc(float cx, float cy, float r, float v, bool soft = false) {
        int x0 = (int)std::floor(cx - r - 1), x1 = (int)std::ceil(cx + r + 1), y0 = (int)std::floor(cy - r - 1), y1 = (int)std::ceil(cy + r + 1);
        if ((double)(x1 - x0) * (y1 - y0) > 16e6) return;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
                if (soft) { float a = sat(r - d + 0.5f); if (c.discrete) { if (a >= 0.5f) plot(x, y, v); } else if (a > 0) plot(x, y, v * a); }
                else if (d <= r) set(x, y, v);
            }
    }
    void ring(float cx, float cy, float r, float t, float v) {
        int x0 = (int)std::floor(cx - r - t), x1 = (int)std::ceil(cx + r + t), y0 = (int)std::floor(cy - r - t), y1 = (int)std::ceil(cy + r + t);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float d = absf(std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) - r);
                float a = sat(t / 2 - d + 0.5f);
                if (c.discrete) { if (a >= 0.5f) plot(x, y, v); } else if (a > 0) plot(x, y, v * a);
            }
    }
    // antialiased line of thickness t (output pixels); value v scaled by coverage, max-combined
    void line(float ax, float ay, float bx, float by, float t, float v) {
        int x0 = (int)std::floor(std::min(ax, bx) - t - 1), x1 = (int)std::ceil(std::max(ax, bx) + t + 1);
        int y0 = (int)std::floor(std::min(ay, by) - t - 1), y1 = (int)std::ceil(std::max(ay, by) + t + 1);
        if ((double)(x1 - x0) * (y1 - y0) > 16e6) return;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float d = segDist(x + 0.5f, y + 0.5f, ax, ay, bx, by);
                float a = sat(t / 2 - d + 0.5f);
                if (c.discrete) { if (a >= 0.5f) plot(x, y, v); } else if (a > 0) plot(x, y, v * a);
            }
    }
    void polyline(const std::vector<std::pair<float, float>>& pts, float t, float v, bool closed = false) {
        for (size_t i = 0; i + 1 < pts.size(); i++) line(pts[i].first, pts[i].second, pts[i + 1].first, pts[i + 1].second, t, v);
        if (closed && pts.size() > 2) line(pts.back().first, pts.back().second, pts[0].first, pts[0].second, t, v);
    }
    void tri(float ax, float ay, float bx, float by, float cx, float cy, float v) {
        int x0 = (int)std::floor(std::min({ax, bx, cx})), x1 = (int)std::ceil(std::max({ax, bx, cx}));
        int y0 = (int)std::floor(std::min({ay, by, cy})), y1 = (int)std::ceil(std::max({ay, by, cy}));
        if ((double)(x1 - x0) * (y1 - y0) > 16e6) return;
        auto e = [](float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float px_ = x + 0.5f, py_ = y + 0.5f;
                float e0 = e(ax, ay, bx, by, px_, py_), e1 = e(bx, by, cx, cy, px_, py_), e2 = e(cx, cy, ax, ay, px_, py_);
                if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0)) set(x, y, v);
            }
    }
    void rect(float x0, float y0, float x1, float y1, float v) {
        for (int y = (int)std::floor(y0); y < (int)std::ceil(y1); y++)
            for (int x = (int)std::floor(x0); x < (int)std::ceil(x1); x++) set(x, y, v);
    }
    // box blur with a radius of one design pixel (wrapped for tiles, clamped for scenes); n passes
    void blur(int n = 1) { boxBlur(std::max(1, (int)std::lround(px)), n); }
    void boxBlur(int r, int n) {
        if (r < 1 || w < 2 || h < 2) return;
        std::vector<float> t(o.f.size());
        bool wrap = c.tile;
        for (int k = 0; k < n; k++) {
            int rr = std::min(r, std::max(1, w / 2 - 1));
            float* src = o.f.data();
            float* dst = t.data();
            int W = w, H = h;
            ParallelFor(H, [&](int y) {
                const float* s = src + (size_t)y * W;
                float* d = dst + (size_t)y * W;
                float acc = 0;
                auto at = [&](int x) { return s[wrap ? pmod(x, W) : std::clamp(x, 0, W - 1)]; };
                for (int i = -rr; i <= rr; i++) acc += at(i);
                float inv = 1.0f / (2 * rr + 1);
                for (int x = 0; x < W; x++) { d[x] = acc * inv; acc += at(x + rr + 1) - at(x - rr); }
            });
            rr = std::min(r, std::max(1, h / 2 - 1));
            ParallelFor(W, [&](int x) {
                float acc = 0;
                auto at = [&](int y) { return dst[(size_t)(wrap ? pmod(y, H) : std::clamp(y, 0, H - 1)) * W + x]; };
                for (int i = -rr; i <= rr; i++) acc += at(i);
                float inv = 1.0f / (2 * rr + 1);
                for (int y = 0; y < H; y++) { src[(size_t)y * W + x] = acc * inv; acc += at(y + rr + 1) - at(y - rr); }
            });
        }
    }
    // Fine grain at the design resolution, smoothly interpolated (so it stays soft at any Scale) and
    // periodic on the tile. Value in [0,1). fx, fy: cells per design pixel (1 = one value per pixel)
    float grain(float u, float v, uint32_t salt = 0, float fx = 1, float fy = 1) const {
        int gw = std::max(1, (int)std::lround(c.dw * fx)), gh = std::max(1, (int)std::lround(c.dh * fy));
        float x = u * gw - 0.5f, y = v * gh - 0.5f;
        int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
        float tx = x - x0, ty = y - y0;
        uint32_t s = c.seed * 2654435761U + salt;
        auto H = [&](int i, int j) { return hf(hash3(pmod(i, gw), pmod(j, gh), s)); };
        float a = H(x0, y0), b = H(x0 + 1, y0), cc = H(x0, y0 + 1), d = H(x0 + 1, y0 + 1);
        tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
        return lerpf(lerpf(a, b, tx), lerpf(cc, d, tx), ty);
    }
    // Upsample a design-resolution field (dw x dh) to the output, bicubic-smooth, wrapped for tiles
    void fromDesign(const std::vector<float>& d, int dw, int dh) {
        float* out = o.f.data(); int W = w, H = h;
        bool wrap = c.tile;
        ParallelFor(H, [&](int y) {
            float sy = (y + 0.5f) * dh / H - 0.5f;
            int y0 = (int)std::floor(sy); float ty = sy - y0;
            for (int x = 0; x < W; x++) {
                float sx = (x + 0.5f) * dw / W - 0.5f;
                int x0 = (int)std::floor(sx); float tx = sx - x0;
                auto A = [&](int i, int j) {
                    i = wrap ? pmod(i, dw) : std::clamp(i, 0, dw - 1); j = wrap ? pmod(j, dh) : std::clamp(j, 0, dh - 1);
                    return d[(size_t)j * dw + i];
                };
                // Catmull-Rom
                auto cr = [](float p0, float p1, float p2, float p3, float t) {
                    return p1 + 0.5f * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
                };
                float r[4];
                for (int k = 0; k < 4; k++) r[k] = cr(A(x0 - 1, y0 - 1 + k), A(x0, y0 - 1 + k), A(x0 + 1, y0 - 1 + k), A(x0 + 2, y0 - 1 + k), tx);
                out[(size_t)y * W + x] = cr(r[0], r[1], r[2], r[3], ty);
            }
        });
    }
};
static inline float lvl(int k) { return k / 3.0f; }   // discrete level

// square-torus coordinates for full-screen patterns: x in [-a/2,a/2], y in [-0.5,0.5]
#define FULLXY float a = c.aspect; float X = (u - 0.5f) * a; float Y = v - 0.5f; (void)a; (void)X; (void)Y;

// antialiasing width in u,v units of one output pixel (for analytic edges inside F.each)
static inline float pixU(const GenCtx& c) { return 1.0f / std::max(1, c.w); }
// coverage of a signed distance (negative inside) measured in output pixels
static inline float cov(float dPx) { return sat(0.5f - dPx); }
// a smooth step that is one output pixel wide around d = 0 (d in the same units as w1px)
static inline float aa(float d, float w1px) { return sat(0.5f - d / std::max(1e-9f, w1px)); }
