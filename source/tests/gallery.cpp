// Renders the 100 original patterns (factory settings) into one image + a names file.
#include "../src/core.h"
#include <cstdio>
int main() {
    EngineInit();
    const auto& P = Patterns();
    std::vector<int> ids;
    for (size_t i = 0; i < P.size(); i++) if (P[i].kind == K_DTILE || P[i].kind == K_DSCENE) ids.push_back((int)i);
    const int CW = 240, CH = 135, COLS = 10;
    int rows = ((int)ids.size() + COLS - 1) / COLS;
    Image sheet; sheet.w = CW * COLS; sheet.h = CH * rows; sheet.px.assign((size_t)sheet.w * sheet.h, 0x101010);
    FILE* f = _wfopen(L"gallery_names.txt", L"w, ccs=UTF-8");
    for (size_t k = 0; k < ids.size(); k++) {
        Config c = DefaultConfig(ids[k]);
        Image im;
        RenderDisplay(c, im, CW - 4, CH - 4);
        int cx = (k % COLS) * CW + 2, cy = (k / COLS) * CH + 2;
        for (int y = 0; y < im.h; y++) for (int x = 0; x < im.w; x++) sheet.px[(size_t)(cy + y) * sheet.w + cx + x] = im.px[(size_t)y * im.w + x];
        fwprintf(f, L"%ls\t%ls\n", P[ids[k]].name.c_str(), P[P[ids[k]].category].name.c_str());
    }
    fclose(f);
    std::wstring err;
    SaveImageFile(sheet, L"gallery.png", &err);
    printf("%d patterns\n", (int)ids.size());
}
