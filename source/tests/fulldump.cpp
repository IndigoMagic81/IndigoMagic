// Dumps every IRIX full-screen backdrop at its native size and at 1920x1080 / 1920x1200 / 2560x1080 (Fit = Scale).
#include "../src/core.h"
#include "../src/irix_data.h"
#include "../src/crash.h"
#include <cstdio>
int wmain(int argc, wchar_t** argv) {
    CrashInit(L"fulldump", false);
    EngineInit();
    std::wstring dir = argc > 1 ? argv[1] : L".";
    const auto& P = Patterns();
    for (size_t i = 0; i < P.size(); i++) {
        if (P[i].kind != K_FULL) continue;
        const IrixEntry& e = kIrix[P[i].src];
        Config c = DefaultConfig((int)i);
        Image im;
        c.fit = FIT_STRETCH; RenderDisplay(c, im, e.w, e.h); SaveImageFile(im, dir + L"\\" + P[i].name + L"_native.png", nullptr);
        c.fit = FIT_SCALE;
        int sz[3][2] = {{1920, 1080}, {1920, 1200}, {2560, 1080}};
        for (auto& s : sz) { RenderDisplay(c, im, s[0], s[1]); wchar_t b[64]; swprintf(b, 64, L"_%dx%d.png", s[0], s[1]); SaveImageFile(im, dir + L"\\" + P[i].name + b, nullptr); }
        wprintf(L"%ls %dx%d\n", P[i].name.c_str(), e.w, e.h); fflush(stdout);
    }
    fprintf(stderr, "loop done\n");
    return 0;
}
