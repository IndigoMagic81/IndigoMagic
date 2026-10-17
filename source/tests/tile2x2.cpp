// 2x2 tilings of named patterns (with a cross-hair marking the seams) for visual seam review
#include "../src/core.h"
#include <cstdio>
int main(int argc, char** argv) {
    EngineInit();
    std::vector<std::wstring> names;
    for (int i = 1; i < argc; i++) { std::string s(argv[i]); names.push_back(std::wstring(s.begin(), s.end())); }
    const int C = 300, COLS = 5;
    int rows = ((int)names.size() + COLS - 1) / COLS;
    Image sheet; sheet.w = C * COLS; sheet.h = C * rows; sheet.px.assign((size_t)sheet.w * sheet.h, 0x101010);
    for (size_t k = 0; k < names.size(); k++) {
        int id = FindPattern(names[k]); if (id < 0) { printf("missing %ls\n", names[k].c_str()); continue; }
        Config c = DefaultConfig(id);
        auto t = RenderTile(c);
        int tw = t->w, th = t->h;
        // show a window centred on the tile corner, at 1:1
        int ox = (k % COLS) * C, oy = (k / COLS) * C;
        for (int y = 0; y < C - 4; y++) for (int x = 0; x < C - 4; x++) {
            int sx = ((x - (C - 4) / 2) % tw + tw) % tw, sy = ((y - (C - 4) / 2) % th + th) % th;
            sheet.px[(size_t)(oy + y) * sheet.w + ox + x] = t->px[(size_t)sy * tw + sx];
        }
        // seam markers: small red ticks on the frame edges where the seams are
        for (int d = 0; d < 6; d++) { sheet.px[(size_t)(oy + d) * sheet.w + ox + (C - 4) / 2] = 0xff0000; sheet.px[(size_t)(oy + (C - 4) / 2) * sheet.w + ox + d] = 0xff0000; }
    }
    std::wstring e; SaveImageFile(sheet, L"tile2x2.png", &e);
}
