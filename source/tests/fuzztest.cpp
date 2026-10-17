// Fuzz test: renders every pattern with random seeds, random parameter values (including the exact
// range ends), random tile sizes / shading / colour cycling / adjustments, at random output sizes
// (tiny, huge, extreme aspect) and random desktop offsets (including the negative coordinates of
// displays left of or above the primary). Any C++ exception is reported with the config that
// caused it; a hardware fault ends the run with crash.log naming the config (CrashContext).
//   fuzztest.exe [iterations-per-pattern] [seed]
#include "../src/core.h"
#include "../src/crash.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <exception>

static uint32_t rs = 1;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static float rf() { return (rnd() >> 8) / 16777216.0f; }
static float rparam(const ParamDef& d) {
    switch (rnd() % 6) {
    case 0: return d.min;
    case 1: return d.max;
    case 2: return d.def;
    default: return d.min + (d.max - d.min) * rf();
    }
}
static int rsize() {
    switch (rnd() % 8) {
    case 0: return 1 + rnd() % 4;
    case 1: return 5 + rnd() % 40;
    case 2: return 2000 + rnd() % 2000;
    default: return 40 + rnd() % 900;
    }
}

int main(int argc, char** argv) {
    int iters = argc > 1 ? atoi(argv[1]) : 30;
    rs = argc > 2 ? (uint32_t)strtoul(argv[2], nullptr, 10) : 20261015u;
    if (!rs) rs = 1;
    CrashInit(L"fuzz", false);
    EngineInit();
    const auto& P = Patterns();
    int fails = 0, runs = 0;
    int first = 0;
    if (argc > 3) {   // optional: start at this pattern (by name)
        wchar_t w[128];
        MultiByteToWideChar(CP_UTF8, 0, argv[3], -1, w, 128);
        first = std::max(0, FindPattern(w));
    }
    for (size_t pi = first; pi < P.size(); pi++) {
        if (P[pi].kind == K_HEADER) continue;
        for (int it = 0; it < iters; it++) {
            Config c = DefaultConfig((int)pi);
            const Pattern& p = P[pi];
            if (rnd() % 4) for (int i = 0; i < 4; i++) c.colors[i] = rnd() & 0xffffff;
            c.seed = (rnd() % 5 == 0) ? (rnd() % 4) : rnd();   // includes seed 0..3
            for (size_t i = 0; i < p.params.size() && i < (size_t)kMaxParams; i++) c.params[i] = rparam(p.params[i]);
            c.zoom = 1 + rnd() % 4;
            c.tileSize = (int[]){0, 64, 128, 192, 256, 384, 512}[rnd() % 7];
            c.shading = rnd() % 4;
            c.rampRepeat = 1 + rnd() % 8;
            c.rampOffset = rf();
            c.rampMirror = rnd() & 1;
            c.fit = rnd() % 4;
            if (rnd() % 2) {   // Transform tab
                c.rotate = rnd() % 4; c.mirrorX = rnd() & 1; c.mirrorY = rnd() & 1; c.offsetX = rf(); c.offsetY = rf();
                c.smoothing = rnd() % 3; c.balance = rf() * 200 - 100; c.invert = rnd() & 1;
            }
            if (rnd() % 2) {
                c.adj.brightness = rf() * 200 - 100; c.adj.contrast = rf() * 200 - 100; c.adj.saturation = rf() * 200 - 100;
                c.adj.hue = rf() * 360 - 180; c.adj.soften = rf() * 100; c.adj.calm = rf() * 100;
                c.adj.textTone = rnd() % 3; c.adj.textStrength = rf() * 100;
            }
            SanitizeConfig(c);
            int op = rnd() % 5, w = rsize(), h = rsize();
            int vx = (int)(rnd() % 20000) - 10000, vy = (int)(rnd() % 12000) - 6000;
            std::wstring key = ConfigKey(c);
            CrashContext(L"fuzz %ls op %d %dx%d at %d,%d: %ls", p.name.c_str(), op, w, h, vx, vy, key.c_str());
            try {
                if (op == 0 || op == 1) {
                    if (IsTiled(p.kind)) {
                        auto t = RenderTile(c);
                        if (!t || t->w <= 0 || t->h <= 0 || t->px.size() != (size_t)t->w * t->h) { printf("FAIL bad tile %ls\n", key.c_str()); fails++; }
                        Image im;
                        im.w = std::min(w, 1200); im.h = std::min(h, 800);
                        im.px.assign((size_t)im.w * im.h, 0);
                        if (op == 0) RenderRegionFrom(c, t.get(), im.px.data(), im.w, vx, vy, im.w, im.h);
                        else RenderRegion(c, im.px.data(), im.w, vx, vy, im.w, im.h);
                        AnalyzeReadability(im, c.adj.textTone == TT_DARK ? TT_DARK : TT_LIGHT);
                    } else {
                        Image im;
                        RenderDisplay(c, im, std::min(w, 1600), std::min(h, 1000));
                        if (im.px.size() != (size_t)im.w * im.h) { printf("FAIL bad display image %ls\n", key.c_str()); fails++; }
                    }
                } else if (op == 2) {
                    Image im;
                    RenderDisplay(c, im, std::min(w, 1600), std::min(h, 1000));
                } else if (op == 3) {
                    Image im;
                    RenderToSize(c, std::min(w, 1600), std::min(h, 1000), im);
                } else {
                    Config d = c;
                    if (rnd() % 8 == 0) MakeTextFriendly(d, d.adj.textTone == TT_DARK ? TT_DARK : TT_LIGHT);
                    PeekTile(d);
                    Image im;
                    RenderDisplay(d, im, 1 + rnd() % 3, 1 + rnd() % 3);   // degenerate sizes
                }
            } catch (...) {
                fails++;
                std::wstring s = CrashRecovered(L"fuzz");
                printf("FAIL exception %ls | %ls op %d %dx%d at %d,%d | %ls\n", s.c_str(), p.name.c_str(), op, w, h, vx, vy, key.c_str());
                fflush(stdout);
            }
            runs++;
        }
        printf("%-24ls ok\n", P[pi].name.c_str());
        fflush(stdout);
    }
    printf("RESULT: %d renders, %d failures\n", runs, fails);
    return fails ? 1 : 0;
}
