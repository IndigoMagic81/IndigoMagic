// Shared math / noise helpers for the pattern generators (header-only).
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>

static inline int pmod(int a, int m) { int r = a % m; return r < 0 ? r + m : r; }
static inline float fmodp(float a, float m) { float r = std::fmod(a, m); return r < 0 ? r + m : r; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float sat(float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; }   // NaN -> 0
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static inline float smooth(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static inline float absf(float v) { return v < 0 ? -v : v; }
static const float TAU = 6.28318530718f;
static const float PI_F = 3.14159265359f;

static inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
static inline uint32_t hash3(int a, int b, uint32_t s) {
    return hash32((uint32_t)a * 0x9E3779B1U ^ hash32((uint32_t)b * 0x85EBCA77U ^ hash32(s)));
}
static inline float hf(uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); }

// Small deterministic PRNG for generators that need sequences.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(hash32(seed | 1)) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return hf(next()); }                       // [0,1)
    float range(float a, float b) { return a + (b - a) * uni(); }
    int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); } // [a,b]
    float gauss() { float u = uni() + 1e-7f, v = uni(); return std::sqrt(-2 * std::log(u)) * std::cos(TAU * v); }
};

// Periodic gradient noise. (x,y) in lattice units; lattice repeats every px / py cells.
static inline float perlin(float x, float y, int px, int py, uint32_t seed) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    float fx = x - x0, fy = y - y0;
    auto g = [&](int ix, int iy, float dx, float dy) {
        float a = hf(hash3(pmod(ix, px), pmod(iy, py), seed)) * TAU;
        return std::cos(a) * dx + std::sin(a) * dy;
    };
    float n00 = g(x0, y0, fx, fy), n10 = g(x0 + 1, y0, fx - 1, fy);
    float n01 = g(x0, y0 + 1, fx, fy - 1), n11 = g(x0 + 1, y0 + 1, fx - 1, fy - 1);
    float u = fade(fx), v = fade(fy);
    return lerpf(lerpf(n00, n10, u), lerpf(n01, n11, u), v);
}
// fbm over tile coordinates u,v in [0,1): seamless by construction. Output roughly [-1,1].
static inline float fbm(float u, float v, int px, int py, int oct, float gain, uint32_t seed) {
    float s = 0, a = 1, n = 0;
    for (int o = 0; o < oct; o++) {
        s += a * perlin(u * px, v * py, px, py, seed + o * 1013U);
        n += a; a *= gain; px *= 2; py *= 2;
    }
    return s / n;
}
static inline float fbm(float u, float v, int p, int oct, float gain, uint32_t seed) { return fbm(u, v, p, p, oct, gain, seed); }
// Ridged multifractal (sharp creases), [0,1]
static inline float ridged(float u, float v, int p, int oct, uint32_t seed) {
    float s = 0, a = 1, n = 0;
    for (int o = 0; o < oct; o++) {
        float r = 1.0f - absf(perlin(u * p, v * p, p, p, seed + o * 7919U));
        s += a * r * r; n += a; a *= 0.5f; p *= 2;
    }
    return s / n;
}

struct Worley { float f1, f2, dx, dy, fx, fy; uint32_t id; };
// Periodic cellular noise on a g x g grid (one jittered feature point per cell).
static inline Worley worley(float u, float v, int g, uint32_t seed, float jitter = 0.9f) {
    float x = u * g, y = v * g;
    int cx = (int)std::floor(x), cy = (int)std::floor(y);
    Worley w{1e9f, 1e9f, 0, 0, 0, 0, 0};
    for (int j = -2; j <= 2; j++)
        for (int i = -2; i <= 2; i++) {
            int ix = cx + i, iy = cy + j;
            uint32_t h = hash3(pmod(ix, g), pmod(iy, g), seed);
            float fx = ix + 0.5f + (hf(h) - 0.5f) * jitter;
            float fy = iy + 0.5f + (hf(hash32(h ^ 0x5bd1e995U)) - 0.5f) * jitter;
            float dx = x - fx, dy = y - fy, d = std::sqrt(dx * dx + dy * dy);
            if (d < w.f1) { w.f2 = w.f1; w.f1 = d; w.dx = dx; w.dy = dy; w.fx = fx / g; w.fy = fy / g; w.id = h; }
            else if (d < w.f2) w.f2 = d;
        }
    return w;
}

static const int BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static inline int bayer8(int x, int y) {
    int b = BAYER4[y & 3][x & 3];
    static const int B2[2][2] = {{0, 2}, {3, 1}};
    return b * 4 + B2[(y >> 2) & 1][(x >> 2) & 1];
}

// distance from point p to segment ab
static inline float segDist(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float l2 = vx * vx + vy * vy;
    float t = l2 > 0 ? clampf((wx * vx + wy * vy) / l2, 0, 1) : 0;
    float dx = wx - t * vx, dy = wy - t * vy;
    return std::sqrt(dx * dx + dy * dy);
}
// shortest wrapped difference on a unit torus
static inline float wrapd(float d) { d = d - std::floor(d + 0.5f); return d; }

// Colour helpers: 0xRRGGBB
static inline float lum(uint32_t c) { return 0.2126f * ((c >> 16) & 255) + 0.7152f * ((c >> 8) & 255) + 0.0722f * (c & 255); }
static inline uint32_t mixc(uint32_t a, uint32_t b, float t) {
    int r = (int)std::lround(lerpf((float)((a >> 16) & 255), (float)((b >> 16) & 255), t));
    int g = (int)std::lround(lerpf((float)((a >> 8) & 255), (float)((b >> 8) & 255), t));
    int bl = (int)std::lround(lerpf((float)(a & 255), (float)(b & 255), t));
    return (uint32_t)(r << 16 | g << 8 | bl);
}
static inline uint32_t hsv2rgb_u(float h, float s, float v) {
    h = fmodp(h, 360.0f);
    float c = v * s, x = c * (1 - absf(std::fmod(h / 60.0f, 2.0f) - 1)), m = v - c, r = 0, g = 0, b = 0;
    int k = (int)(h / 60) % 6;
    switch (k) { case 0: r = c; g = x; break; case 1: r = x; g = c; break; case 2: g = c; b = x; break;
                 case 3: g = x; b = c; break; case 4: r = x; b = c; break; default: r = c; b = x; }
    auto q = [](float t) { return (uint32_t)std::clamp((int)std::lround(t * 255), 0, 255); };
    return q(r + m) << 16 | q(g + m) << 8 | q(b + m);
}
