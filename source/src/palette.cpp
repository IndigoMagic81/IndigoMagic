// Palettes, the random colour generator and palette files
#include "palette.h"
#include <algorithm>
#include <cmath>
#include <cwchar>

// ---------------------------------------------------------------------------------------------
// OKLab / OKLCH (perceptually even lightness and hue - what makes generated palettes look good)
// ---------------------------------------------------------------------------------------------
struct Lab { float L, a, b; };
static float s2l(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
static float l2s(float c) { return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }
static Lab toLab(uint32_t c) {
    float r = s2l(((c >> 16) & 255) / 255.0f), g = s2l(((c >> 8) & 255) / 255.0f), b = s2l((c & 255) / 255.0f);
    float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;
    l = std::cbrt(l); m = std::cbrt(m); s = std::cbrt(s);
    return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s, 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}
static bool fromLab(Lab c, float rgb[3]) {
    float l = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b, m = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b,
          s = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
    l = l * l * l; m = m * m * m; s = s * s * s;
    rgb[0] = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    rgb[1] = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    rgb[2] = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
    const float e = 1e-4f;
    return rgb[0] >= -e && rgb[0] <= 1 + e && rgb[1] >= -e && rgb[1] <= 1 + e && rgb[2] >= -e && rgb[2] <= 1 + e;
}
static uint32_t packLin(const float rgb[3]) {
    auto q = [](float v) { return (uint32_t)std::clamp((int)std::lround(l2s(std::clamp(v, 0.0f, 1.0f)) * 255.0f), 0, 255); };
    return q(rgb[0]) << 16 | q(rgb[1]) << 8 | q(rgb[2]);
}
// OKLCH -> sRGB with gamut mapping by chroma reduction (keeps lightness and hue)
static uint32_t oklch(float L, float C, float hdeg) {
    L = std::clamp(L, 0.0f, 1.0f);
    float h = hdeg * 3.14159265f / 180.0f;
    float rgb[3];
    if (fromLab({L, C * std::cos(h), C * std::sin(h)}, rgb)) return packLin(rgb);
    float lo = 0, hi = C;
    for (int i = 0; i < 20; i++) {
        float mid = (lo + hi) / 2;
        if (fromLab({L, mid * std::cos(h), mid * std::sin(h)}, rgb)) lo = mid; else hi = mid;
    }
    fromLab({L, lo * std::cos(h), lo * std::sin(h)}, rgb);
    return packLin(rgb);
}
static void lch(uint32_t c, float& L, float& C, float& H) {
    Lab l = toLab(c);
    L = l.L; C = std::sqrt(l.a * l.a + l.b * l.b);
    H = std::atan2(l.b, l.a) * 180.0f / 3.14159265f;
    if (H < 0) H += 360;
}
float Luma(uint32_t c) { return toLab(c).L; }
float ContrastRatio(uint32_t a, uint32_t b) {
    auto Y = [](uint32_t c) { return 0.2126f * s2l(((c >> 16) & 255) / 255.0f) + 0.7152f * s2l(((c >> 8) & 255) / 255.0f) + 0.0722f * s2l((c & 255) / 255.0f); };
    float x = Y(a), y = Y(b);
    if (x < y) std::swap(x, y);
    return (x + 0.05f) / (y + 0.05f);
}

// ---------------------------------------------------------------------------------------------
// Built-in palettes. Order inside a palette: base / darkest first; neon sets are background + 3 accents.
// ---------------------------------------------------------------------------------------------
const std::vector<Palette>& BuiltinPalettes() {
    static std::vector<Palette> v = [] {
        struct D { const wchar_t* g; const wchar_t* n; uint32_t c[4]; };
        static const D d[] = {
            // 90s Neon - Memphis / Saturday-morning-TV brights
            {L"90s Neon", L"Saturday Morning", {0x120a1e, 0xff2d95, 0x00d4c8, 0xffe14a}},
            {L"90s Neon", L"Hallway Lockers", {0x1b1036, 0xff5fa2, 0x2fd8f0, 0xb5ff4a}},
            {L"90s Neon", L"Jazz Cup", {0xf6f3ec, 0x18b4b0, 0x6b3fa0, 0x222222}},
            {L"90s Neon", L"Memphis Milano", {0xf2ede1, 0xff5ea8, 0x2bb8d6, 0xffc93c}},
            {L"90s Neon", L"Arcade Carpet", {0x0a0614, 0xff3ec9, 0x3ef0ff, 0xfff24a}},
            {L"90s Neon", L"Windbreaker", {0x2a1a5e, 0xff3b6b, 0x14d3c4, 0xffd23f}},
            {L"90s Neon", L"Laser Tag", {0x050010, 0x8a00ff, 0xff00aa, 0x00fff0}},
            {L"90s Neon", L"Slap Bracelet", {0x111111, 0x39ff14, 0xff3cac, 0xffea00}},
            {L"90s Neon", L"Mall Food Court", {0xfff4e0, 0xff7a3d, 0x2ec4b6, 0x7b2ff7}},
            {L"90s Neon", L"Hypercolor", {0x1e0b3d, 0xd62ad0, 0x31e3ff, 0xfff25c}},
            {L"90s Neon", L"Pocket Pager", {0x0b2b26, 0x00f5a0, 0xff6b9d, 0xf9f871}},
            {L"90s Neon", L"Skate Park", {0x18181f, 0xff6f00, 0x00b4ff, 0xe4ff1a}},
            // SGI classics - Indigo Magic desktop moods
            {L"SGI Classic", L"Desktop Blue", {0x3d5a7a, 0x5f7f9f, 0x7d9ec0, 0xa9c1da}},
            {L"SGI Classic", L"Indigo Magic", {0x2b3a55, 0x5a7aa0, 0x8fa9c9, 0xc9d6e3}},
            {L"SGI Classic", L"Toolchest", {0x5a5648, 0x8a8672, 0xc5c1aa, 0xeae7d8}},
            {L"SGI Classic", L"Rose", {0x5a3a46, 0x9a6a78, 0xc49aa6, 0xead6dc}},
            {L"SGI Classic", L"Lascaux", {0x3a2a1e, 0x7a5a3e, 0xb08a62, 0xe0caa8}},
            {L"SGI Classic", L"Pacific", {0x123a4a, 0x2f6f7f, 0x5fa3ad, 0xc4e3e6}},
            {L"SGI Classic", L"Gotham", {0x1a1a22, 0x3a3a48, 0x6a6a7a, 0xaeb0bc}},
            {L"SGI Classic", L"Sierra", {0x2e3b2a, 0x5b6e46, 0x93a46a, 0xd8e0b8}},
            {L"SGI Classic", L"Workstation Teal", {0x0d2f33, 0x1f6b6f, 0x4fa8a3, 0xb9e2dc}},
            {L"SGI Classic", L"Graphite", {0x202124, 0x45474d, 0x7b7e86, 0xc3c6cc}},
            // Fractal Glow - luminous, psychedelic CGI
            {L"Fractal Glow", L"Electropaint", {0x0b0033, 0x6a00ff, 0xff3cac, 0xffd319}},
            {L"Fractal Glow", L"Dreamscape", {0x10002b, 0x5a189a, 0xe0aaff, 0xfff3b0}},
            {L"Fractal Glow", L"Acid Sunset", {0x1a0533, 0xc0175d, 0xff8c42, 0xfff275}},
            {L"Fractal Glow", L"Chrome Dream", {0x0d0d1a, 0x4b5d78, 0xb8c6db, 0xffffff}},
            {L"Fractal Glow", L"Nebula", {0x070b2a, 0x3c1874, 0xd6336c, 0xffc6a8}},
            {L"Fractal Glow", L"Hologram", {0x0a1a2f, 0x3a7ca5, 0x9ef0f0, 0xf0e6ff}},
            {L"Fractal Glow", L"Ultraviolet", {0x0c0021, 0x3e0f6b, 0x9b5de5, 0xf3c4fb}},
            {L"Fractal Glow", L"Infrared", {0x050505, 0x7a0000, 0xff4800, 0xffe066}},
            {L"Fractal Glow", L"Phosphor", {0x020804, 0x0a5a1a, 0x40e060, 0xd0ffd0}},
            {L"Fractal Glow", L"Liquid Gold", {0x120a02, 0x6b4410, 0xd9a332, 0xfff1c1}},
            // Text friendly - calm, low-contrast; dark sets for white icon labels, light sets for dark labels
            {L"Text Friendly", L"Slate Calm", {0x2a3240, 0x323b4b, 0x3b4556, 0x434e60}},
            {L"Text Friendly", L"Deep Navy", {0x141c2b, 0x1b2538, 0x223047, 0x2a3a55}},
            {L"Text Friendly", L"Forest Floor", {0x1f2a22, 0x26342a, 0x2e3e33, 0x37493c}},
            {L"Text Friendly", L"Charcoal", {0x1c1c1e, 0x242426, 0x2c2c2f, 0x353538}},
            {L"Text Friendly", L"Plum Night", {0x231a2b, 0x2b2035, 0x342740, 0x3d2e4b}},
            {L"Text Friendly", L"Teal Shade", {0x163236, 0x1b3c40, 0x21474c, 0x275258}},
            {L"Text Friendly", L"Warm Umber", {0x2d231c, 0x352a22, 0x3e3128, 0x48392f}},
            {L"Text Friendly", L"Dusk Blue", {0x26324a, 0x2c3a55, 0x334361, 0x3a4c6d}},
            {L"Text Friendly", L"Paper Light", {0xe2ddd0, 0xe8e3d6, 0xede9de, 0xf3f0e7}},
            {L"Text Friendly", L"Mist Light", {0xd5dde5, 0xdbe3ea, 0xe1e8ee, 0xe7edf2}},
            // Nature & metal
            {L"Nature & Metal", L"Verde Marble", {0x000000, 0x101033, 0x306636, 0x445b43}},
            {L"Nature & Metal", L"Copper", {0x2b1408, 0x7a3b16, 0xc8743a, 0xf3c39a}},
            {L"Nature & Metal", L"Glacier", {0x0e2233, 0x3b6a8c, 0x9cc7e0, 0xf2fbff}},
            {L"Nature & Metal", L"Autumn", {0x2a0f05, 0x8a2f0e, 0xd8762a, 0xf6d38b}},
            {L"Nature & Metal", L"Desert", {0x3d2b1f, 0x8c6a4a, 0xd1ad7d, 0xf5e4c3}},
            {L"Nature & Metal", L"Moss", {0x14200f, 0x3d5a24, 0x7f9e46, 0xd4e3a3}},
            {L"Nature & Metal", L"Steel", {0x1b1f24, 0x4a525c, 0x8b95a1, 0xd5dbe1}},
            {L"Nature & Metal", L"Coral Reef", {0x0a2e3c, 0x1c7c8c, 0xff7f6b, 0xffe3c2}},
        };
        std::vector<Palette> out;
        for (const D& x : d) {
            Palette p;
            p.group = x.g; p.name = x.n; p.builtin = true;
            for (int i = 0; i < 4; i++) p.c[i] = x.c[i];
            out.push_back(p);
        }
        return out;
    }();
    return v;
}

// ---------------------------------------------------------------------------------------------
// Random generator
// ---------------------------------------------------------------------------------------------
const wchar_t* HarmonyName(int h) {
    static const wchar_t* n[HM_COUNT] = {L"Auto", L"Analogous", L"Complementary", L"Triadic", L"Split Complement", L"Tetradic", L"Monochrome",
                                         L"90s Neon", L"SGI Workstation", L"Psychedelic", L"Pastel Dream", L"Midnight", L"Earth & Metal", L"Calm (Text Friendly)"};
    return n[std::clamp(h, 0, HM_COUNT - 1)];
}
const wchar_t* HarmonyHelp(int h) {
    static const wchar_t* n[HM_COUNT] = {
        L"Picks a scheme that suits this pattern's colour roles",
        L"Neighbouring hues - smooth and natural",
        L"Opposite hues - strong push and pull",
        L"Three evenly spaced hues - lively and balanced",
        L"A hue and the two either side of its opposite",
        L"Four hues spaced around the wheel - maximal variety",
        L"One hue, many shades",
        L"Hot pink, teal, yellow, purple - Memphis and Saturday-morning TV",
        L"Muted Indigo Magic teals, slates and plums",
        L"Big hue jumps at full saturation - luminous early CGI",
        L"Soft, light and airy",
        L"Deep blues and violets with one glowing accent",
        L"Ochres, olives, rust and metal",
        L"Dark, quiet and low-contrast so icon labels stay readable"};
    return n[std::clamp(h, 0, HM_COUNT - 1)];
}

struct PRng {
    uint32_t s;
    explicit PRng(uint32_t seed) : s(seed * 2654435761u ^ 0x9E3779B9u) { if (!s) s = 1; for (int i = 0; i < 4; i++) next(); }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float r(float a, float b) { return a + (b - a) * f(); }
    int i(int n) { return (int)(next() % (uint32_t)n); }
};
static float wrapH(float h) { h = std::fmod(h, 360.0f); return h < 0 ? h + 360 : h; }
static const uint8_t IRIX_DARK_TO_LIGHT[4] = {3, 1, 0, 2};

void RandomPalette(int pattern, int harmony, const uint32_t cur[4], const bool lock[4], uint32_t seed, uint32_t out[4]) {
    const Pattern& p = Patterns()[pattern];
    PRng r(seed);
    int role = p.role;
    int hm = harmony;
    if (hm <= HM_AUTO || hm >= HM_COUNT) {
        if (p.textFriendly) hm = HM_CALM;
        else if (role == ROLE_ACCENT) { static const int c[] = {HM_NEON90, HM_NEON90, HM_NEON90, HM_TRIADIC, HM_TETRADIC, HM_PSYCHE}; hm = c[r.i(6)]; }
        else if (role == ROLE_IRIX || role == ROLE_TWO) { static const int c[] = {HM_SGI, HM_SGI, HM_MONO, HM_ANALOGOUS, HM_EARTH, HM_PASTEL}; hm = c[r.i(6)]; }
        else { static const int c[] = {HM_ANALOGOUS, HM_COMPLEMENTARY, HM_TRIADIC, HM_SPLIT, HM_PSYCHE, HM_MIDNIGHT, HM_SGI, HM_PASTEL}; hm = c[r.i(8)]; }
    }
    // hue offsets per harmony (for the four slots in role order)
    float off[4] = {0, 0, 0, 0};
    float base = r.r(0, 360);
    switch (hm) {
    case HM_ANALOGOUS: { float k = r.r(0.8f, 1.5f); float o[4] = {-27, -9, 9, 27}; for (int i = 0; i < 4; i++) off[i] = o[i] * k; break; }
    case HM_COMPLEMENTARY: { float o[4] = {0, r.r(-12, 12), 180, 180 + r.r(-12, 12)}; for (int i = 0; i < 4; i++) off[i] = o[i]; break; }
    case HM_TRIADIC: { float o[4] = {0, 120, 240, r.r(-15, 15)}; for (int i = 0; i < 4; i++) off[i] = o[i]; break; }
    case HM_SPLIT: { float o[4] = {0, 150, 210, r.r(-15, 15)}; for (int i = 0; i < 4; i++) off[i] = o[i]; break; }
    case HM_TETRADIC: { float o[4] = {0, 90, 180, 270}; for (int i = 0; i < 4; i++) off[i] = o[i]; break; }
    case HM_SGI: base = r.r(180, 300); { float o[4] = {0, 8, 16, 24}; for (int i = 0; i < 4; i++) off[i] = o[i] * r.r(0.6f, 1.4f); } break;
    case HM_PSYCHE: { float h = 0; for (int i = 0; i < 4; i++) { off[i] = h; h += r.r(70, 150); } break; }
    case HM_PASTEL: { float o[4] = {0, 40, 160, 220}; for (int i = 0; i < 4; i++) off[i] = o[i] * r.r(0.5f, 1.2f); break; }
    case HM_MIDNIGHT: base = r.r(215, 290); { float o[4] = {0, 10, 25, r.r(120, 200)}; for (int i = 0; i < 4; i++) off[i] = o[i]; } break;
    case HM_EARTH: base = r.r(20, 105); { float o[4] = {0, 10, -12, 25}; for (int i = 0; i < 4; i++) off[i] = o[i] * r.r(0.6f, 1.3f); } break;
    case HM_CALM: { float o[4] = {0, 4, 8, 12}; for (int i = 0; i < 4; i++) off[i] = o[i]; break; }
    default: break;   // mono
    }
    // locked colours steer the base hue so the new ones fit around them
    for (int i = 0; i < 4; i++)
        if (lock && lock[i]) {
            float L, C, H; lch(cur[i], L, C, H);
            if (C > 0.025f) { base = wrapH(H - off[i]); break; }
        }
    float H[4], L[4], C[4];
    for (int i = 0; i < 4; i++) H[i] = wrapH(base + off[i]);

    if (hm == HM_NEON90) {
        static const float neon[] = {345, 192, 100, 290, 140, 30, 220};   // pink, teal, yellow, purple, lime, orange, blue
        int a = r.i(7), b, c;
        do b = r.i(7); while (b == a);
        do c = r.i(7); while (c == a || c == b);
        H[1] = neon[a]; H[2] = neon[b]; H[3] = neon[c]; H[0] = neon[a] + r.r(-20, 20);
    }
    if (role == ROLE_ACCENT || hm == HM_NEON90) {
        bool darkBg = hm == HM_NEON90 ? r.f() < 0.75f : r.f() < 0.6f;
        if (role != ROLE_ACCENT) darkBg = true;
        L[0] = darkBg ? r.r(0.13f, 0.22f) : r.r(0.93f, 0.97f);
        C[0] = darkBg ? r.r(0.03f, 0.07f) : r.r(0.008f, 0.025f);
        float cmax = hm == HM_NEON90 ? r.r(0.21f, 0.30f) : (hm == HM_SGI || hm == HM_CALM ? r.r(0.05f, 0.09f) : r.r(0.14f, 0.24f));
        for (int i = 1; i < 4; i++) {
            L[i] = darkBg ? r.r(0.66f, 0.89f) : r.r(0.52f, 0.72f);
            C[i] = cmax * r.r(0.8f, 1.0f);
        }
        if (role != ROLE_ACCENT) {   // ramp patterns with a neon request: dark base -> brights, sorted by lightness
            std::sort(L + 1, L + 4);
        }
    } else if (role == ROLE_IRIX) {
        float mid = hm == HM_MIDNIGHT ? r.r(0.3f, 0.45f) : (hm == HM_PASTEL ? r.r(0.8f, 0.9f) : r.r(0.5f, 0.82f));
        float spread = r.r(0.05f, 0.16f);
        float cm = hm == HM_PSYCHE ? r.r(0.12f, 0.2f) : (hm == HM_SGI || hm == HM_MONO || hm == HM_CALM ? r.r(0.015f, 0.06f) : r.r(0.03f, 0.1f));
        for (int k = 0; k < 4; k++) {   // k = darkness rank
            int slot = IRIX_DARK_TO_LIGHT[k];
            L[slot] = mid + spread * (k - 1.5f) / 1.5f;
            C[slot] = cm * r.r(0.7f, 1.1f);
            H[slot] = wrapH(base + off[k] * 0.4f);
        }
    } else if (role == ROLE_TWO) {
        L[0] = r.r(0.3f, 0.85f);
        float d = r.r(0.22f, 0.42f);
        L[1] = L[0] + (L[0] > 0.55f ? -d : d);
        float cm = hm == HM_PSYCHE || hm == HM_NEON90 ? 0.18f : (hm == HM_SGI || hm == HM_MONO || hm == HM_CALM ? 0.04f : 0.1f);
        C[0] = cm * r.r(0.5f, 1.0f); C[1] = cm * r.r(0.6f, 1.1f);
        H[1] = wrapH(base + off[2]);
        L[2] = L[0]; L[3] = L[1]; C[2] = C[0]; C[3] = C[1]; H[2] = H[0]; H[3] = H[1];
    } else if (role == ROLE_SOLID) {
        for (int i = 0; i < 4; i++) { L[i] = r.r(0.35f, 0.75f); C[i] = r.r(0.03f, 0.12f); }
    } else {   // ramp / scene: stops dark -> light
        float L0 = r.r(0.10f, 0.28f), L3 = r.r(0.80f, 0.96f), cmax = r.r(0.10f, 0.19f);
        if (hm == HM_MIDNIGHT) { L0 = r.r(0.07f, 0.14f); L3 = r.r(0.58f, 0.75f); cmax = r.r(0.08f, 0.15f); }
        if (hm == HM_PASTEL) { L0 = r.r(0.55f, 0.68f); L3 = r.r(0.92f, 0.97f); cmax = r.r(0.05f, 0.1f); }
        if (hm == HM_PSYCHE) cmax = r.r(0.18f, 0.28f);
        if (hm == HM_SGI || hm == HM_EARTH) cmax = r.r(0.04f, 0.09f);
        if (hm == HM_MONO) cmax = r.r(0.04f, 0.14f);
        if (hm == HM_CALM) { L0 = r.r(0.22f, 0.32f); L3 = L0 + r.r(0.07f, 0.13f); cmax = r.r(0.015f, 0.05f); }
        float prof[4] = {0.6f, 1.0f, 0.9f, 0.45f};
        if (hm == HM_PSYCHE || hm == HM_NEON90) { prof[0] = 0.7f; prof[3] = 0.75f; }
        L[0] = L0; L[3] = L3;
        L[1] = L0 + (L3 - L0) * r.r(0.25f, 0.42f);
        L[2] = L0 + (L3 - L0) * r.r(0.58f, 0.78f);
        for (int i = 0; i < 4; i++) C[i] = cmax * prof[i] * r.r(0.85f, 1.1f);
    }
    uint32_t gen[4];
    for (int i = 0; i < 4; i++) gen[i] = oklch(L[i], std::max(0.0f, C[i]), H[i]);
    if (hm == HM_NEON90 && (role == ROLE_IRIX || role == ROLE_TWO)) {   // neon set -> this pattern's slot order
        uint32_t m[4];
        ApplyPaletteToPattern(pattern, gen, m);
        for (int i = 0; i < 4; i++) gen[i] = m[i];
    }
    for (int i = 0; i < 4; i++) out[i] = (lock && lock[i]) ? (cur[i] & 0xffffff) : gen[i];
}
uint32_t RandomSlot(int pattern, int harmony, const uint32_t cur[4], int slot, uint32_t seed) {
    bool lock[4] = {true, true, true, true};
    lock[slot & 3] = false;
    uint32_t out[4];
    RandomPalette(pattern, harmony, cur, lock, seed, out);
    return out[slot & 3];
}
void ApplyPaletteToPattern(int pattern, const uint32_t pal[4], uint32_t out[4]) {
    const Pattern& p = Patterns()[pattern];
    for (int i = 0; i < 4; i++) out[i] = pal[i] & 0xffffff;
    if (p.role == ROLE_IRIX) {
        int idx[4] = {0, 1, 2, 3};
        std::sort(idx, idx + 4, [&](int a, int b) { return Luma(pal[a]) < Luma(pal[b]); });
        for (int k = 0; k < 4; k++) out[IRIX_DARK_TO_LIGHT[k]] = pal[idx[k]] & 0xffffff;
    } else if (p.role == ROLE_TWO) {
        int best = 1;
        for (int k = 2; k < 4; k++) if (ContrastRatio(pal[0], pal[k]) > ContrastRatio(pal[0], pal[best])) best = k;
        out[0] = pal[0] & 0xffffff;
        out[1] = pal[best] & 0xffffff;
    }
}

// ---------------------------------------------------------------------------------------------
// User palettes + files
// ---------------------------------------------------------------------------------------------
static std::wstring userPalPath() { return AppDataDir() + L"\\palettes.txt"; }
static bool parseHex(const std::wstring& s, size_t at, uint32_t& c) {
    if (at + 6 > s.size()) return false;
    for (size_t k = at; k < at + 6; k++) if (!iswxdigit(s[k])) return false;
    if (at + 6 < s.size() && iswxdigit(s[at + 6])) return false;
    c = (uint32_t)wcstoul(s.substr(at, 6).c_str(), nullptr, 16);
    return true;
}
std::vector<Palette> LoadUserPalettes() {
    std::vector<Palette> v;
    std::wstring t;
    if (!ReadTextFile(userPalPath(), t)) return v;
    size_t pos = 0;
    while (pos < t.size()) {
        size_t e = t.find(L'\n', pos);
        std::wstring line = t.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos);
        pos = e == std::wstring::npos ? t.size() : e + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        size_t eq = line.rfind(L'=');
        if (eq == std::wstring::npos || eq == 0) continue;
        Palette p;
        p.name = line.substr(0, eq); p.group = L"My Palettes"; p.builtin = false;
        std::wstring cs = line.substr(eq + 1);
        int n = 0;
        for (size_t k = 0; k < cs.size() && n < 4; k++)
            if (cs[k] == L'#' && parseHex(cs, k + 1, p.c[n])) n++;
        if (n == 4) v.push_back(p);
    }
    return v;
}
bool SaveUserPalettes(const std::vector<Palette>& v) {
    std::wstring t = L"; Indigo Magic user palettes: Name=#RRGGBB,#RRGGBB,#RRGGBB,#RRGGBB\r\n";
    for (auto& p : v) {
        if (p.builtin) continue;
        std::wstring nm = p.name;
        for (auto& ch : nm) if (ch == L'=' || ch == L'\r' || ch == L'\n') ch = L' ';
        wchar_t b[64];
        swprintf(b, 64, L"#%06X,#%06X,#%06X,#%06X", p.c[0] & 0xffffff, p.c[1] & 0xffffff, p.c[2] & 0xffffff, p.c[3] & 0xffffff);
        t += nm + L"=" + b + L"\r\n";
    }
    return WriteTextFile(userPalPath(), t);
}
bool ExportPaletteFile(const std::wstring& path, const std::wstring& name, const uint32_t c[4], std::wstring* err) {
    std::wstring t = L"GIMP Palette\r\nName: " + name + L"\r\nColumns: 4\r\n#\r\n";
    for (int i = 0; i < 4; i++) {
        wchar_t b[96];
        swprintf(b, 96, L"%3u %3u %3u\t#%06X\r\n", (c[i] >> 16) & 255, (c[i] >> 8) & 255, c[i] & 255, c[i] & 0xffffff);
        t += b;
    }
    if (!WriteTextFile(path, t)) { if (err) *err = L"Could not write the palette file."; return false; }
    return true;
}
bool ImportPaletteFile(const std::wstring& path, std::wstring& name, uint32_t out[4], std::wstring* err) {
    std::wstring t;
    if (!ReadTextFile(path, t)) { if (err) *err = L"Could not read the file."; return false; }
    std::vector<uint32_t> cols;
    name.clear();
    size_t pos = 0;
    bool gimp = t.compare(0, 12, L"GIMP Palette") == 0 || (t.size() > 13 && t.compare(1, 12, L"GIMP Palette") == 0);
    while (pos < t.size() && cols.size() < 4) {
        size_t e = t.find(L'\n', pos);
        std::wstring line = t.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos);
        pos = e == std::wstring::npos ? t.size() : e + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ' || line.back() == L'\t')) line.pop_back();
        if (line.compare(0, 5, L"Name:") == 0) { name = line.substr(5); while (!name.empty() && name[0] == L' ') name.erase(0, 1); continue; }
        if (gimp) {
            int r, g, b;
            if (swscanf(line.c_str(), L" %d %d %d", &r, &g, &b) == 3 && r >= 0 && r < 256 && g >= 0 && g < 256 && b >= 0 && b < 256) {
                cols.push_back((uint32_t)(r << 16 | g << 8 | b));
                continue;
            }
        }
        for (size_t k = 0; k < line.size() && cols.size() < 4; k++) {
            uint32_t c;
            if (line[k] == L'#' && parseHex(line, k + 1, c)) cols.push_back(c);
        }
    }
    if (cols.empty()) { if (err) *err = L"No colours found. Use a GIMP .gpl palette or a text file with #RRGGBB codes."; return false; }
    auto mix = [](uint32_t a, uint32_t b, float t2) {
        int r = (int)std::lround(((a >> 16) & 255) * (1 - t2) + ((b >> 16) & 255) * t2), g = (int)std::lround(((a >> 8) & 255) * (1 - t2) + ((b >> 8) & 255) * t2),
            bb = (int)std::lround((a & 255) * (1 - t2) + (b & 255) * t2);
        return (uint32_t)(r << 16 | g << 8 | bb);
    };
    if (cols.size() == 1) cols = {cols[0], cols[0], cols[0], cols[0]};
    else if (cols.size() == 2) cols = {cols[0], mix(cols[0], cols[1], 1 / 3.0f), mix(cols[0], cols[1], 2 / 3.0f), cols[1]};
    else if (cols.size() == 3) cols.push_back(mix(cols[1], cols[2], 0.5f));
    for (int i = 0; i < 4; i++) out[i] = cols[i] & 0xffffff;
    if (name.empty()) {
        size_t s = path.find_last_of(L"\\/");
        name = path.substr(s == std::wstring::npos ? 0 : s + 1);
        size_t d = name.rfind(L'.');
        if (d != std::wstring::npos && d > 0) name = name.substr(0, d);
    }
    return true;
}
