// Renders patterns (factory settings) at wallpaper-like size into <dir>/<n>.png, for review.
// usage: bigshots.exe <dir> [w h] [which: o=generated, i=IRIX, a=all, or a family / pattern name] [zoom] [smoothing]
#include "../src/core.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
int wmain(int argc, wchar_t** argv) {
    EngineInit();
    std::wstring dir = argc > 1 ? argv[1] : L".";
    int W = argc > 3 ? _wtoi(argv[2]) : 960, H = argc > 3 ? _wtoi(argv[3]) : 540;
    std::wstring which = argc > 4 ? argv[4] : L"o";
    int zoom = argc > 5 ? _wtoi(argv[5]) : 0, sm = argc > 6 ? _wtoi(argv[6]) : -1;
    const auto& P = Patterns();
    FILE* f = _wfopen((dir + L"\\names.txt").c_str(), L"w, ccs=UTF-8");
    int n = 0;
    LARGE_INTEGER fr; QueryPerformanceFrequency(&fr);
    for (size_t i = 0; i < P.size(); i++) {
        const Pattern& p = P[i];
        if (p.kind == K_HEADER) continue;
        bool gen = p.kind == K_DTILE || p.kind == K_DSCENE;
        const std::wstring& cat = P[p.category].name;
        if (which == L"o" && !gen) continue;
        if (which == L"i" && gen) continue;
        if (which != L"o" && which != L"i" && which != L"a" && _wcsicmp(which.c_str(), cat.c_str()) && _wcsicmp(which.c_str(), p.name.c_str())) continue;
        Config c = DefaultConfig((int)i);
        if (zoom) c.zoom = zoom;
        if (sm >= 0) c.smoothing = sm;
        Image im;
        LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
        RenderDisplay(c, im, W, H);
        QueryPerformanceCounter(&t1);
        wchar_t b[64]; swprintf(b, 64, L"\\%03d.png", n);
        SaveImageFile(im, dir + b, nullptr);
        fwprintf(f, L"%03d\t%ls\t%ls\t%.0f\n", n, p.name.c_str(), cat.c_str(), (t1.QuadPart - t0.QuadPart) * 1000.0 / fr.QuadPart);
        n++;
    }
    fclose(f);
    printf("%d rendered\n", n);
    return 0;
}
