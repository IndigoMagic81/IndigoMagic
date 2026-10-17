// Indigo Magic: pattern registry, rendering pipeline,
// adjustments, readability, wallpaper and export.
#include "core.h"
#include "gens.h"
#include "irix_data.h"
#include "noise.h"
#include "crash.h"
#include <exception>
#include <objbase.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <list>
#include <map>

struct IndexImage { int w = 0, h = 0; std::vector<uint8_t> px; };

// ---------------------------------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------------------------------
static CRITICAL_SECTION g_cs;
struct Lock {
    Lock() { EnterCriticalSection(&g_cs); }
    ~Lock() { LeaveCriticalSection(&g_cs); }
};
// Rows are handed out to helper threads. An exception in any thread stops the remaining rows
// and is re-thrown on the calling thread once every helper has finished (the helpers use the
// caller's stack frame, so nothing may unwind it early).
struct PRCtx {
    void (*fn)(int, void*); void* ctx; volatile LONG next; int h;
    volatile LONG failed; std::exception_ptr err; const wchar_t* parentCtx;
};
static void prRun(PRCtx* c) {
    try {
        for (;;) {
            LONG y = InterlockedIncrement(&c->next) - 1;
            if (y >= c->h) break;
            c->fn((int)y, c->ctx);
        }
    } catch (...) {
        if (InterlockedExchange(&c->failed, 1) == 0) c->err = std::current_exception();
        InterlockedExchange(&c->next, c->h);   // the other threads stop at their next row
    }
}
static DWORD WINAPI prThread(LPVOID p) {
    PRCtx* c = (PRCtx*)p;
    CrashThread(L"render helper");
    CrashContext(L"%ls", c->parentCtx);
    prRun(c);
    return 0;
}
void ParallelRows(int h, void (*fn)(int, void*), void* ctx) {
    static int ncpu = [] {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        int n = (int)si.dwNumberOfProcessors;
        wchar_t ov[16];   // IRIXBG_THREADS overrides the count (testing)
        if (GetEnvironmentVariableW(L"IRIXBG_THREADS", ov, 16)) n = _wtoi(ov);
        return std::clamp(n, 1, 32);
    }();
    int nt = std::min(ncpu, h / 8);
    if (nt <= 1) { for (int y = 0; y < h; y++) fn(y, ctx); return; }
    PRCtx c{fn, ctx, 0, h, 0, nullptr, CrashGetContext()};
    HANDLE th[32];
    int made = 0;
    for (int i = 0; i < nt - 1; i++) {
        th[made] = CreateThread(nullptr, 0, prThread, &c, 0, nullptr);
        if (th[made]) made++;
    }
    prRun(&c);
    if (made) WaitForMultipleObjects(made, th, TRUE, INFINITE);
    for (int i = 0; i < made; i++) CloseHandle(th[i]);
    if (c.failed) std::rethrow_exception(c.err);
}

// ---------------------------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------------------------
enum GradId { GR_TWILIGHT, GR_VERTICAL, GR_HORIZONTAL, GR_DIAGONAL, GR_RADIAL, GR_CORNER };
struct GradDef { const wchar_t* name; int id; uint32_t def[4]; const wchar_t* blurb; };
static const GradDef kGrads[] = {
    {L"Twilight", GR_TWILIGHT, {0x0b1030, 0x3a2b6e, 0xb5577b, 0xf4a75b}, L"Dusk sky gradient, top to bottom (IRIX ran /usr/sbin/twilight here; this draws its sky)"},
    {L"Linear Vertical", GR_VERTICAL, {0x24384f, 0x4f6f94, 0x7d9ec0, 0xa9c1da}, L"4-stop gradient, top to bottom"},
    {L"Linear Horizontal", GR_HORIZONTAL, {0x1c3b3b, 0x467a7a, 0x7fb0a8, 0xd7e6c8}, L"4-stop gradient, left to right"},
    {L"Linear Diagonal", GR_DIAGONAL, {0x2b1d3a, 0x6b2d5c, 0xc0504d, 0xf0b85a}, L"4-stop gradient, top-left to bottom-right"},
    {L"Radial", GR_RADIAL, {0xf6f0d8, 0xc9b48a, 0x6e5a3e, 0x241c14}, L"4-stop gradient, centre outwards"},
    {L"Corner Glow", GR_CORNER, {0xffe9b0, 0xe3934a, 0x8e3b46, 0x2a1b3a}, L"4-stop gradient radiating from the top-left corner"},
};

static Pattern makeHeader(const wchar_t* n) {
    Pattern p;
    p.name = n; p.kind = K_HEADER; p.ncolors = 0; p.src = -1;
    return p;
}
static void setSlotNames(Pattern& p) {
    static const wchar_t* irix[4] = {L"Color 1", L"Color 2", L"Color 3", L"Color 4"};
    static const wchar_t* ramp[4] = {L"Stop 1 (low)", L"Stop 2", L"Stop 3", L"Stop 4 (high)"};
    static const wchar_t* acc[4] = {L"Background", L"Accent 1", L"Accent 2", L"Accent 3"};
    for (int i = 0; i < 4; i++) {
        switch (p.role) {
        case ROLE_SOLID: p.slotNames[i] = L"Color"; break;
        case ROLE_TWO: p.slotNames[i] = i == 0 ? L"Background" : L"Foreground"; break;
        case ROLE_RAMP: case ROLE_SCENE: p.slotNames[i] = ramp[i]; break;
        case ROLE_ACCENT: p.slotNames[i] = acc[i]; break;
        default: p.slotNames[i] = irix[i];
        }
    }
}
static bool sortByName(const Pattern& a, const Pattern& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; }
static Pattern gradPattern(const GradDef& g) {
    Pattern p;
    p.name = g.name; p.kind = K_GRAD; p.ncolors = 4; p.src = g.id; p.blurb = g.blurb; p.out = OUT_FIELD; p.role = ROLE_RAMP;
    for (int k = 0; k < 4; k++) p.def[k] = g.def[k];
    return p;
}
static Pattern genPattern(int i) {
    const GenDef2& g = kGens2[i];
    Pattern p;
    p.name = g.name; p.blurb = g.blurb; p.src = i;
    p.kind = g.full ? K_DSCENE : K_DTILE;
    p.genFlags = g.flags;
    bool disc = (g.flags & GF_DISCRETE) != 0, two = (g.flags & GF_TWO) != 0;
    p.ncolors = two ? 2 : 4;
    p.out = disc ? OUT_INDEX : OUT_FIELD;
    p.role = two ? ROLE_TWO : disc ? ROLE_ACCENT : (g.flags & GF_IRIXSLOTS) ? ROLE_IRIX : ROLE_RAMP;
    if (g.flags & GF_IRIXSLOTS) { static const uint8_t o[4] = {3, 1, 0, 2}; for (int k = 0; k < 4; k++) p.stopOrder[k] = o[k]; }
    p.defShading = (g.flags & GF_DIFFUSE) ? SH_DIFFUSE : (g.flags & GF_ORDERED) ? SH_ORDERED : (g.flags & GF_BANDS) ? SH_BANDS : SH_SMOOTH;
    p.defTile = g.full ? 0 : (g.defTile ? g.defTile : 256);
    p.usesTile = !g.full && (p.defTile % 64) == 0;
    p.usesSeed = g.variations;
    p.textFriendly = g.textFriendly;
    for (int k = 0; k < 4; k++) p.def[k] = g.def[k];
    for (int k = 0; k < g.nparams && k < kMaxParams; k++) {
        const ParamDef2& s = g.params[k];
        if (!s.name) continue;
        ParamDef d;
        d.name = s.name; d.min = s.min; d.max = s.max; d.def = s.def; d.decimals = s.decimals;
        if (s.choices) {
            std::wstring all = s.choices;
            size_t a = 0;
            while (a <= all.size()) {
                size_t b = all.find(L'|', a);
                if (b == std::wstring::npos) b = all.size();
                d.choices.push_back(all.substr(a, b - a));
                a = b + 1;
            }
            d.min = 0; d.max = (float)d.choices.size() - 1; d.decimals = 0;
        }
        p.params.push_back(d);
    }
    return p;
}

static std::vector<Pattern> buildPatterns() {
    std::vector<Pattern> v;
    // IRIX Classics: exactly the list of the IRIX 6.5 Background Setting panel (alphabetical, as
    // it showed them) - the 36 pixmaps, bitmaps and backdrops, and Twilight
    std::vector<Pattern> irix, extras;
    for (int i = 0; i < kIrixCount; i++) {
        const IrixEntry& e = kIrix[i];
        Pattern p;
        p.name = std::wstring(e.name, e.name + strlen(e.name));
        p.kind = e.kind; p.ncolors = e.ncolors; p.src = i; p.out = OUT_INDEX;
        p.role = e.kind == K_SOLID ? ROLE_SOLID : (e.kind == K_TILE2 ? ROLE_TWO : ROLE_IRIX);
        p.pixelArt = e.kind == K_TILE4 || e.kind == K_TILE2;
        for (int k = 0; k < 4; k++) p.def[k] = e.colors[k];
        wchar_t b[160];
        if (e.kind == K_SOLID) swprintf(b, 160, L"IRIX solid colour");
        else if (e.kind == K_FULL) swprintf(b, 160, L"IRIX full-screen backdrop  (%dx%d source, 4 colours)", e.w, e.h);
        else swprintf(b, 160, L"IRIX %ls  (%dx%d tile, %d colours)", e.kind == K_TILE2 ? L"bitmap" : L"pixmap", e.w, e.h, e.ncolors);
        p.blurb = b;
        if (e.kind == K_FULL) {
            ParamDef d;
            d.name = L"sgi Wordmark"; d.min = 0; d.max = 1; d.def = 0; d.decimals = 0;
            d.choices = {L"Hidden", L"Shown (bleeds off the bottom, as on IRIX)"};
            p.params.push_back(d);
        }
        if (e.extra) {
            swprintf(b, 160, L"  (%dx%d %ls)", e.w, e.h, e.kind == K_TILE2 ? L"bitmap" : L"pixmap");
            p.blurb = std::wstring(e.blurb, e.blurb + strlen(e.blurb)) + b;
            extras.push_back(p);
        } else {
            irix.push_back(p);
        }
    }
    irix.push_back(gradPattern(kGrads[0]));   // Twilight
    std::sort(irix.begin(), irix.end(), sortByName);
    v.push_back(makeHeader(L"IRIX Classics"));
    for (auto& p : irix) v.push_back(p);
    // IRIX Extras: pixmaps IRIX shipped but never listed, and the X11 root-window bitmaps
    std::sort(extras.begin(), extras.end(), sortByName);
    v.push_back(makeHeader(L"IRIX Extras"));
    for (auto& p : extras) v.push_back(p);
    // the generated families
    for (int fam = 0; fam < FAM_COUNT; fam++) {
        std::vector<Pattern> part;
        for (int i = 0; i < kGens2Count; i++)
            if (kGens2[i].family == fam) part.push_back(genPattern(i));
        if (part.empty()) continue;
        std::sort(part.begin(), part.end(), sortByName);
        v.push_back(makeHeader(kFamilyNames[fam]));
        for (auto& p : part) v.push_back(p);
    }
    v.push_back(makeHeader(L"Gradients"));
    for (size_t g = 1; g < sizeof(kGrads) / sizeof(kGrads[0]); g++) v.push_back(gradPattern(kGrads[g]));
    int cat = 0;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i].kind == K_HEADER) cat = (int)i;
        v[i].category = cat;
        setSlotNames(v[i]);
    }
    // Default Scale / Pixel Size on a 100% (96 dpi) screen, chosen pattern by pattern from side-by-
    // side renders at 1x-4x (tests/zoomsheet.cpp). Small pixel-art tiles read better doubled; fine
    // textures, stone and marble stay 1x. Every tiled default grows with the display scaling (see
    // DefaultZoom), so a pattern looks the same size on a 150% laptop as on a 100% monitor.
    static const wchar_t* kDouble[] = {
        // IRIX classics
        L"Brain", L"Bulge", L"Burlap", L"Cracked", L"Crumpled", L"Escher Knot", L"Moorish", L"Scales", L"Scallop", L"Scribble",
        L"Sprinkle", L"Wicker",
        // IRIX Extras: the 16x16 X11 root bitmaps are tiny at 1x
        L"Boxes", L"Cross Weave", L"Dimples", L"Graph Paper", L"Open Dots", L"Plaid", L"Polka Dots", L"Root Weave", L"Stars", L"Targets",
        L"Tie Fighter", L"Wide Weave", L"Wingdogs",
        // generated: woven bitmaps and design-pixel grids
        L"Herringbone", L"Houndstooth", L"Wickerwork", L"Cobblestone", L"Plasma", L"Topographic", L"Zellige", L"Cellular Automaton", L"Life Colony", L"Sierpinski Carpet", L"Bayer Dither"};
    for (auto& p : v) {
        for (auto n : kDouble) if (p.name == n && IsTiled(p.kind)) p.defZoom = 2;
    }
    return v;
}
static std::vector<Pattern>* g_patterns = nullptr;
void EngineInit() {
    static bool done = false;
    if (done) return;
    done = true;
    InitializeCriticalSection(&g_cs);
    g_patterns = new std::vector<Pattern>(buildPatterns());
}
const std::vector<Pattern>& Patterns() {
    if (!g_patterns) EngineInit();
    return *g_patterns;
}
int FindPattern(const std::wstring& name) {
    const auto& p = Patterns();
    for (size_t i = 0; i < p.size(); i++)
        if (p[i].kind != K_HEADER && _wcsicmp(p[i].name.c_str(), name.c_str()) == 0) return (int)i;
    return -1;
}
// Patterns retired in 2.3 (they did not work as wallpapers). Presets, imported .irixbg files and
// the applied background that name one open its closest relative instead of failing.
static const struct { const wchar_t* from; const wchar_t* to; } kRetired[] = {
    {L"Burning Ship", L"Mandelbrot"}, {L"Fourier Terrain", L"Domain Warp"}, {L"Interference", L"Moire Rings"},
    {L"Metaballs", L"Quasicrystal"}, {L"Kaleidoscope", L"Mandala"}, {L"Hypercube", L"Wire Sphere"},
    {L"Superformula", L"Rose Curves"}, {L"Spirograph", L"Rose Curves"}, {L"Lorenz Butterfly", L"Clifford Attractor"},
    {L"Copper Bars", L"Synth Grid"}, {L"CRT Scanlines", L"Dot Matrix"}, {L"Chrome Spheres", L"Raytraced Floor"},
    {L"Iso Cubes", L"Gouraud Mesh"}, {L"Scribble Hatch", L"Memphis"}, {L"Honeycomb", L"Diamond Lattice"},
    {L"Static", L"Basalt"}};
int FindPatternOrSuccessor(const std::wstring& name) {
    int p = FindPattern(name);
    if (p >= 0) return p;
    for (auto& r : kRetired)
        if (_wcsicmp(r.from, name.c_str()) == 0) return FindPattern(r.to);
    return -1;
}
bool IsRetiredPattern(const std::wstring& name) {
    for (auto& r : kRetired) if (_wcsicmp(r.from, name.c_str()) == 0) return true;
    return false;
}
bool IsTiled(int k) { return k == K_SOLID || k == K_TILE4 || k == K_TILE2 || k == K_GEN || k == K_DTILE; }
bool IsGenerated(int pat) { int k = Patterns()[pat].kind; return k == K_DTILE || k == K_DSCENE; }
bool IsDisplayKind(int k) { return k == K_FULL || k == K_GRAD || k == K_DSCENE; }
bool HasShading(int pat) { const Pattern& p = Patterns()[pat]; return p.out == OUT_FIELD && (p.kind == K_GRAD || p.kind == K_DTILE || p.kind == K_DSCENE); }
bool HasVariations(int pat) { return Patterns()[pat].usesSeed; }
std::wstring SlotName(int pat, int slot) { return Patterns()[pat].slotNames[slot & 3]; }

// ---------------------------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------------------------
bool Adjust::operator==(const Adjust& o) const {
    return brightness == o.brightness && contrast == o.contrast && saturation == o.saturation && hue == o.hue && soften == o.soften &&
           calm == o.calm && textTone == o.textTone && textStrength == o.textStrength;
}
bool Config::operator==(const Config& o) const {
    for (int i = 0; i < 4; i++)
        if (colors[i] != o.colors[i]) return false;
    for (int i = 0; i < kMaxParams; i++)
        if (params[i] != o.params[i]) return false;
    return pattern == o.pattern && seed == o.seed && fit == o.fit && zoom == o.zoom && tileSize == o.tileSize && shading == o.shading &&
           rampRepeat == o.rampRepeat && rampOffset == o.rampOffset && rampMirror == o.rampMirror && rotate == o.rotate &&
           mirrorX == o.mirrorX && mirrorY == o.mirrorY && offsetX == o.offsetX && offsetY == o.offsetY && smoothing == o.smoothing &&
           balance == o.balance && invert == o.invert && adj == o.adj;
}
static int defaultFit(int kind) { return kind == K_FULL ? FIT_SCALE : (IsDisplayKind(kind) ? FIT_STRETCH : FIT_TILE); }
double DisplayScale() {
    static const double scale = [] {
        UINT dx = 0, dy = 0;
        HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        typedef HRESULT(WINAPI * GDFM)(HMONITOR, int, UINT*, UINT*);
        if (HMODULE sh = LoadLibraryW(L"shcore.dll"))
            if (GDFM f = (GDFM)GetProcAddress(sh, "GetDpiForMonitor")) f(mon, 0, &dx, &dy);
        if (!dx) {
            HDC dc = GetDC(nullptr);
            dx = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
            if (dc) ReleaseDC(nullptr, dc);
        }
        return std::clamp(dx / 96.0, 1.0, 4.0);
    }();
    return scale;
}
int DefaultZoom(int pat) {
    const Pattern& p = Patterns()[pat];
    if (!IsTiled(p.kind) || p.kind == K_SOLID) return 1;
    // 1x at 150% is 2x at 100% in physical size: round to the nearest whole size
    return std::clamp((int)(p.defZoom * DisplayScale() + 0.5), 1, 4);
}
int TileZoom(const Config& c) {
    int k = Patterns()[std::clamp(c.pattern, 0, (int)Patterns().size() - 1)].kind;
    return (k == K_DTILE || k == K_SOLID) ? 1 : std::clamp(c.zoom, 1, 4);
}
Config DefaultConfig(int pat) {
    const Pattern& p = Patterns()[pat];
    Config c;
    c.pattern = pat;
    c.zoom = DefaultZoom(pat);
    for (int i = 0; i < 4; i++) c.colors[i] = p.def[i] & 0xffffff;
    for (size_t i = 0; i < p.params.size() && i < (size_t)kMaxParams; i++) c.params[i] = p.params[i].def;
    c.fit = defaultFit(p.kind);
    c.shading = p.defShading;
    return c;
}
static const int kTileSizes[] = {64, 128, 192, 256, 384, 512};
void SanitizeConfig(Config& c) {
    const auto& P = Patterns();
    if (c.pattern < 0 || c.pattern >= (int)P.size() || P[c.pattern].kind == K_HEADER) c.pattern = FindPattern(L"Bulge") >= 0 ? FindPattern(L"Bulge") : 1;
    const Pattern& p = P[c.pattern];
    for (int i = 0; i < 4; i++) c.colors[i] &= 0xffffff;
    for (int i = 0; i < kMaxParams; i++) {
        if (i < (int)p.params.size()) {
            const ParamDef& d = p.params[i];
            float v = std::isfinite(c.params[i]) ? c.params[i] : d.def;
            v = std::clamp(v, std::min(d.min, d.max), std::max(d.min, d.max));
            if (d.decimals == 0) v = std::round(v);
            c.params[i] = v;
        } else {
            c.params[i] = 0;
        }
    }
    if (p.kind == K_FULL) { if (c.fit < FIT_CENTER || c.fit > FIT_STRETCH) c.fit = FIT_SCALE; }
    else c.fit = defaultFit(p.kind);
    c.zoom = std::clamp(c.zoom, 1, 4);
    if (!IsTiled(P[c.pattern].kind) || P[c.pattern].kind == K_SOLID) c.zoom = 1;   // Pixel Size only means something for tiles
    bool okTile = c.tileSize == 0;
    for (int t : kTileSizes) okTile |= c.tileSize == t;
    if (!okTile || !p.usesTile) c.tileSize = 0;
    c.shading = std::clamp(c.shading, 0, 3);
    c.rampRepeat = std::clamp(c.rampRepeat, 1, 8);
    if (!std::isfinite(c.rampOffset)) c.rampOffset = 0;
    c.rampOffset = std::clamp(c.rampOffset, 0.0f, 1.0f);
    c.rampMirror = c.rampMirror ? 1 : 0;
    auto cl = [](float& v, float a, float b) { if (!std::isfinite(v)) v = 0; v = std::clamp(v, a, b); };
    cl(c.adj.brightness, -100, 100); cl(c.adj.contrast, -100, 100); cl(c.adj.saturation, -100, 100); cl(c.adj.hue, -180, 180);
    cl(c.adj.soften, 0, 100); cl(c.adj.calm, 0, 100); cl(c.adj.textStrength, 0, 100);
    c.adj.textTone = std::clamp(c.adj.textTone, 0, 2);
    // Transform
    bool gen = p.kind == K_DTILE || p.kind == K_DSCENE;
    c.rotate = p.kind == K_SOLID ? 0 : pmod(c.rotate, 4);
    c.mirrorX = p.kind != K_SOLID && c.mirrorX ? 1 : 0;
    c.mirrorY = p.kind != K_SOLID && c.mirrorY ? 1 : 0;
    bool tiled = IsTiled(p.kind) && p.kind != K_SOLID;
    cl(c.offsetX, 0, 1); cl(c.offsetY, 0, 1);
    if (!tiled) c.offsetX = c.offsetY = 0;
    c.smoothing = gen ? std::clamp(c.smoothing, 0, 2) : SM_NORMAL;
    cl(c.balance, -100, 100);
    c.invert = c.invert ? 1 : 0;
    if (!HasShading(c.pattern)) { c.balance = 0; c.invert = 0; }
}
int EffectiveTileSize(const Config& c) {
    const Pattern& p = Patterns()[c.pattern];
    if (p.kind != K_DTILE) return 0;
    return (p.usesTile && c.tileSize) ? c.tileSize : p.defTile;
}
std::wstring ConfigKey(const Config& c) {
    wchar_t b[720];
    const Pattern& p = Patterns()[c.pattern];
    swprintf(b, 720,
             L"%d|%06x%06x%06x%06x|%u|%.9g,%.9g,%.9g,%.9g,%.9g,%.9g|f%d|t%d|z%d|s%d|r%d,%.9g,%d|x%d,%d,%d,%.9g,%.9g,%d,%.9g,%d|a%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%.9g",
             c.pattern, c.colors[0], c.colors[1], c.colors[2], c.colors[3], p.usesSeed ? c.seed : 0u, c.params[0], c.params[1], c.params[2],
             c.params[3], c.params[4], c.params[5], p.kind == K_FULL ? c.fit : 0, EffectiveTileSize(c), p.kind == K_DTILE ? c.zoom : 0, c.shading,
             c.rampRepeat, c.rampOffset, c.rampMirror, c.rotate, c.mirrorX, c.mirrorY, c.offsetX, c.offsetY, c.smoothing, c.balance, c.invert,
             c.adj.brightness, c.adj.contrast, c.adj.saturation, c.adj.hue, c.adj.soften, c.adj.calm, c.adj.textTone, c.adj.textStrength);
    return b;
}

// ---- text serialisation
static std::wstring fl(float v) { wchar_t b[40]; swprintf(b, 40, L"%.9g", v); return b; }
std::wstring ConfigKeysText(const Config& c) {
    const Pattern& p = Patterns()[c.pattern];
    wchar_t col[64];
    swprintf(col, 64, L"#%06X,#%06X,#%06X,#%06X", c.colors[0], c.colors[1], c.colors[2], c.colors[3]);
    std::wstring s;
    s += L"Pattern=" + p.name + L"\r\n";
    s += L"Colors=" + std::wstring(col) + L"\r\n";
    s += L"Seed=" + std::to_wstring(c.seed) + L"\r\n";
    s += L"Params=";
    for (int i = 0; i < kMaxParams; i++) s += (i ? L"," : L"") + fl(c.params[i]);
    s += L"\r\n";
    s += L"Fit=" + std::to_wstring(c.fit) + L"\r\n";
    s += L"PixelSize=" + std::to_wstring(c.zoom) + L"\r\n";
    s += L"TileSize=" + std::to_wstring(c.tileSize) + L"\r\n";
    s += L"Shading=" + std::to_wstring(c.shading) + L"\r\n";
    s += L"RampRepeat=" + std::to_wstring(c.rampRepeat) + L"\r\n";
    s += L"RampOffset=" + fl(c.rampOffset) + L"\r\n";
    s += L"RampMirror=" + std::to_wstring(c.rampMirror) + L"\r\n";
    s += L"Rotate=" + std::to_wstring(c.rotate) + L"\r\n";
    s += L"MirrorX=" + std::to_wstring(c.mirrorX) + L"\r\n";
    s += L"MirrorY=" + std::to_wstring(c.mirrorY) + L"\r\n";
    s += L"OffsetX=" + fl(c.offsetX) + L"\r\n";
    s += L"OffsetY=" + fl(c.offsetY) + L"\r\n";
    s += L"Smoothing=" + std::to_wstring(c.smoothing) + L"\r\n";
    s += L"Balance=" + fl(c.balance) + L"\r\n";
    s += L"Invert=" + std::to_wstring(c.invert) + L"\r\n";
    s += L"Brightness=" + fl(c.adj.brightness) + L"\r\n";
    s += L"Contrast=" + fl(c.adj.contrast) + L"\r\n";
    s += L"Saturation=" + fl(c.adj.saturation) + L"\r\n";
    s += L"Hue=" + fl(c.adj.hue) + L"\r\n";
    s += L"Soften=" + fl(c.adj.soften) + L"\r\n";
    s += L"Calm=" + fl(c.adj.calm) + L"\r\n";
    s += L"TextTone=" + std::to_wstring(c.adj.textTone) + L"\r\n";
    s += L"TextStrength=" + fl(c.adj.textStrength) + L"\r\n";
    return s;
}
std::wstring ConfigToText(const Config& c, const std::wstring& name) {
    std::wstring nm = name;
    for (auto& ch : nm) if (ch == L'\r' || ch == L'\n') ch = L' ';
    return L"[IrixBackgroundPreset]\r\nVersion=2\r\nName=" + nm + L"\r\n" + ConfigKeysText(c);
}
static std::wstring trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n\xFEFF"), b = s.find_last_not_of(L" \t\r\n");
    return a == std::wstring::npos ? L"" : s.substr(a, b - a + 1);
}
std::vector<IniSection> ParseIni(const std::wstring& text) {
    std::vector<IniSection> out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find(L'\n', pos);
        std::wstring line = trim(text.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos));
        pos = e == std::wstring::npos ? text.size() + 1 : e + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line[0] == L'[' && line.back() == L']') { out.push_back({line.substr(1, line.size() - 2), {}}); continue; }
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        if (out.empty()) out.push_back({L"", {}});
        std::wstring k = trim(line.substr(0, eq));
        for (auto& ch : k) ch = towlower(ch);
        out.back().kv[k] = trim(line.substr(eq + 1));
    }
    return out;
}
bool ConfigFromKeys(const std::map<std::wstring, std::wstring>& kvIn, Config& out, std::wstring* err) {
    std::map<std::wstring, std::wstring> kv = kvIn;
    int pat = FindPatternOrSuccessor(kv[L"pattern"]);
    if (pat < 0) { if (err) *err = L"Unknown pattern \"" + kv[L"pattern"] + L"\"."; return false; }
    Config c = DefaultConfig(pat);
    auto has = [&](const wchar_t* k) { return kv.count(k) > 0; };
    auto num = [&](const wchar_t* k, float& f) { if (has(k)) f = (float)wcstod(kv[k].c_str(), nullptr); };
    auto inum = [&](const wchar_t* k, int& i) { if (has(k)) i = (int)wcstol(kv[k].c_str(), nullptr, 10); };
    if (has(L"colors")) {
        std::wstring s = kv[L"colors"];
        int n = 0;
        size_t p = 0;
        while (n < 4 && p < s.size()) {
            size_t q = s.find(L',', p);
            std::wstring t = trim(s.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p));
            if (!t.empty() && t[0] == L'#') t.erase(0, 1);
            if (t.size() != 6 || t.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos) { if (err) *err = L"Bad colour value \"" + t + L"\"."; return false; }
            c.colors[n++] = (uint32_t)wcstoul(t.c_str(), nullptr, 16);
            if (q == std::wstring::npos) break;
            p = q + 1;
        }
    }
    if (has(L"seed")) c.seed = (uint32_t)wcstoul(kv[L"seed"].c_str(), nullptr, 10);
    if (has(L"params")) {
        std::wstring s = kv[L"params"];
        size_t p = 0;
        for (int i = 0; i < kMaxParams && p <= s.size(); i++) {
            size_t q = s.find(L',', p);
            c.params[i] = (float)wcstod(s.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p).c_str(), nullptr);
            if (q == std::wstring::npos) break;
            p = q + 1;
        }
    }
    inum(L"fit", c.fit); inum(L"pixelsize", c.zoom); inum(L"tilesize", c.tileSize); inum(L"shading", c.shading);
    inum(L"ramprepeat", c.rampRepeat); num(L"rampoffset", c.rampOffset); inum(L"rampmirror", c.rampMirror);
    num(L"brightness", c.adj.brightness); num(L"contrast", c.adj.contrast); num(L"saturation", c.adj.saturation); num(L"hue", c.adj.hue);
    num(L"soften", c.adj.soften); num(L"calm", c.adj.calm); inum(L"texttone", c.adj.textTone); num(L"textstrength", c.adj.textStrength);
    inum(L"rotate", c.rotate); inum(L"mirrorx", c.mirrorX); inum(L"mirrory", c.mirrorY); num(L"offsetx", c.offsetX); num(L"offsety", c.offsetY);
    inum(L"smoothing", c.smoothing); num(L"balance", c.balance); inum(L"invert", c.invert);
    SanitizeConfig(c);
    out = c;
    return true;
}
bool ConfigFromText(const std::wstring& text, Config& out, std::wstring* name, std::wstring* err) {
    auto secs = ParseIni(text);
    for (auto& s : secs) {
        if (_wcsicmp(s.name.c_str(), L"IrixBackgroundPreset") != 0) continue;
        if (!ConfigFromKeys(s.kv, out, err)) return false;
        if (name) { auto it = s.kv.find(L"name"); *name = it == s.kv.end() ? L"" : it->second; }
        return true;
    }
    if (err) *err = L"This file is not a background preset (.irixbg).";
    return false;
}

// ---- UTF-8 / UTF-16 text files
bool ReadTextFile(const std::wstring& path, std::wstring& out) {
    // share everything: the display watcher may be reading state.txt while the panel replaces it
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart > 32 * 1024 * 1024) { CloseHandle(f); return false; }
    std::string b((size_t)sz.QuadPart, '\0');
    DWORD rd = 0;
    bool ok = sz.QuadPart == 0 || (ReadFile(f, &b[0], (DWORD)b.size(), &rd, nullptr) && rd == b.size());
    CloseHandle(f);
    if (!ok) return false;
    if (b.size() >= 2 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xFE) {
        out.assign((const wchar_t*)(b.data() + 2), (b.size() - 2) / 2);
        return true;
    }
    size_t off = (b.size() >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF) ? 3 : 0;
    int n = MultiByteToWideChar(CP_UTF8, 0, b.data() + off, (int)(b.size() - off), nullptr, 0);
    if (n <= 0 && b.size() > off) n = MultiByteToWideChar(CP_ACP, 0, b.data() + off, (int)(b.size() - off), nullptr, 0);
    out.assign(std::max(0, n), L'\0');
    if (n > 0 && !MultiByteToWideChar(CP_UTF8, 0, b.data() + off, (int)(b.size() - off), &out[0], n)) MultiByteToWideChar(CP_ACP, 0, b.data() + off, (int)(b.size() - off), &out[0], n);
    return true;
}
bool WriteTextFile(const std::wstring& path, const std::wstring& text) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string b(std::max(0, n), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &b[0], n, nullptr, nullptr);
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = b.empty() || (WriteFile(f, b.data(), (DWORD)b.size(), &wr, nullptr) && wr == b.size());
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    // atomic-ish replace so a crash mid-save never leaves a half-written settings or preset file
    // antivirus, search indexing or cloud sync can hold a just-written file for a moment: retry
    for (int attempt = 0; attempt < 10; attempt++) {
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        DWORD e = GetLastError();
        if (e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED && e != ERROR_LOCK_VIOLATION) break;
        Sleep(30 + attempt * 20);
    }
    DeleteFileW(tmp.c_str());
    return false;
}

// ---------------------------------------------------------------------------------------------
// IRIX decoding
// ---------------------------------------------------------------------------------------------
static IndexImage decodeIrix(const IrixEntry& e) {
    IndexImage im; im.w = e.w; im.h = e.h; im.px.resize((size_t)e.w * e.h);
    if (e.kind == K_FULL) {
        size_t o = 0, i = 0;
        while (i < e.size && o < im.px.size()) {
            uint32_t run = 0; int sh = 0;
            for (;;) { uint8_t b = e.data[i++]; run |= (uint32_t)(b & 0x7f) << sh; sh += 7; if (!(b & 0x80)) break; }
            uint8_t v = e.data[i++];
            for (uint32_t k = 0; k < run && o < im.px.size(); k++) im.px[o++] = v;
        }
    } else {
        for (size_t k = 0; k < im.px.size(); k++) im.px[k] = (e.data[k >> 2] >> ((k & 3) * 2)) & 3;
    }
    return im;
}

// ---------------------------------------------------------------------------------------------
// Raw generation (before colour) - cached
// ---------------------------------------------------------------------------------------------
struct Raw {
    int w = 0, h = 0;
    bool field = false;          // f valid (0..1), else idx valid (slots)
    std::vector<float> f;
    std::vector<uint8_t> idx;
    size_t bytes() const { return f.size() * 4 + idx.size() + 64; }
};
typedef std::shared_ptr<const Raw> RawP;
struct RawCache {
    std::list<std::pair<std::wstring, RawP>> lru;
    size_t bytes = 0;
};
static RawCache g_raw;
static RawP rawGet(const std::wstring& k) {
    Lock l;
    for (auto it = g_raw.lru.begin(); it != g_raw.lru.end(); ++it)
        if (it->first == k) { auto v = *it; g_raw.lru.erase(it); g_raw.lru.push_front(v); return v.second; }
    return nullptr;
}
// field cache: up to 420 MB, less on machines with little memory (1/16 of RAM, at least 96 MB)
static size_t rawBudget() {
    static const size_t budget = [] {
        MEMORYSTATUSEX ms{sizeof(ms)};
        size_t b = (size_t)420 << 20;
        if (GlobalMemoryStatusEx(&ms)) b = std::clamp<size_t>((size_t)(ms.ullTotalPhys / 16), (size_t)96 << 20, b);
        return b;
    }();
    return budget;
}
static void rawPut(const std::wstring& k, RawP r) {
    if (r->bytes() > rawBudget() / 2) return;   // a one-off giant (a huge export): don't keep it around
    Lock l;
    for (auto it = g_raw.lru.begin(); it != g_raw.lru.end(); ++it)
        if (it->first == k) { g_raw.bytes -= it->second->bytes(); g_raw.lru.erase(it); break; }
    g_raw.lru.push_front({k, r});
    g_raw.bytes += r->bytes();
    while (g_raw.lru.size() > 2 && (g_raw.bytes > rawBudget() || g_raw.lru.size() > 48)) {
        g_raw.bytes -= g_raw.lru.back().second->bytes();
        g_raw.lru.pop_back();
    }
}
static void percentileNormalize(std::vector<float>& f, float lo, float hi, float gamma) {
    std::vector<float> s;
    size_t step = std::max<size_t>(1, f.size() / 200000);
    for (size_t i = 0; i < f.size(); i += step) s.push_back(std::isfinite(f[i]) ? f[i] : 0.0f);
    if (s.empty()) return;
    size_t ka = (size_t)(lo * (s.size() - 1)), kb = (size_t)(hi * (s.size() - 1));
    std::nth_element(s.begin(), s.begin() + ka, s.end());
    float a = s[ka];
    std::nth_element(s.begin(), s.begin() + kb, s.end());
    float b = s[kb];
    // a flat field (every sample equal) has no range: map it to 0 instead of dividing by ~0,
    // which gave inf * 0 = NaN and then an out-of-range colour index
    float range = b - a;
    float inv = range > 1e-7f * std::max(1.0f, std::fabs(a)) ? 1.0f / range : 0.0f;
    bool g = std::fabs(gamma - 1.0f) > 1e-6f;
    for (auto& v : f) {
        float t = std::isfinite(v) ? sat((v - a) * inv) : 0.0f;
        v = g ? std::pow(t, gamma) : t;
    }
}
// The raw pattern before colour: IRIX index images, or a generator's field / slot image drawn at
// w x h output pixels with px output pixels per design pixel.
static RawP genRaw(const Config& c, int w, int h, bool tile, float px = 1.0f) {
    const Pattern& p = Patterns()[c.pattern];
    wchar_t kb[260];
    swprintf(kb, 260, L"%d|%u|%.9g,%.9g,%.9g,%.9g,%.9g,%.9g|%dx%d|%d|%.6g", c.pattern, p.usesSeed ? c.seed : 0u, c.params[0], c.params[1], c.params[2],
             c.params[3], c.params[4], c.params[5], w, h, tile ? 1 : 0, px);
    std::wstring key = kb;
    if (RawP r = rawGet(key)) return r;
    auto r = std::make_shared<Raw>();
    if (p.kind == K_TILE4 || p.kind == K_TILE2 || p.kind == K_FULL) {
        IndexImage im = decodeIrix(kIrix[p.src]);
        r->w = im.w; r->h = im.h; r->idx = std::move(im.px);
    } else if (p.kind == K_DTILE || p.kind == K_DSCENE) {
        const GenDef2& g = kGens2[p.src];
        GenCtx ctx;
        ctx.w = std::max(1, w); ctx.h = std::max(1, h); ctx.tile = tile; ctx.seed = c.seed;
        for (int i = 0; i < kMaxParams; i++) ctx.p[i] = c.params[i];
        ctx.aspect = (float)ctx.w / ctx.h;
        ctx.px = std::max(1.0f, px);
        ctx.dw = std::max(1, (int)std::lround(ctx.w / ctx.px));
        ctx.dh = std::max(1, (int)std::lround(ctx.h / ctx.px));
        ctx.discrete = (g.flags & GF_DISCRETE) != 0;
        GenOut o;
        g.fn(o, ctx);
        if ((int)o.f.size() != ctx.w * ctx.h) o.f.assign((size_t)ctx.w * ctx.h, 0.0f);
        r->w = ctx.w; r->h = ctx.h;
        if (g.flags & GF_DISCRETE) {
            r->idx.resize(o.f.size());
            for (size_t i = 0; i < o.f.size(); i++) {
                float v = std::isfinite(o.f[i]) ? o.f[i] : 0.0f;
                r->idx[i] = (uint8_t)std::clamp((int)std::lround(v * 3.0f), 0, 3);
            }
        } else {
            r->field = true;
            if (o.normalize) percentileNormalize(o.f, o.lo, o.hi, o.gamma);
            else {
                bool g2 = std::fabs(o.gamma - 1.0f) > 1e-6f;
                for (auto& v : o.f) { float t = std::isfinite(v) ? sat(v) : 0.0f; v = g2 ? std::pow(t, o.gamma) : t; }
            }
            r->f = std::move(o.f);
        }
    } else if (p.kind == K_GRAD) {
        r->w = w; r->h = h; r->field = true;
        r->f.resize((size_t)w * h);
        float diag = std::sqrt((float)w * w + (float)h * h);
        int id = p.src;
        float* f = r->f.data();
        ParallelFor(h, [&](int y) {
            for (int x = 0; x < w; x++) {
                float fx = x + 0.5f, fy = y + 0.5f, t;
                switch (id) {
                case GR_HORIZONTAL: t = fx / w; break;
                case GR_DIAGONAL: t = (fx / w + fy / h) * 0.5f; break;
                case GR_RADIAL: { float dx = fx - w * 0.5f, dy = fy - h * 0.5f; t = std::sqrt(dx * dx + dy * dy) / (diag * 0.5f); break; }
                case GR_CORNER: t = std::sqrt(fx * fx + fy * fy) / diag; break;
                default: t = fy / h; break;
                }
                f[(size_t)y * w + x] = sat(t);
            }
        });
    } else {
        r->w = r->h = 8; r->idx.assign(64, 0);
    }
    rawPut(key, r);
    return r;
}
// IRIX backdrop without its sgi wordmark (the source images crop the wordmark at the bottom edge)
static RawP noLogo(const Config& c, RawP im);

// ---------------------------------------------------------------------------------------------
// Colour mapping
// ---------------------------------------------------------------------------------------------
static inline float rampMap(float t, const Config& c) {
    if (c.invert) t = 1.0f - t;
    if (c.balance != 0) t = std::pow(sat(t), std::exp2(-c.balance / 50.0f));   // +100: 4x more of the high colours
    if (c.rampRepeat == 1 && c.rampOffset == 0 && !c.rampMirror) return t;
    float v = t * c.rampRepeat + c.rampOffset;
    if (c.rampMirror) { float m = fmodp(v, 2.0f); return m <= 1.0f ? m : 2.0f - m; }
    float fr = v - std::floor(v);
    if (fr == 0.0f && v > 0.0f && t >= 1.0f) fr = 1.0f;
    return fr;
}
static void colorizeField(const std::vector<float>& f, int W, int H, const Config& c, uint32_t* out, bool wrap) {
    const Pattern& pat = Patterns()[c.pattern];
    uint32_t col[4];
    for (int i = 0; i < 4; i++) col[i] = c.colors[pat.stopOrder[i] & 3];
    float st[4][3];
    for (int i = 0; i < 4; i++) { st[i][0] = (float)((col[i] >> 16) & 255); st[i][1] = (float)((col[i] >> 8) & 255); st[i][2] = (float)(col[i] & 255); }
    int sh = c.shading;
    if (sh == SH_DIFFUSE) {
        std::vector<float> v((size_t)W * H);
        for (size_t i = 0; i < v.size(); i++) v[i] = sat(rampMap(f[i], c)) * 3.0f;
        for (int y = 0; y < H; y++) {
            bool rev = y & 1;
            for (int k = 0; k < W; k++) {
                int x = rev ? W - 1 - k : k;
                float old = v[(size_t)y * W + x];
                int q = std::clamp((int)std::lround(old), 0, 3);
                out[(size_t)y * W + x] = col[q] & 0xffffff;
                float e = old - q;
                int d = rev ? -1 : 1;
                auto at = [&](int xx, int yy) -> float* {
                    if (wrap) return &v[(size_t)pmod(yy, H) * W + pmod(xx, W)];
                    if (xx < 0 || xx >= W || yy >= H) return nullptr;
                    return &v[(size_t)yy * W + xx];
                };
                if (float* p = at(x + d, y)) *p += e * 7 / 16;
                if (y + 1 < H || wrap) {
                    if (y + 1 < H) {
                        if (float* p = at(x - d, y + 1)) *p += e * 3 / 16;
                        if (float* p = at(x, y + 1)) *p += e * 5 / 16;
                        if (float* p = at(x + d, y + 1)) *p += e * 1 / 16;
                    }
                }
            }
        }
        return;
    }
    ParallelFor(H, [&](int y) {
        for (int x = 0; x < W; x++) {
            float t = sat(rampMap(f[(size_t)y * W + x], c));   // indices below stay in 0..3 even for NaN
            uint32_t o;
            if (sh == SH_BANDS) {
                o = col[std::min(3, (int)(t * 4.0f))];
            } else if (sh == SH_ORDERED) {
                float s = t * 3.0f;
                int i = std::min(2, (int)s);
                o = (s - i) > (bayer8(x, y) + 0.5f) / 64.0f ? col[i + 1] : col[i];
            } else {
                float s = t * 3.0f;
                int i = std::min(2, (int)s);
                float fr = s - i;
                uint32_t hh = hash3(x, y, 0x51ED);
                float n = hf(hh) + hf(hash32(hh)) - 1.0f;   // triangular +-1 LSB: no banding in big gradients
                int ch[3];
                for (int k = 0; k < 3; k++) ch[k] = std::clamp((int)std::lround(st[i][k] + (st[i + 1][k] - st[i][k]) * fr + n), 0, 255);
                o = (uint32_t)(ch[0] << 16 | ch[1] << 8 | ch[2]);
            }
            out[(size_t)y * W + x] = o & 0xffffff;
        }
    });
}

// ---------------------------------------------------------------------------------------------
// Adjustments
// ---------------------------------------------------------------------------------------------
static void boxBlurRGB(std::vector<uint32_t>& px, int W, int H, int r, bool wrap, int passes) {
    if (r < 1 || W < 2 || H < 2) return;
    std::vector<uint32_t> tmp(px.size());
    for (int pass = 0; pass < passes; pass++) {
        // horizontal: px -> tmp
        int rr = std::min(r, W / 2 - 1 > 0 ? W / 2 - 1 : 1);
        ParallelFor(H, [&](int y) {
            const uint32_t* s = &px[(size_t)y * W];
            uint32_t* d = &tmp[(size_t)y * W];
            int n = 2 * rr + 1, sr = 0, sg = 0, sb = 0;
            auto at = [&](int x) { return s[wrap ? pmod(x, W) : std::clamp(x, 0, W - 1)]; };
            for (int k = -rr; k <= rr; k++) { uint32_t c = at(k); sr += (c >> 16) & 255; sg += (c >> 8) & 255; sb += c & 255; }
            for (int x = 0; x < W; x++) {
                d[x] = (uint32_t)((sr / n) << 16 | (sg / n) << 8 | (sb / n));
                uint32_t a = at(x - rr), b = at(x + rr + 1);
                sr += (int)((b >> 16) & 255) - (int)((a >> 16) & 255);
                sg += (int)((b >> 8) & 255) - (int)((a >> 8) & 255);
                sb += (int)(b & 255) - (int)(a & 255);
            }
        });
        // vertical: tmp -> px
        rr = std::min(r, H / 2 - 1 > 0 ? H / 2 - 1 : 1);
        ParallelFor(W, [&](int x) {
            int n = 2 * rr + 1, sr = 0, sg = 0, sb = 0;
            auto at = [&](int y) { return tmp[(size_t)(wrap ? pmod(y, H) : std::clamp(y, 0, H - 1)) * W + x]; };
            for (int k = -rr; k <= rr; k++) { uint32_t c = at(k); sr += (c >> 16) & 255; sg += (c >> 8) & 255; sb += c & 255; }
            for (int y = 0; y < H; y++) {
                px[(size_t)y * W + x] = (uint32_t)((sr / n) << 16 | (sg / n) << 8 | (sb / n));
                uint32_t a = at(y - rr), b = at(y + rr + 1);
                sr += (int)((b >> 16) & 255) - (int)((a >> 16) & 255);
                sg += (int)((b >> 8) & 255) - (int)((a >> 8) & 255);
                sb += (int)(b & 255) - (int)(a & 255);
            }
        });
    }
}
static bool adjustIsIdentity(const Adjust& a) {
    return a.brightness == 0 && a.contrast == 0 && a.saturation == 0 && a.hue == 0 && a.soften == 0 && a.calm == 0 && a.textTone == TT_OFF;
}
// scale: pixels per 1080p-reference pixel (1 for tiles, which are drawn 1:1)
static void applyAdjust(Image& img, const Adjust& a, bool wrap, float scale) {
    if (adjustIsIdentity(a) || img.px.empty()) return;
    int W = img.w, H = img.h;
    bool colourOps = a.brightness != 0 || a.contrast != 0 || a.saturation != 0 || a.hue != 0;
    if (colourOps) {
        float ang = a.hue * PI_F / 180.0f, cs = std::cos(ang), sn = std::sin(ang);
        float m[9] = {0.213f + 0.787f * cs - 0.213f * sn, 0.715f - 0.715f * cs - 0.715f * sn, 0.072f - 0.072f * cs + 0.928f * sn,
                      0.213f - 0.213f * cs + 0.143f * sn, 0.715f + 0.285f * cs + 0.140f * sn, 0.072f - 0.072f * cs - 0.283f * sn,
                      0.213f - 0.213f * cs - 0.787f * sn, 0.715f - 0.715f * cs + 0.715f * sn, 0.072f + 0.928f * cs + 0.072f * sn};
        float satK = 1.0f + a.saturation / 100.0f;
        float conK = a.contrast >= 0 ? 1.0f + a.contrast / 100.0f * 1.5f : 1.0f + a.contrast / 100.0f;
        float bri = a.brightness / 100.0f * 0.5f;
        bool hue = a.hue != 0;
        uint32_t* px = img.px.data();
        ParallelFor(H, [&](int y) {
            for (int x = 0; x < W; x++) {
                uint32_t c = px[(size_t)y * W + x];
                float r = ((c >> 16) & 255) / 255.0f, g = ((c >> 8) & 255) / 255.0f, b = (c & 255) / 255.0f;
                if (hue) {
                    float r2 = m[0] * r + m[1] * g + m[2] * b, g2 = m[3] * r + m[4] * g + m[5] * b, b2 = m[6] * r + m[7] * g + m[8] * b;
                    r = r2; g = g2; b = b2;
                }
                float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                r = l + (r - l) * satK; g = l + (g - l) * satK; b = l + (b - l) * satK;
                r = (r - 0.5f) * conK + 0.5f + bri; g = (g - 0.5f) * conK + 0.5f + bri; b = (b - 0.5f) * conK + 0.5f + bri;
                auto q = [](float v) { return (uint32_t)std::clamp((int)std::lround(v * 255.0f), 0, 255); };
                px[(size_t)y * W + x] = q(r) << 16 | q(g) << 8 | q(b);
            }
        });
    }
    if (a.soften > 0) {
        int r = std::max(1, (int)std::lround(a.soften / 100.0f * 6.0f * scale));
        boxBlurRGB(img.px, W, H, r, wrap, 2);
    }
    if (a.calm > 0) {
        // keep the broad colour layout, flatten local detail (what makes labels hard to read)
        int R = std::max(2, (int)std::lround((8.0f + a.calm / 100.0f * 26.0f) * scale));
        if (wrap) R = std::min(R, std::max(2, std::min(W, H) / 3));
        Image b = img;
        boxBlurRGB(b.px, W, H, R, wrap, 2);
        float k = 1.0f - a.calm / 100.0f * 0.92f;
        uint32_t* px = img.px.data();
        const uint32_t* bp = b.px.data();
        ParallelFor(H, [&](int y) {
            for (int x = 0; x < W; x++) {
                size_t i = (size_t)y * W + x;
                uint32_t c = px[i], m = bp[i];
                int ch[3];
                for (int s = 0; s < 3; s++) {
                    int sh = 16 - 8 * s;
                    float v = ((m >> sh) & 255) + (((c >> sh) & 255) - (float)((m >> sh) & 255)) * k;
                    ch[s] = std::clamp((int)std::lround(v), 0, 255);
                }
                px[i] = (uint32_t)(ch[0] << 16 | ch[1] << 8 | ch[2]);
            }
        });
    }
    if (a.textTone != TT_OFF && a.textStrength > 0) {
        // compress the luminance range away from the label colour (soft knee keeps the image's character)
        float s = a.textStrength / 100.0f;
        bool light = a.textTone == TT_LIGHT;
        float lim = lerpf(1.0f, 0.30f, s);   // light labels: max luma; dark labels: max (1 - luma)
        float knee = lim * 0.55f;
        uint32_t* px = img.px.data();
        ParallelFor(H, [&](int y) {
            for (int x = 0; x < W; x++) {
                size_t i = (size_t)y * W + x;
                uint32_t c = px[i];
                float r = ((c >> 16) & 255) / 255.0f, g = ((c >> 8) & 255) / 255.0f, b = (c & 255) / 255.0f;
                float l = 0.299f * r + 0.587f * g + 0.114f * b;
                float m = light ? l : 1.0f - l;
                if (m <= knee || m <= 1e-5f) continue;
                float t = (m - knee) / (1.0f - knee);
                float m2 = knee + (lim - knee) * (1.0f - (1.0f - t) * (1.0f - t));
                float k = m2 / m;
                if (light) { r *= k; g *= k; b *= k; }
                else { r = 1 - (1 - r) * k; g = 1 - (1 - g) * k; b = 1 - (1 - b) * k; }
                auto q = [](float v) { return (uint32_t)std::clamp((int)std::lround(v * 255.0f), 0, 255); };
                px[i] = q(r) << 16 | q(g) << 8 | q(b);
            }
        });
    }
}

// ---------------------------------------------------------------------------------------------
// Supersampling: generators draw at ss x the output resolution; the colours are then averaged
// down in linear light (an exact box filter), which antialiases every edge and line
// ---------------------------------------------------------------------------------------------
struct LinLut {
    float toLin[256];
    uint8_t toSrgb[4097];
    LinLut() {
        for (int i = 0; i < 256; i++) { float v = i / 255.0f; toLin[i] = v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }
        for (int i = 0; i <= 4096; i++) {
            float v = i / 4096.0f;
            float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
            toSrgb[i] = (uint8_t)std::clamp((int)std::lround(s * 255.0f), 0, 255);
        }
    }
};
static const LinLut& linLut() { static const LinLut l; return l; }
static void downsampleRGB(const uint32_t* src, int SW, int SH, int ss, Image& out) {
    out.w = SW / ss; out.h = SH / ss;
    out.px.assign((size_t)out.w * out.h, 0);
    if (ss <= 1) { std::copy(src, src + (size_t)out.w * out.h, out.px.begin()); return; }
    const LinLut& L = linLut();
    float inv = 4096.0f / (ss * ss);
    int W = out.w;
    uint32_t* dst = out.px.data();
    ParallelFor(out.h, [&](int y) {
        for (int x = 0; x < W; x++) {
            float r = 0, g = 0, b = 0;
            for (int j = 0; j < ss; j++) {
                const uint32_t* row = src + (size_t)(y * ss + j) * SW + x * ss;
                for (int i = 0; i < ss; i++) { uint32_t c = row[i]; r += L.toLin[(c >> 16) & 255]; g += L.toLin[(c >> 8) & 255]; b += L.toLin[c & 255]; }
            }
            dst[(size_t)y * W + x] = (uint32_t)L.toSrgb[std::min(4096, (int)(r * inv + 0.5f))] << 16 | (uint32_t)L.toSrgb[std::min(4096, (int)(g * inv + 0.5f))] << 8 |
                                     L.toSrgb[std::min(4096, (int)(b * inv + 0.5f))];
        }
    });
}
static void downsampleField(const std::vector<float>& f, int SW, int SH, int ss, std::vector<float>& out) {
    int W = SW / ss, H = SH / ss;
    out.assign((size_t)W * H, 0.0f);
    float inv = 1.0f / (ss * ss);
    float* dst = out.data();
    ParallelFor(H, [&](int y) {
        for (int x = 0; x < W; x++) {
            float s = 0;
            for (int j = 0; j < ss; j++) for (int i = 0; i < ss; i++) s += f[(size_t)(y * ss + j) * SW + x * ss + i];
            dst[(size_t)y * W + x] = s * inv;
        }
    });
}
// Supersampling factor for a generated pattern drawn at W x H output pixels
static int chooseSS(const Pattern& p, int W, int H, bool tile, int smoothing, bool draft) {
    if (draft || smoothing == SM_OFF) return 1;
    bool edges = (p.genFlags & (GF_EDGES | GF_DISCRETE)) != 0;
    if (p.genFlags & GF_PIXEL) edges = false;   // square design pixels: exact at any whole Scale
    if (!edges) return smoothing == SM_HIGH ? 2 : 1;   // smooth fields are clean at native resolution
    int ss = tile ? (smoothing == SM_HIGH ? 4 : 2) : (smoothing == SM_HIGH ? 3 : 2);
    double budget = tile ? (smoothing == SM_HIGH ? 3072.0 * 3072.0 : 2048.0 * 2048.0) : (smoothing == SM_HIGH ? 24e6 : 17e6);
    while (ss > 1 && (double)W * H * ss * ss > budget) ss--;
    return ss;
}
// Colour a raw image drawn at ss x the output size and bring it down to the output size
static void colourDown(const Raw& r, const Config& c, int ss, bool wrap, Image& out) {
    const Pattern& p = Patterns()[c.pattern];
    if (r.field) {
        if (ss > 1 && (c.shading == SH_ORDERED || c.shading == SH_DIFFUSE)) {
            // dithers are drawn on the output pixels: average the field first
            std::vector<float> f;
            downsampleField(r.f, r.w, r.h, ss, f);
            out.w = r.w / ss; out.h = r.h / ss;
            out.px.resize((size_t)out.w * out.h);
            colorizeField(f, out.w, out.h, c, out.px.data(), wrap);
            return;
        }
        std::vector<uint32_t> hi((size_t)r.w * r.h);
        colorizeField(r.f, r.w, r.h, c, hi.data(), wrap);
        downsampleRGB(hi.data(), r.w, r.h, ss, out);
    } else {
        uint32_t pal[4];
        for (int i = 0; i < 4; i++) pal[i] = c.colors[i] & 0xffffff;
        if (p.kind == K_TILE2 || p.ncolors == 2) pal[2] = pal[0], pal[3] = pal[1];
        if (ss <= 1) {
            out.w = r.w; out.h = r.h;
            out.px.resize(r.idx.size());
            for (size_t i = 0; i < r.idx.size(); i++) out.px[i] = pal[r.idx[i] & 3];
            return;
        }
        std::vector<uint32_t> hi(r.idx.size());
        for (size_t i = 0; i < r.idx.size(); i++) hi[i] = pal[r.idx[i] & 3];
        downsampleRGB(hi.data(), r.w, r.h, ss, out);
    }
}
// Transform tab: quarter turns, mirrors (and for tiles a wrapped shift) of the finished image
static void orientImage(Image& img, int rotate, int mx, int my) {
    rotate = pmod(rotate, 4);
    if (!rotate && !mx && !my) return;
    int W = img.w, H = img.h;
    int OW = (rotate & 1) ? H : W, OH = (rotate & 1) ? W : H;
    std::vector<uint32_t> o((size_t)OW * OH);
    const uint32_t* s = img.px.data();
    ParallelFor(OH, [&](int y) {
        for (int x = 0; x < OW; x++) {
            int X = mx ? OW - 1 - x : x, Y = my ? OH - 1 - y : y;   // mirror in the output frame
            int sx, sy;
            switch (rotate) {   // clockwise
            case 1: sx = Y; sy = H - 1 - X; break;
            case 2: sx = W - 1 - X; sy = H - 1 - Y; break;
            case 3: sx = W - 1 - Y; sy = X; break;
            default: sx = X; sy = Y;
            }
            o[(size_t)y * OW + x] = s[(size_t)sy * W + sx];
        }
    });
    img.w = OW; img.h = OH; img.px.swap(o);
}
static void shiftImage(Image& img, float ox, float oy) {
    int dx = (int)std::lround(ox * img.w), dy = (int)std::lround(oy * img.h);
    if ((!dx && !dy) || img.w <= 0 || img.h <= 0) return;
    std::vector<uint32_t> o(img.px.size());
    int W = img.w, H = img.h;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) o[(size_t)pmod(y + dy, H) * W + pmod(x + dx, W)] = img.px[(size_t)y * W + x];
    img.px.swap(o);
}

// ---------------------------------------------------------------------------------------------
// Tiles
// ---------------------------------------------------------------------------------------------
struct TileCache { std::list<std::pair<std::wstring, std::shared_ptr<const Image>>> lru; size_t bytes = 0; };
static TileCache g_tiles;
void EngineTrimCaches() {
    Lock l;
    g_raw.lru.clear();
    g_raw.bytes = 0;
    g_tiles.lru.clear();
    g_tiles.bytes = 0;
}
std::shared_ptr<const Image> RenderTile(const Config& cfgIn, bool draft) {
    Config c = cfgIn;
    SanitizeConfig(c);
    const Pattern& p = Patterns()[c.pattern];
    std::wstring key = ConfigKey(c) + (draft ? L"|draft" : L"");
    {
        Lock l;
        for (auto it = g_tiles.lru.begin(); it != g_tiles.lru.end(); ++it)
            if (it->first == key) { auto v = *it; g_tiles.lru.erase(it); g_tiles.lru.push_front(v); return v.second; }
    }
    auto img = std::make_shared<Image>();
    float adjScale = 1.0f;
    if (p.kind == K_SOLID) {
        img->w = img->h = 8;
        img->px.assign(64, c.colors[0] & 0xffffff);
    } else if (p.kind == K_DTILE) {
        // drawn natively at its Scale (zoom) and supersampled: smooth at any size
        int z = std::clamp(c.zoom, 1, 4), N = EffectiveTileSize(c) * z;
        int ss = chooseSS(p, N, N, true, c.smoothing, draft);
        RawP r = genRaw(c, N * ss, N * ss, true, (float)(z * ss));
        colourDown(*r, c, ss, true, *img);
        adjScale = (float)z;
    } else {
        RawP r = genRaw(c, 0, 0, true);
        colourDown(*r, c, 1, true, *img);
    }
    applyAdjust(*img, c.adj, true, adjScale);
    orientImage(*img, c.rotate, c.mirrorX, c.mirrorY);
    shiftImage(*img, c.offsetX, c.offsetY);
    std::shared_ptr<const Image> res = img;
    Lock l;
    g_tiles.lru.push_front({key, res});
    g_tiles.bytes += res->px.size() * 4;
    while (g_tiles.lru.size() > 1 && (g_tiles.lru.size() > 96 || g_tiles.bytes > ((size_t)192 << 20))) {
        g_tiles.bytes -= g_tiles.lru.back().second->px.size() * 4;
        g_tiles.lru.pop_back();
    }
    return res;
}
std::shared_ptr<const Image> PeekTile(const Config& cfgIn, bool draft) {
    Config c = cfgIn;
    SanitizeConfig(c);
    std::wstring key = ConfigKey(c) + (draft ? L"|draft" : L"");
    Lock l;
    for (auto& e : g_tiles.lru)
        if (e.first == key) return e.second;
    return nullptr;
}
void RenderRegionFrom(const Config& c, const Image* T, uint32_t* dst, int stride, int vx, int vy, int w, int h) {
    if (!T || T->w <= 0 || T->h <= 0 || !dst || w <= 0 || h <= 0) return;
    int z = TileZoom(c);
    int tw = T->w * z, th = T->h * z;
    std::vector<int> col(w);
    for (int x = 0; x < w; x++) col[x] = pmod(vx + x, tw) / z;   // anchored at virtual (0,0) = primary display origin
    ParallelFor(h, [&](int y) {
        const uint32_t* row = &T->px[(size_t)(pmod(vy + y, th) / z) * T->w];
        uint32_t* d = dst + (size_t)y * stride;
        for (int x = 0; x < w; x++) d[x] = row[col[x]];
    });
}
void RenderRegion(const Config& c, uint32_t* dst, int stride, int vx, int vy, int w, int h) {
    if (!dst || w <= 0 || h <= 0) return;
    auto t = RenderTile(c);
    RenderRegionFrom(c, t.get(), dst, stride, vx, vy, w, h);
}

// ---------------------------------------------------------------------------------------------
// Display kinds
// ---------------------------------------------------------------------------------------------
struct FullMeta { int bg; RECT bar; RECT logo; };
static FullMeta fullMeta(int pat, const Raw& im) {
    static std::map<int, FullMeta> m;
    {
        Lock l;
        auto it = m.find(pat);
        if (it != m.end()) return it->second;
    }
    int cnt[4] = {0, 0, 0, 0};
    for (uint8_t v : im.idx) cnt[v & 3]++;
    int bg = (int)(std::max_element(cnt, cnt + 4) - cnt);
    RECT bar{im.w, im.h, -1, -1}, logo{im.w, im.h, -1, -1};
    for (int y = 0; y < im.h; y++)
        for (int x = 0; x < im.w; x++)
            if (im.idx[(size_t)y * im.w + x] != bg) {
                RECT& r = (y < im.h / 2) ? bar : logo;
                r.left = std::min<LONG>(r.left, x); r.top = std::min<LONG>(r.top, y);
                r.right = std::max<LONG>(r.right, x + 1); r.bottom = std::max<LONG>(r.bottom, y + 1);
            }
    auto pad = [&](RECT& r) {
        if (r.right < 0) { r = RECT{0, 0, 0, 0}; return; }
        r.left = std::max<LONG>(0, r.left - 3); r.top = std::max<LONG>(0, r.top - 3);
        r.right = std::min<LONG>(im.w, r.right + 3); r.bottom = std::min<LONG>(im.h, r.bottom + 3);
    };
    pad(bar); pad(logo);
    FullMeta fm{bg, bar, logo};
    Lock l;
    m[pat] = fm;
    return fm;
}
static void blitScaled(const Raw& im, const uint32_t pal[4], RECT sr, uint32_t* dst, int stride, int W, int H, double dx0, double dy0, double dw, double dh) {
    if (dw <= 0 || dh <= 0 || sr.right <= sr.left || sr.bottom <= sr.top) return;
    double sx = (sr.right - sr.left) / dw, sy = (sr.bottom - sr.top) / dh;
    int kx = std::clamp((int)std::ceil(sx), 1, 6), ky = std::clamp((int)std::ceil(sy), 1, 6);
    int X0 = std::max(0, (int)std::floor(dx0)), X1 = std::min(W, (int)std::ceil(dx0 + dw));
    int Y0 = std::max(0, (int)std::floor(dy0)), Y1 = std::min(H, (int)std::ceil(dy0 + dh));
    float pr[4], pg[4], pb[4];
    for (int i = 0; i < 4; i++) { pr[i] = (float)((pal[i] >> 16) & 255); pg[i] = (float)((pal[i] >> 8) & 255); pb[i] = (float)(pal[i] & 255); }
    auto at = [&](int x, int y) {
        x = std::clamp(x, (int)sr.left, (int)sr.right - 1); y = std::clamp(y, (int)sr.top, (int)sr.bottom - 1);
        return im.idx[(size_t)y * im.w + x] & 3;
    };
    bool exact = std::fabs(sx - 1.0) < 1e-9 && std::fabs(sy - 1.0) < 1e-9;
    if (Y1 <= Y0) return;
    ParallelFor(Y1 - Y0, [&](int yy) {
        int Y = Y0 + yy;
        for (int X = X0; X < X1; X++) {
            if (exact) {
                int x = sr.left + (int)std::floor(X - dx0 + 1e-6), y = sr.top + (int)std::floor(Y - dy0 + 1e-6);
                if (x >= sr.left && x < sr.right && y >= sr.top && y < sr.bottom) dst[(size_t)Y * stride + X] = pal[im.idx[(size_t)y * im.w + x] & 3];
                continue;
            }
            float r = 0, g = 0, b = 0;
            for (int j = 0; j < ky; j++)
                for (int i = 0; i < kx; i++) {
                    double fx = sr.left + (X + (i + 0.5) / kx - dx0) * sx - 0.5;
                    double fy = sr.top + (Y + (j + 0.5) / ky - dy0) * sy - 0.5;
                    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
                    float tx = (float)(fx - x0), ty = (float)(fy - y0);
                    int a = at(x0, y0), bb = at(x0 + 1, y0), cc = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
                    float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
                    r += pr[a] * w00 + pr[bb] * w10 + pr[cc] * w01 + pr[d] * w11;
                    g += pg[a] * w00 + pg[bb] * w10 + pg[cc] * w01 + pg[d] * w11;
                    b += pb[a] * w00 + pb[bb] * w10 + pb[cc] * w01 + pb[d] * w11;
                }
            float n = 1.0f / (kx * ky);
            dst[(size_t)Y * stride + X] = (uint32_t)((int)(r * n + 0.5f) << 16 | (int)(g * n + 0.5f) << 8 | (int)(b * n + 0.5f));
        }
    });
}
static RawP noLogo(const Config& c, RawP im) {
    std::wstring key = L"nologo|" + std::to_wstring(c.pattern);
    if (RawP r = rawGet(key)) return r;
    FullMeta fm = fullMeta(c.pattern, *im);
    auto r = std::make_shared<Raw>(*im);
    for (int y = im->h / 2; y < im->h; y++)
        for (int x = 0; x < im->w; x++) r->idx[(size_t)y * im->w + x] = (uint8_t)fm.bg;
    rawPut(key, r);
    return r;
}
static void renderDisplayUpright(const Config& c, Image& out, int W, int H) {
    const Pattern& p = Patterns()[c.pattern];
    out.w = W; out.h = H;
    out.px.assign((size_t)W * H, 0);
    if (IsTiled(p.kind)) { RenderRegion(c, out.px.data(), W, 0, 0, W, H); return; }
    float scale = H / 1080.0f;
    if (p.kind == K_FULL) {
        RawP im = genRaw(c, 0, 0, false);
        FullMeta fm = fullMeta(c.pattern, *im);
        bool logo = c.params[0] >= 0.5f;
        if (!logo) im = noLogo(c, im);
        uint32_t pal[4];
        for (int i = 0; i < 4; i++) pal[i] = c.colors[i] & 0xffffff;
        std::fill(out.px.begin(), out.px.end(), pal[fm.bg]);
        RECT whole{0, 0, im->w, im->h};
        if (c.fit == FIT_STRETCH) blitScaled(*im, pal, whole, out.px.data(), W, W, H, 0, 0, W, H);
        else if (c.fit == FIT_CENTER) blitScaled(*im, pal, whole, out.px.data(), W, W, H, std::floor((W - im->w) / 2.0), std::floor((H - im->h) / 2.0), im->w, im->h);
        else {
            double sc = std::min((double)W / im->w, (double)H / im->h);
            RECT b = fm.bar, l = fm.logo;
            blitScaled(*im, pal, b, out.px.data(), W, W, H, b.left * sc, b.top * sc, (b.right - b.left) * sc, (b.bottom - b.top) * sc);
            if (logo) {   // the wordmark keeps its place against the bottom edge, as IRIX drew it
                double cx = (l.left + l.right) * 0.5 / im->w * W;
                double lw = (l.right - l.left) * sc, lh = (l.bottom - l.top) * sc;
                blitScaled(*im, pal, l, out.px.data(), W, W, H, cx - lw / 2, H - (im->h - l.bottom) * sc - lh, lw, lh);
            }
        }
    } else if (p.kind == K_DSCENE) {
        int ss = chooseSS(p, W, H, false, c.smoothing, false);
        RawP r = genRaw(c, W * ss, H * ss, false, (float)ss);
        if (r->w != W * ss || r->h != H * ss) return;
        colourDown(*r, c, ss, false, out);
    } else {
        RawP r = genRaw(c, W, H, false);
        if (r->w != W || r->h != H) return;
        colourDown(*r, c, 1, false, out);
    }
    applyAdjust(out, c.adj, false, scale);
}
void RenderDisplay(const Config& cfgIn, Image& out, int W, int H) {
    Config c = cfgIn;
    SanitizeConfig(c);
    const Pattern& p = Patterns()[c.pattern];
    W = std::max(1, W); H = std::max(1, H);
    if (IsTiled(p.kind)) {   // tiles: the transform is already in the tile
        out.w = W; out.h = H;
        out.px.assign((size_t)W * H, 0);
        RenderRegion(c, out.px.data(), W, 0, 0, W, H);
        return;
    }
    // a scene turned a quarter is drawn for the turned display, then turned into place
    bool quarter = c.rotate & 1;
    renderDisplayUpright(c, out, quarter ? H : W, quarter ? W : H);
    orientImage(out, c.rotate, c.mirrorX, c.mirrorY);
}
bool RenderToSize(const Config& c, int w, int h, Image& out) {
    if (w < 1 || h < 1 || (size_t)w * h > (size_t)16384 * 16384) return false;
    RenderDisplay(c, out, w, h);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Readability (label legibility against the background)
// ---------------------------------------------------------------------------------------------
static inline float lin(float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }
Readability AnalyzeReadability(const Image& img, int tone) {
    Readability r;
    r.lightLabels = tone != TT_DARK;
    if (img.px.empty()) return r;
    int W = img.w, H = img.h;
    int step = std::max(1, (int)std::sqrt((double)W * H / 60000.0));
    struct Lut { float v[256]; Lut() { for (int i = 0; i < 256; i++) v[i] = lin(i / 255.0f); } };
    static const Lut lut;   // initialised once, thread-safely (preview worker and export tasks share it)
    const float* LUT = lut.v;
    std::vector<float> L;
    double diff = 0;
    int nd = 0;
    for (int y = 0; y < H; y += step)
        for (int x = 0; x < W; x += step) {
            uint32_t c = img.px[(size_t)y * W + x];
            float l = 0.2126f * LUT[(c >> 16) & 255] + 0.7152f * LUT[(c >> 8) & 255] + 0.0722f * LUT[c & 255];
            L.push_back(l);
            if (x + 1 < W) {
                uint32_t d = img.px[(size_t)y * W + x + 1];
                float l2 = 0.2126f * LUT[(d >> 16) & 255] + 0.7152f * LUT[(d >> 8) & 255] + 0.0722f * LUT[d & 255];
                diff += std::fabs(std::sqrt(l) - std::sqrt(l2));
                nd++;
            }
        }
    float worst;
    if (r.lightLabels) {
        size_t k = (size_t)(0.9 * (L.size() - 1));
        std::nth_element(L.begin(), L.begin() + k, L.end());
        worst = L[k];
        r.ratio = (1.0f + 0.05f) / (worst + 0.05f);
    } else {
        size_t k = (size_t)(0.1 * (L.size() - 1));
        std::nth_element(L.begin(), L.begin() + k, L.end());
        worst = L[k];
        r.ratio = (worst + 0.05f) / 0.05f;
    }
    r.busy = nd ? std::clamp((float)(diff / nd) * 6.0f, 0.0f, 1.0f) : 0.0f;
    float eff = r.ratio / (1.0f + 1.5f * r.busy);
    r.score = eff >= 7.0f ? 3 : eff >= 4.5f ? 2 : eff >= 3.0f ? 1 : 0;
    return r;
}
int MakeTextFriendly(Config& c, int tone) {
    // gentlest calm/strength combination that reaches "Good" label contrast
    if (tone != TT_LIGHT && tone != TT_DARK) tone = TT_LIGHT;
    static const float calms[] = {30, 45, 60, 80};
    static const float strengths[] = {25, 40, 55, 70, 85, 100};
    // never end up worse than where we started (an already Excellent background stays Excellent)
    Image im0;
    RenderDisplay(c, im0, 320, 180);
    Readability r0 = AnalyzeReadability(im0, tone);
    int target = std::max(2, r0.score);
    Config best = c;
    int bestScore = r0.score;
    float bestRatio = r0.ratio;
    for (float cm : calms)
        for (float st : strengths) {
            Config t = c;
            t.adj.textTone = tone;
            t.adj.calm = std::max(c.adj.calm, cm);
            t.adj.textStrength = st;
            Image im;
            RenderDisplay(t, im, 320, 180);
            Readability r = AnalyzeReadability(im, tone);
            if (r.score >= target) { c = t; return r.score; }
            if (r.score > bestScore || (r.score == bestScore && r.ratio > bestRatio)) { best = t; bestScore = r.score; bestRatio = r.ratio; }
        }
    c = best;
    return bestScore;
}
const wchar_t* ReadabilityWord(int s) {
    static const wchar_t* w[4] = {L"Poor", L"Fair", L"Good", L"Excellent"};
    return w[std::clamp(s, 0, 3)];
}

// ---------------------------------------------------------------------------------------------
// Displays, wallpaper, files
// ---------------------------------------------------------------------------------------------
static BOOL CALLBACK monEnum(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfoW(m, &mi)) ((std::vector<DisplayInfo>*)lp)->push_back({mi.rcMonitor, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
    return TRUE;
}
std::vector<DisplayInfo> EnumDisplays() {
    std::vector<DisplayInfo> v;
    EnumDisplayMonitors(nullptr, nullptr, monEnum, (LPARAM)&v);
    if (v.empty()) v.push_back({RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)}, true});
    return v;
}
std::wstring AppDataDir() {
    wchar_t buf[MAX_PATH] = L"";
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring d = (n && n < MAX_PATH) ? std::wstring(buf) : std::wstring(L".");
    d += L"\\IrixBackground";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
bool ComposeDesktop(const Config& c, Image& out, RECT* vrOut) {
    const Pattern& p = Patterns()[c.pattern];
    auto disp = EnumDisplays();
    RECT vr = disp[0].rc;
    for (auto& d : disp) UnionRect(&vr, &vr, &d.rc);
    int W = vr.right - vr.left, H = vr.bottom - vr.top;
    if (W <= 0 || H <= 0) return false;
    out.w = W; out.h = H;
    out.px.assign((size_t)W * H, 0);
    if (IsTiled(p.kind)) {
        RenderRegion(c, out.px.data(), W, vr.left, vr.top, W, H);   // one continuous plane across every display
    } else {
        for (auto& d : disp) {
            int w = d.rc.right - d.rc.left, h = d.rc.bottom - d.rc.top;
            Image one;
            RenderDisplay(c, one, w, h);
            for (int y = 0; y < h; y++)
                memcpy(&out.px[(size_t)(d.rc.top - vr.top + y) * W + (d.rc.left - vr.left)], &one.px[(size_t)y * w], (size_t)w * 4);
        }
    }
    if (vrOut) *vrOut = vr;
    return true;
}
static bool writeBmp(const std::wstring& path, const Image& img) {
    int W = img.w, H = img.h;
    int rowBytes = (W * 3 + 3) & ~3;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih); ih.biWidth = W; ih.biHeight = H; ih.biPlanes = 1; ih.biBitCount = 24; ih.biCompression = BI_RGB;
    ih.biSizeImage = (DWORD)rowBytes * H; ih.biXPelsPerMeter = ih.biYPelsPerMeter = 3780;
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + ih.biSizeImage;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wr;
    bool ok = WriteFile(f, &fh, sizeof(fh), &wr, nullptr) && WriteFile(f, &ih, sizeof(ih), &wr, nullptr);
    std::vector<uint8_t> row(rowBytes, 0);
    for (int y = H - 1; ok && y >= 0; y--) {
        const uint32_t* s = &img.px[(size_t)y * W];
        for (int x = 0; x < W; x++) { row[x * 3] = s[x] & 255; row[x * 3 + 1] = (s[x] >> 8) & 255; row[x * 3 + 2] = (s[x] >> 16) & 255; }
        ok = WriteFile(f, row.data(), rowBytes, &wr, nullptr) && wr == (DWORD)rowBytes;
    }
    CloseHandle(f);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}
static const GUID kCLSID_WICImagingFactory = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
static const GUID kIID_IWICImagingFactory = {0xec5ec8a9, 0xc395, 0x4314, {0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70}};
static const GUID kContainerPng = {0x1b7cfaf4, 0x713f, 0x473c, {0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf}};
static const GUID kFormat24bppBGR = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0c}};
static bool writePng(const std::wstring& path, const Image& img, std::wstring* err) {
    HRESULT ci = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IWICImagingFactory* fac = nullptr;
    IWICStream* st = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* fr = nullptr;
    IPropertyBag2* bag = nullptr;
    HRESULT hr = CoCreateInstance(kCLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, kIID_IWICImagingFactory, (void**)&fac);
    if (SUCCEEDED(hr)) hr = fac->CreateStream(&st);
    if (SUCCEEDED(hr)) hr = st->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = fac->CreateEncoder(kContainerPng, nullptr, &enc);
    if (SUCCEEDED(hr)) hr = enc->Initialize(st, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&fr, &bag);
    if (SUCCEEDED(hr)) hr = fr->Initialize(bag);
    if (SUCCEEDED(hr)) hr = fr->SetSize(img.w, img.h);
    WICPixelFormatGUID fmt = kFormat24bppBGR;
    if (SUCCEEDED(hr)) hr = fr->SetPixelFormat(&fmt);
    if (SUCCEEDED(hr) && memcmp(&fmt, &kFormat24bppBGR, sizeof(GUID)) != 0) hr = E_FAIL;
    if (SUCCEEDED(hr)) {
        int stride = (img.w * 3 + 3) & ~3;
        int band = std::max(1, (int)(16 * 1024 * 1024 / std::max(1, stride)));
        std::vector<uint8_t> buf((size_t)stride * std::min(band, img.h));
        for (int y0 = 0; y0 < img.h && SUCCEEDED(hr); y0 += band) {
            int n = std::min(band, img.h - y0);
            for (int y = 0; y < n; y++) {
                const uint32_t* s = &img.px[(size_t)(y0 + y) * img.w];
                uint8_t* d = &buf[(size_t)y * stride];
                for (int x = 0; x < img.w; x++) { d[x * 3] = s[x] & 255; d[x * 3 + 1] = (s[x] >> 8) & 255; d[x * 3 + 2] = (s[x] >> 16) & 255; }
            }
            hr = fr->WritePixels(n, stride, (UINT)((size_t)stride * n), buf.data());
        }
    }
    if (SUCCEEDED(hr)) hr = fr->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();
    ok = SUCCEEDED(hr);
    if (bag) bag->Release();
    if (fr) fr->Release();
    if (enc) enc->Release();
    if (st) st->Release();
    if (fac) fac->Release();
    if (SUCCEEDED(ci)) CoUninitialize();
    if (!ok) {
        DeleteFileW(path.c_str());
        if (err) { wchar_t b[96]; swprintf(b, 96, L"PNG encoder failed (0x%08lX).", (unsigned long)hr); *err = b; }
    }
    return ok;
}
bool SaveImageFile(const Image& img, const std::wstring& path, std::wstring* err) {
    if (img.px.empty() || img.w <= 0 || img.h <= 0) { if (err) *err = L"Nothing to save."; return false; }
    std::wstring ext = path.size() >= 4 ? path.substr(path.size() - 4) : L"";
    for (auto& ch : ext) ch = towlower(ch);
    if (ext == L".png") return writePng(path, img, err);
    if (ext == L".bmp") {
        if (writeBmp(path, img)) return true;
        if (err) *err = L"Could not write the file.";
        return false;
    }
    if (err) *err = L"Choose a .png or .bmp file name.";
    return false;
}
bool ApplyWallpaper(const Config& c, std::wstring* info) {
    Image img;
    RECT vr;
    if (!ComposeDesktop(c, img, &vr)) { if (info) *info = L"No displays found."; return false; }
    auto disp = EnumDisplays();
    std::wstring dir = AppDataDir();
    wchar_t name[64];
    swprintf(name, 64, L"\\wallpaper_%08x.bmp", (unsigned)GetTickCount());
    std::wstring path = dir + name;
    if (!writeBmp(path, img)) { if (info) *info = L"Could not write " + path; return false; }
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        // Span (22) maps the bitmap 1:1 onto the virtual desktop; with one display Fill (10) is exact too.
        const wchar_t* style = disp.size() > 1 ? L"22" : L"10";
        RegSetValueExW(k, L"WallpaperStyle", 0, REG_SZ, (const BYTE*)style, (DWORD)(wcslen(style) + 1) * 2);
        RegSetValueExW(k, L"TileWallpaper", 0, REG_SZ, (const BYTE*)L"0", 4);
        DWORD q = 100;   // never let Windows re-encode the wallpaper lossily: keeps every tile pixel exact
        RegSetValueExW(k, L"JPEGImportQuality", 0, REG_DWORD, (const BYTE*)&q, sizeof(q));
        RegCloseKey(k);
    }
    BOOL ok = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)path.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\wallpaper_*.bmp").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring f = dir + L"\\" + fd.cFileName;
            if (_wcsicmp(f.c_str(), path.c_str()) != 0) DeleteFileW(f.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (info) {
        const Pattern& p = Patterns()[c.pattern];
        wchar_t b[220];
        swprintf(b, 220, L"%ls applied to %d display%ls (%dx%d)%ls", p.name.c_str(), (int)disp.size(), disp.size() > 1 ? L"s" : L"", img.w, img.h,
                 IsTiled(p.kind) && p.kind != K_SOLID ? L", tiles locked across displays" : L"");
        *info = b;
    }
    return ok != FALSE;
}
