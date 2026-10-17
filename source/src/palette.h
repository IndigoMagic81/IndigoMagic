// Palettes, the random colour generator and palette files
#pragma once
#include "core.h"

struct Palette {
    std::wstring name, group;
    uint32_t c[4] = {0, 0, 0, 0};
    bool builtin = true;
};
const std::vector<Palette>& BuiltinPalettes();

enum Harmony {
    HM_AUTO = 0, HM_ANALOGOUS, HM_COMPLEMENTARY, HM_TRIADIC, HM_SPLIT, HM_TETRADIC, HM_MONO,
    HM_NEON90, HM_SGI, HM_PSYCHE, HM_PASTEL, HM_MIDNIGHT, HM_EARTH, HM_CALM, HM_COUNT
};
const wchar_t* HarmonyName(int h);
const wchar_t* HarmonyHelp(int h);

// Random palette suited to the pattern's colour roles. Locked slots keep cur[] and steer the hue.
void RandomPalette(int pattern, int harmony, const uint32_t cur[4], const bool lock[4], uint32_t seed, uint32_t out[4]);
// Re-roll one slot so it still fits the other three.
uint32_t RandomSlot(int pattern, int harmony, const uint32_t cur[4], int slot, uint32_t seed);
// Map a palette onto a pattern's slots (role-aware: IRIX tiles get luminance order, bitmaps get max contrast).
void ApplyPaletteToPattern(int pattern, const uint32_t pal[4], uint32_t out[4]);

// User palettes (palettes.txt in the app data folder)
std::vector<Palette> LoadUserPalettes();
bool SaveUserPalettes(const std::vector<Palette>& v);

// GIMP .gpl (or any text file with #RRGGBB codes) import, .gpl export
bool ExportPaletteFile(const std::wstring& path, const std::wstring& name, const uint32_t c[4], std::wstring* err);
bool ImportPaletteFile(const std::wstring& path, std::wstring& name, uint32_t c[4], std::wstring* err);

// colour utilities
float Luma(uint32_t c);                       // 0..1 (perceptual lightness, OKLab L)
float ContrastRatio(uint32_t a, uint32_t b);  // WCAG
