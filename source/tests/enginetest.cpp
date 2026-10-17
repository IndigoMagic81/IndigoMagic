// Engine test: renders every pattern through the real pipeline (contact sheets), checks preset
// text round-trips exactly, adjustments/readability behave, PNG/BMP export works, and times 4K scenes.
#include "../src/core.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static double now() { LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f); return t.QuadPart * 1000.0 / f.QuadPart; }
static uint64_t hashImg(const Image& im) { uint64_t h = 1469598103934665603ULL; for (uint32_t v : im.px) { h ^= v; h *= 1099511628211ULL; } return h ^ im.w * 31 ^ im.h; }
int main(int argc, char** argv) {
    EngineInit();
    const auto& P = Patterns();
    int npat = 0, ndream = 0;
    for (auto& p : P) { if (p.kind != K_HEADER) npat++; if (p.kind == K_DTILE || p.kind == K_DSCENE) ndream++; }
    printf("patterns: %d selectable, %d originals\n", npat, ndream);
    CHECK(ndream == 138, "expected 138 generated patterns, got %d", ndream);

    // ---- contact sheets (cell 220x124), every pattern through RenderDisplay with defaults
    const int CW = 220, CH = 124, COLS = 8;
    std::vector<int> ids; for (size_t i = 0; i < P.size(); i++) if (P[i].kind != K_HEADER) ids.push_back((int)i);
    int rows = ((int)ids.size() + COLS - 1) / COLS;
    Image sheet; sheet.w = CW * COLS; sheet.h = CH * rows; sheet.px.assign((size_t)sheet.w * sheet.h, 0x202020);
    for (size_t k = 0; k < ids.size(); k++) {
        Config c = DefaultConfig(ids[k]);
        c.zoom = 1;   // the sheet (and its flat-output check) looks at the pattern itself, at 1x
        Image im; double t0 = now(); RenderDisplay(c, im, CW - 4, CH - 4); double t1 = now();
        CHECK(im.w == CW - 4 && im.h == CH - 4 && im.px.size() == (size_t)im.w * im.h, "size %ls", P[ids[k]].name.c_str());
        int cx = (k % COLS) * CW, cy = (k / COLS) * CH;
        for (int y = 0; y < im.h; y++) for (int x = 0; x < im.w; x++) sheet.px[(size_t)(cy + y) * sheet.w + cx + x] = im.px[(size_t)y * im.w + x];
        // flat output check (catches broken generators)
        uint32_t mn = 0xffffffff, mx = 0; for (uint32_t v : im.px) { uint32_t l = ((v >> 16) & 255) + ((v >> 8) & 255) + (v & 255); mn = std::min(mn, l); mx = std::max(mx, l); }
        if (P[ids[k]].kind != K_SOLID) CHECK(mx > mn, "flat output: %ls", P[ids[k]].name.c_str());
        if (t1 - t0 > 400) printf("  slow preview: %-22ls %.0f ms\n", P[ids[k]].name.c_str(), t1 - t0);
    }
    std::wstring err;
    CHECK(SaveImageFile(sheet, L"sheet_all.png", &err), "png save: %ls", err.c_str());
    CHECK(SaveImageFile(sheet, L"sheet_all.bmp", &err), "bmp save: %ls", err.c_str());

    // ---- serialisation round trip on randomised configs: text -> config identical, render identical
    uint32_t rs = 12345;
    auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; };
    auto rf = [&]() { return (rnd() >> 8) / 16777216.0f; };
    int rt = 0;
    for (int it = 0; it < 400; it++) {
        Config c = DefaultConfig(ids[rnd() % ids.size()]);
        for (int i = 0; i < 4; i++) c.colors[i] = rnd() & 0xffffff;
        c.seed = rnd();
        const Pattern& p = P[c.pattern];
        for (size_t i = 0; i < p.params.size(); i++) c.params[i] = p.params[i].min + (p.params[i].max - p.params[i].min) * rf();
        c.zoom = 1 + rnd() % 4; c.tileSize = (int[]){0, 128, 256, 384, 512}[rnd() % 5]; c.shading = rnd() % 4; c.rampRepeat = 1 + rnd() % 8;
        c.rampOffset = rf(); c.rampMirror = rnd() & 1; c.fit = rnd() % 4;
        c.adj.brightness = rf() * 200 - 100; c.adj.contrast = rf() * 200 - 100; c.adj.saturation = rf() * 200 - 100; c.adj.hue = rf() * 360 - 180;
        c.adj.soften = rf() * 100; c.adj.calm = rf() * 100; c.adj.textTone = rnd() % 3; c.adj.textStrength = rf() * 100;
        SanitizeConfig(c);
        std::wstring name = L"Test preset " + std::to_wstring(it);
        std::wstring txt = ConfigToText(c, name);
        Config d; std::wstring nm, e;
        bool ok = ConfigFromText(txt, d, &nm, &e);
        CHECK(ok, "parse failed: %ls", e.c_str());
        CHECK(d == c, "round trip mismatch for %ls", p.name.c_str());
        CHECK(nm == name, "name mismatch");
        if (it < 40) {   // pixel-identical renders
            Image a, b; RenderDisplay(c, a, 96, 64); RenderDisplay(d, b, 96, 64);
            CHECK(hashImg(a) == hashImg(b), "render mismatch %ls", p.name.c_str());
        }
        rt++;
    }
    printf("round trips: %d\n", rt);
    // malformed input must fail cleanly
    Config z; std::wstring e;
    CHECK(!ConfigFromText(L"hello", z, nullptr, &e), "accepted garbage");
    CHECK(!ConfigFromText(L"[IrixBackgroundPreset]\nPattern=Nope\n", z, nullptr, &e), "accepted unknown pattern");
    CHECK(ConfigFromText(L"[IrixBackgroundPreset]\nPattern=Bulge\nParams=nan,inf,-9e9,9e9\nPixelSize=99\n", z, nullptr, &e) && z.zoom == 4, "sanitize");

    // ---- determinism: same config twice -> same pixels (incl. threaded paths)
    for (int k = 0; k < 20; k++) {
        Config c = DefaultConfig(ids[(k * 7919) % ids.size()]);
        Image a, b; RenderDisplay(c, a, 300, 170);
        Config c2 = c; c2.adj.brightness = 0.5f; RenderDisplay(c2, b, 300, 170);   // flush cache key
        RenderDisplay(c, b, 300, 170);
        CHECK(hashImg(a) == hashImg(b), "nondeterministic %ls", P[c.pattern].name.c_str());
    }

    // ---- readability: Make Text Friendly must reach "Good" (or better) and never make things worse
    int good = 0, tested = 0;
    double tsum = 0;
    for (int id : ids) {
        if (P[id].kind == K_SOLID) continue;
        Config c = DefaultConfig(id);
        Image a; RenderDisplay(c, a, 320, 180);
        Readability r0 = AnalyzeReadability(a, TT_LIGHT);
        double t0 = now();
        int sc = MakeTextFriendly(c, TT_LIGHT);
        tsum += now() - t0;
        Image b; RenderDisplay(c, b, 320, 180);
        Readability r1 = AnalyzeReadability(b, TT_LIGHT);
        tested++;
        CHECK(r1.score == sc, "score mismatch %ls", P[id].name.c_str());
        CHECK(r1.score >= r0.score, "text friendly made %ls worse (%d -> %d)", P[id].name.c_str(), r0.score, r1.score);
        if (r1.score >= 2) good++;
        else printf("  not good after Make Text Friendly: %-22ls ratio %.2f busy %.2f (calm %.0f str %.0f)\n", P[id].name.c_str(), r1.ratio, r1.busy, c.adj.calm, c.adj.textStrength);
        // dark labels too
        Config d = DefaultConfig(id);
        int sd = MakeTextFriendly(d, TT_DARK);
        if (sd < 2) printf("  dark labels not good: %-22ls\n", P[id].name.c_str());
    }
    printf("text friendly reached Good on %d/%d (avg %.0f ms per pattern)\n", good, tested, tsum / tested);
    CHECK(good == tested, "text friendly shortfalls");

    // ---- 4K timing for display kinds + big tiles
    if (argc > 1) {
        for (int id : ids) {
            if (!IsDisplayKind(P[id].kind)) continue;
            Config c = DefaultConfig(id);
            Image im; double t0 = now(); RenderDisplay(c, im, 3840, 2160); double t1 = now();
            printf("  4K %-22ls %7.0f ms\n", P[id].name.c_str(), t1 - t0);
        }
        for (int id : ids) {
            if (P[id].kind != K_DTILE || !P[id].usesTile) continue;
            Config c = DefaultConfig(id); c.tileSize = 512;
            double t0 = now(); auto t = RenderTile(c); double t1 = now();
            if (t1 - t0 > 300) printf("  512 tile %-22ls %7.0f ms\n", P[id].name.c_str(), t1 - t0);
        }
    }
    printf(fails ? "FAILURES: %d\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
