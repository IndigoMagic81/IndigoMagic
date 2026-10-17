// The generated patterns (gens.cpp): every background that is not an IRIX original.
//
// Resolution independence: a generator draws a "design" image of dw x dh design pixels (the
// size the pattern was designed at - a 256 px tile, or the display) at px output pixels per
// design pixel. The engine renders at px > 1 to supersample (antialiasing) and to draw a tile
// natively at 2x / 3x / 4x Scale instead of blowing up its pixels. Generators therefore place
// everything in real output pixels (F.w, F.h) and multiply any fixed pixel size by c.px.
#pragma once
#include "core.h"
#include <vector>

struct GenCtx {
    int w, h;          // output size in pixels (tile: w==h)
    bool tile;         // true: must wrap seamlessly
    uint32_t seed;
    float p[kMaxParams];   // parameter values
    float aspect;      // w/h (1 for tiles)
    float px = 1;      // output pixels per design pixel (supersampling x Scale)
    int dw = 0, dh = 0;    // design size (w/px, h/px) - for pixel-grid algorithms
    bool discrete = false; // GF_DISCRETE: shapes are flat slots; drawing helpers use hard coverage (supersampling smooths them)
};
struct GenOut {
    std::vector<float> f;      // w*h values
    bool normalize = true;     // stretch by percentiles to [0,1]
    float lo = 0.01f, hi = 0.99f;
    float gamma = 1.0f;
};
typedef void (*GenFn2)(GenOut& o, const GenCtx& c);

// The families shown in the list (after IRIX Classics and IRIX Extras), in list order
enum Family {
    FAM_GEOMETRIC, FAM_WIREFRAME, FAM_FRACTALS, FAM_RETRO, FAM_DECORATIVE, FAM_TOPO, FAM_ABSTRACT, FAM_PROCEDURAL,
    FAM_DEMOSCENE, FAM_NEON, FAM_TEXT, FAM_COUNT
};
extern const wchar_t* const kFamilyNames[FAM_COUNT];

struct ParamDef2 { const wchar_t* name; float min, max, def; int decimals; const wchar_t* choices; };   // choices: "A|B|C" option menu

enum GenFlags {
    GF_DISCRETE = 1,     // values are exactly k/3: flat colour slots (shapes) - never ramp-blended or dithered
    GF_EDGES = 2,        // hard edges / thin lines / fine detail: supersampled for antialiasing
    GF_TWO = 4,          // two colours (background / foreground), values 0 or 1/3
    GF_IRIXSLOTS = 8,    // IRIX slot convention (slot 3 darkest, 1, 0, 2 lightest) for the ramp stops
    GF_DIFFUSE = 16,     // default shading: IRIX error diffusion
    GF_ORDERED = 32,     // default shading: ordered dither
    GF_BANDS = 64,       // default shading: banded
    GF_PIXEL = 128       // drawn on a design-pixel grid (hard square pixels by intent)
};

struct GenDef2 {
    const wchar_t* name;
    int family;                // Family
    bool full;                 // per-display scene instead of a seamless tile
    int defTile;               // default tile size (tiles)
    uint32_t def[4];
    const wchar_t* blurb;
    int nparams;
    ParamDef2 params[kMaxParams];
    bool textFriendly;
    bool variations;
    GenFn2 fn;
    int flags;
};
extern const GenDef2 kGens2[];
extern const int kGens2Count;
inline bool GenIsDiscrete(int i) { return i >= 0 && i < kGens2Count && (kGens2[i].flags & GF_DISCRETE); }
