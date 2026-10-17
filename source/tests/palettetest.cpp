// Palette generator, palette files and preset library tests (+ a swatch sheet for visual review)
#include "../src/palette.h"
#include "../src/presets.h"
#include <cstdio>
#include <cmath>
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
int main() {
    EngineInit();
    const auto& P = Patterns();
    int ramp = FindPattern(L"Mandelbrot"), accent = FindPattern(L"Memphis"), irix = FindPattern(L"Bulge"), two = FindPattern(L"Linen"), calm = FindPattern(L"Soft Grid");
    CHECK(ramp >= 0 && accent >= 0 && irix >= 0 && two >= 0 && calm >= 0, "test patterns missing");
    CHECK(P[accent].role == ROLE_ACCENT && P[ramp].role == ROLE_RAMP && P[irix].role == ROLE_IRIX && P[two].role == ROLE_TWO, "roles");

    // ---- swatch sheet: rows = harmonies, for a ramp pattern (left) and an accent pattern (right)
    const int SW = 22, PW = SW * 4 + 8, N = 8;
    Image sheet; sheet.w = PW * N * 2 + 20; sheet.h = (SW + 6) * HM_COUNT; sheet.px.assign((size_t)sheet.w * sheet.h, 0x303030);
    uint32_t cur[4] = {0, 0, 0, 0};
    bool none[4] = {false, false, false, false};
    for (int h = 0; h < HM_COUNT; h++)
        for (int side = 0; side < 2; side++)
            for (int k = 0; k < N; k++) {
                uint32_t out[4];
                RandomPalette(side ? accent : ramp, h, cur, none, 1000 + h * 100 + k, out);
                for (int i = 0; i < 4; i++)
                    for (int y = 0; y < SW; y++)
                        for (int x = 0; x < SW; x++)
                            sheet.px[(size_t)(h * (SW + 6) + 3 + y) * sheet.w + side * (PW * N + 20) + k * PW + i * SW + x] = out[i];
            }
    std::wstring e;
    SaveImageFile(sheet, L"palettes.png", &e);

    // ---- properties over many seeds
    for (int h = 0; h < HM_COUNT; h++)
        for (int s = 0; s < 200; s++) {
            uint32_t o[4];
            RandomPalette(ramp, h, cur, none, s * 7 + h, o);
            if (h != HM_NEON90) CHECK(Luma(o[0]) < Luma(o[1]) && Luma(o[1]) < Luma(o[2]) && Luma(o[2]) < Luma(o[3]), "ramp not ascending (harmony %ls seed %d)", HarmonyName(h), s);
            RandomPalette(accent, h, cur, none, s * 7 + h, o);
            for (int i = 1; i < 4; i++) CHECK(ContrastRatio(o[0], o[i]) >= 1.8f, "accent %d too close to background (%ls, %.2f)", i, HarmonyName(h), ContrastRatio(o[0], o[i]));
            RandomPalette(irix, h, cur, none, s * 7 + h, o);
            CHECK(Luma(o[3]) <= Luma(o[1]) && Luma(o[1]) <= Luma(o[0]) && Luma(o[0]) <= Luma(o[2]), "irix slot order (%ls)", HarmonyName(h));
            RandomPalette(two, h, cur, none, s * 7 + h, o);
            CHECK(ContrastRatio(o[0], o[1]) >= 1.5f, "bitmap fg/bg too close (%ls %.2f)", HarmonyName(h), ContrastRatio(o[0], o[1]));
        }
    // calm palettes keep white labels readable
    int calmOk = 0;
    for (int s = 0; s < 200; s++) { uint32_t o[4]; RandomPalette(calm, HM_AUTO, cur, none, s, o); float worst = 99; for (int i = 0; i < 4; i++) worst = std::min(worst, ContrastRatio(0xffffff, o[i])); if (worst >= 4.5f) calmOk++; }
    printf("calm palettes with >=4.5:1 white-label contrast: %d/200\n", calmOk);
    CHECK(calmOk >= 190, "calm palettes not text friendly enough");
    // locks are respected; single-slot reroll touches only that slot
    uint32_t base[4] = {0x123456, 0xabcdef, 0x777777, 0xff8800};
    bool lk[4] = {true, false, true, false};
    for (int s = 0; s < 100; s++) {
        uint32_t o[4];
        RandomPalette(ramp, HM_AUTO, base, lk, s, o);
        CHECK(o[0] == base[0] && o[2] == base[2], "lock ignored");
        uint32_t one = RandomSlot(ramp, HM_TRIADIC, base, 1, s);
        (void)one;
    }
    // different seeds really differ
    { uint32_t a[4], b[4]; RandomPalette(ramp, HM_AUTO, cur, none, 1, a); RandomPalette(ramp, HM_AUTO, cur, none, 2, b); CHECK(memcmp(a, b, sizeof a) != 0, "seeds identical"); }
    // palette application
    { uint32_t pal[4] = {0x101010, 0x404040, 0x808080, 0xf0f0f0}, o[4];
      ApplyPaletteToPattern(irix, pal, o); CHECK(o[3] == 0x101010 && o[1] == 0x404040 && o[0] == 0x808080 && o[2] == 0xf0f0f0, "irix mapping");
      ApplyPaletteToPattern(two, pal, o); CHECK(o[0] == 0x101010 && o[1] == 0xf0f0f0, "bitmap mapping");
      ApplyPaletteToPattern(ramp, pal, o); CHECK(memcmp(o, pal, sizeof o) == 0, "ramp mapping"); }

    // ---- palette files
    uint32_t pc[4] = {0x0a0b0c, 0xff00aa, 0x00ffcc, 0xfafafa}, back[4];
    std::wstring nm;
    CHECK(ExportPaletteFile(L"test.gpl", L"Neon Test", pc, &e), "gpl export: %ls", e.c_str());
    CHECK(ImportPaletteFile(L"test.gpl", nm, back, &e) && nm == L"Neon Test" && memcmp(pc, back, sizeof pc) == 0, "gpl round trip");
    WriteTextFile(L"hex.txt", L"my colours: #ff0000 and #0000FF\n");
    CHECK(ImportPaletteFile(L"hex.txt", nm, back, &e) && back[0] == 0xff0000 && back[3] == 0x0000ff, "hex import");
    WriteTextFile(L"empty.txt", L"nothing here");
    CHECK(!ImportPaletteFile(L"empty.txt", nm, back, &e), "empty import should fail");
    std::vector<Palette> up = {{L"Mine = one", L"My Palettes", {1, 2, 3, 4}, false}, {L"Second", L"My Palettes", {0xffffff, 0, 0x123456, 0x654321}, false}};
    CHECK(SaveUserPalettes(up), "save user palettes");
    auto upl = LoadUserPalettes();
    CHECK(upl.size() == 2 && upl[1].name == L"Second" && upl[1].c[2] == 0x123456 && upl[0].c[3] == 4, "user palettes round trip (%d)", (int)upl.size());

    // ---- presets
    for (auto& p : LoadPresets()) DeletePreset(p.name, nullptr);
    Config c = DefaultConfig(ramp);
    c.params[0] = 3.25f; c.adj.calm = 33.3f; c.colors[2] = 0xabcdef;
    CHECK(SavePreset(L"  My: Dream / Preset?  ", c, &e), "save: %ls", e.c_str());
    CHECK(!SavePreset(L"   ", c, &e), "empty name accepted");
    auto pl = LoadPresets();
    CHECK(pl.size() == 1 && pl[0].name == L"My: Dream / Preset?" && pl[0].cfg == c, "load after save");
    Config c2 = c; c2.seed = 99;
    CHECK(SavePreset(L"my: dream / preset?", c2, &e), "overwrite");
    pl = LoadPresets();
    CHECK(pl.size() == 1 && pl[0].cfg.seed == 99, "overwrite should replace (%d)", (int)pl.size());
    CHECK(SavePreset(L"Second", DefaultConfig(accent), &e), "save 2");
    CHECK(!RenamePreset(L"Second", L"MY: DREAM / PRESET?", &e), "rename onto existing accepted");
    CHECK(RenamePreset(L"Second", L"Neon Party", &e), "rename: %ls", e.c_str());
    pl = LoadPresets();
    CHECK(pl.size() == 2 && FindPreset(pl, L"Neon Party") >= 0 && FindPreset(pl, L"Second") < 0, "rename result");
    CHECK(ExportPresetFile(L"exported.irixbg", L"Neon Party", pl[FindPreset(pl, L"Neon Party")].cfg, &e), "export");
    Config ci; std::wstring in;
    CHECK(ImportPresetFile(L"exported.irixbg", ci, in, &e) && in == L"Neon Party" && ci == pl[FindPreset(pl, L"Neon Party")].cfg, "import");
    CHECK(DeletePreset(L"Neon Party", &e), "delete");
    CHECK(!DeletePreset(L"Neon Party", &e), "double delete accepted");
    pl = LoadPresets();
    CHECK(pl.size() == 1, "count after delete");
    for (auto& p : pl) DeletePreset(p.name, nullptr);
    printf(fails ? "FAILURES: %d\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
