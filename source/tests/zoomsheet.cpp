// Pixel-size study: every tiled pattern (factory settings) cropped at 1x, 2x, 3x and 4x, at the
// screen's own pixel scale, to choose each pattern's default Pixel Size.
//   zoomsheet.exe <outdir> [w h]   -> <outdir>/NNN_zK.png + names.txt
#include "../src/core.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    EngineInit();
    if (argc < 2) return 1;
    int W = argc > 3 ? atoi(argv[2]) : 320, H = argc > 3 ? atoi(argv[3]) : 200;
    wchar_t dir[512];
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, dir, 512);
    const auto& P = Patterns();
    std::wstring names;
    int n = 0;
    for (size_t i = 0; i < P.size(); i++) {
        if (!IsTiled(P[i].kind) || P[i].kind == K_SOLID) continue;
        Config c = DefaultConfig((int)i);
        auto t = RenderTile(c);
        for (int z = 1; z <= 4; z++) {
            Config cz = c;
            cz.zoom = z;
            Image im;
            im.w = W; im.h = H;
            im.px.assign((size_t)W * H, 0);
            RenderRegionFrom(cz, t.get(), im.px.data(), W, 0, 0, W, H);
            wchar_t fn[600];
            swprintf(fn, 600, L"%ls\\%03d_z%d.png", dir, n, z);
            SaveImageFile(im, fn, nullptr);
        }
        wchar_t line[300];
        swprintf(line, 300, L"%03d|%ls|%d|%dx%d\n", n, P[i].name.c_str(), P[i].kind, t ? t->w : 0, t ? t->h : 0);
        names += line;
        n++;
    }
    std::wstring nf = std::wstring(dir) + L"\\names.txt";
    WriteTextFile(nf, names);
    printf("%d tiled patterns\n", n);
    return 0;
}
