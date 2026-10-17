// Indigo Magic - engine interface
// Core types: pattern registry, configuration, rendering pipeline, wallpaper + export.
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------
// Pattern registry
// ---------------------------------------------------------------------------------------------
const int kMaxParams = 6;      // a pattern's own controls
enum PatKind {
    K_SOLID = 0,   // one colour
    K_TILE4 = 1,   // IRIX 4-colour pixmap tile
    K_TILE2 = 2,   // IRIX 2-colour bitmap tile
    K_FULL = 3,    // IRIX full-screen backdrop
    K_GEN = 4,     // (retired in 2.4: the v1 generated tiles are now K_DTILE generators)
    K_GRAD = 5,    // gradient, composed per display
    K_HEADER = 6,  // list heading, not selectable
    K_DTILE = 7,   // original generator, seamless tile
    K_DSCENE = 8   // original generator, full-screen scene composed per display
};
enum FitMode { FIT_CENTER = 0, FIT_SCALE = 1, FIT_STRETCH = 2, FIT_TILE = 3 };
enum OutKind { OUT_FIELD = 0, OUT_INDEX = 1, OUT_RGB = 2 };
// How the four colour slots are used - drives labels, palette application and the randomiser
enum Role { ROLE_SOLID = 0, ROLE_IRIX = 1, ROLE_TWO = 2, ROLE_RAMP = 3, ROLE_ACCENT = 4, ROLE_SCENE = 5 };
enum Shading { SH_SMOOTH = 0, SH_BANDS = 1, SH_ORDERED = 2, SH_DIFFUSE = 3 };
enum TextTone { TT_OFF = 0, TT_LIGHT = 1, TT_DARK = 2 };

struct ParamDef {
    std::wstring name;
    float min = 0, max = 1, def = 0;
    int decimals = 0;                     // display precision for sliders
    std::vector<std::wstring> choices;    // non-empty: option menu, value = choice index
};

struct Pattern {
    std::wstring name, blurb;
    int kind = K_SOLID;
    int ncolors = 4;
    uint32_t def[4] = {0, 0, 0, 0};
    int src = 0;               // IRIX data index / classic generator id / gradient id / kGens2 index
    int category = 0;          // index of the heading this pattern sits under
    int out = OUT_INDEX;
    int role = ROLE_IRIX;
    int defShading = SH_SMOOTH;
    int defTile = 256;         // default tile size (tiles)
    bool usesTile = false;     // tile size is meaningful
    bool usesSeed = false;     // "New Variation" changes it
    bool textFriendly = false;
    int defZoom = 1;           // best-looking Pixel Size on a 100% (96 dpi) screen
    bool pixelArt = false;     // made of hard pixels: Pixel Size enlarges its pixels (IRIX art); otherwise Scale redraws it larger
    int genFlags = 0;          // GenFlags of a generated pattern
    uint8_t stopOrder[4] = {0, 1, 2, 3};   // colour slot used for each ramp stop, darkest first
    std::vector<ParamDef> params;
    std::wstring slotNames[4];
};

const std::vector<Pattern>& Patterns();
int FindPattern(const std::wstring& name);
int FindPatternOrSuccessor(const std::wstring& name);   // also maps patterns retired in 2.3 to their closest relative
bool IsRetiredPattern(const std::wstring& name);
bool IsTiled(int kind);        // tiled across the desktop, phase-locked to the primary display origin
bool IsDisplayKind(int kind);  // composed once per display
bool HasShading(int pat);      // field output / gradient: shading + colour cycling apply
bool HasVariations(int pat);
bool IsGenerated(int pat);     // drawn by a generator at any resolution (Scale, Smoothing, Balance apply)
std::wstring SlotName(int pat, int slot);

// ---------------------------------------------------------------------------------------------
// Configuration: everything needed to reproduce a background exactly
// ---------------------------------------------------------------------------------------------
struct Adjust {
    float brightness = 0, contrast = 0, saturation = 0, hue = 0;   // -100..100 (hue -180..180)
    float soften = 0, calm = 0;                                    // 0..100
    int textTone = TT_OFF;
    float textStrength = 60;                                        // 0..100
    bool operator==(const Adjust& o) const;
};
enum Smoothing { SM_OFF = 0, SM_NORMAL = 1, SM_HIGH = 2 };
struct Config {
    int pattern = 0;
    uint32_t colors[4] = {0, 0, 0, 0};
    uint32_t seed = 1;
    float params[kMaxParams] = {0, 0, 0, 0, 0, 0};
    int fit = FIT_TILE;
    int zoom = 1;          // tiled kinds: Pixel Size (IRIX art) or Scale (generated tiles, redrawn larger)
    int tileSize = 0;      // generated tiles: 0 = pattern default
    int shading = SH_SMOOTH;
    int rampRepeat = 1;    // colour cycling: ramp repeats 1..8
    float rampOffset = 0;  // 0..1
    int rampMirror = 0;
    // Transform tab
    int rotate = 0;        // quarter turns clockwise 0..3
    int mirrorX = 0, mirrorY = 0;
    float offsetX = 0, offsetY = 0;   // tiles: shift 0..1 of a tile
    int smoothing = SM_NORMAL;        // generated patterns: antialiasing quality
    float balance = 0;     // -100..100: shifts a gradient-style pattern towards its dark / light colours
    int invert = 0;        // gradient-style patterns: run the colours the other way
    Adjust adj;
    bool operator==(const Config& o) const;
    bool operator!=(const Config& o) const { return !(*this == o); }
};
Config DefaultConfig(int pat);
int DefaultZoom(int pat);     // this pattern's default Pixel Size on this computer's primary display
double DisplayScale();        // primary display scaling (1.0 = 100%)
void SanitizeConfig(Config& c);
int EffectiveTileSize(const Config& c);
std::wstring ConfigKey(const Config& c);   // identifies the rendered pixels (excludes fit/zoom)

// Serialisation (exact round trip: colours as hex, floats with 9 significant digits)
std::wstring ConfigToText(const Config& c, const std::wstring& name);      // a whole preset file
bool ConfigFromText(const std::wstring& text, Config& c, std::wstring* name, std::wstring* err);
std::wstring ConfigKeysText(const Config& c);                              // just the key=value lines
struct IniSection { std::wstring name; std::map<std::wstring, std::wstring> kv; };   // keys lower-cased
std::vector<IniSection> ParseIni(const std::wstring& text);
bool ConfigFromKeys(const std::map<std::wstring, std::wstring>& kv, Config& c, std::wstring* err);
bool ReadTextFile(const std::wstring& path, std::wstring& out);           // UTF-8 (BOM optional) or UTF-16LE
bool WriteTextFile(const std::wstring& path, const std::wstring& text);   // UTF-8, replaced atomically

// ---------------------------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------------------------
struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;   // 0x00RRGGBB
};
// The final RGB tile for tiled kinds (after colour mapping, adjustments and transforms). Cached,
// thread-safe. Generated tiles come back already drawn at their Scale (TileZoom() == 1); IRIX
// art comes back at 1x and is enlarged by TileZoom() when it is laid out. draft: thumbnails -
// no supersampling.
std::shared_ptr<const Image> RenderTile(const Config& c, bool draft = false);
int TileZoom(const Config& c);
void EngineTrimCaches();   // drop cached fields and tiles (the display watcher, between re-applies)
// Cached tile only (never renders) - lets the UI thread draw without blocking.
std::shared_ptr<const Image> PeekTile(const Config& c, bool draft = false);
// Tiled kinds: fill a region of the virtual desktop starting at (vx,vy).
void RenderRegion(const Config& c, uint32_t* dst, int stride, int vx, int vy, int w, int h);
void RenderRegionFrom(const Config& c, const Image* tile, uint32_t* dst, int stride, int vx, int vy, int w, int h);
// Display kinds (and tiled kinds anchored at 0,0): a whole w x h display.
void RenderDisplay(const Config& c, Image& out, int w, int h);

struct Readability {
    float ratio = 0;           // WCAG contrast ratio for the label colour against the worst 10% of the background
    float busy = 0;            // 0..1 local detail
    int score = 0;             // 0 poor, 1 fair, 2 good, 3 excellent
    bool lightLabels = true;
};
Readability AnalyzeReadability(const Image& img, int tone);
const wchar_t* ReadabilityWord(int score);
// Adjusts calm / label tone / strength (gentlest first) until labels read "Good"; returns the score reached.
int MakeTextFriendly(Config& c, int tone);

struct DisplayInfo { RECT rc; bool primary; };
std::vector<DisplayInfo> EnumDisplays();
bool ComposeDesktop(const Config& c, Image& out, RECT* vr);
bool ApplyWallpaper(const Config& c, std::wstring* info);
bool SaveImageFile(const Image& img, const std::wstring& path, std::wstring* err);   // .png or .bmp
bool RenderToSize(const Config& c, int w, int h, Image& out);                         // export helper

std::wstring AppDataDir();      // %APPDATA%\IrixBackground (created)
void ParallelRows(int h, void (*fn)(int y, void* ctx), void* ctx);   // splits rows over all cores
template <class F> inline void ParallelFor(int h, F f) {
    ParallelRows(h, [](int y, void* c) { (*(F*)c)(y); }, &f);
}
void EngineInit();              // builds the registry; call once before using threads
