// Application state: the shared App object, undo/redo, persistence, applied wallpaper, watcher
#include "app.h"
#include "crash.h"
#include <shellapi.h>
#include <algorithm>

App g;
bool g_selftest = false;

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValue = L"IrixBackground";
static const wchar_t* kWatchClass = L"IrixBackgroundWatcher";

Config& Cur() { return g.cfg[g.sel]; }

void Edit() {
    EditState s{g.sel, Cur()};
    if (!g.undo.empty() && g.undo.back() == s) return;
    g.undo.push_back(s);
    while (g.undo.size() > 200) g.undo.pop_front();
    g.redo.clear();
}
static void restore(const EditState& s) {
    if (s.sel < 0 || s.sel >= (int)g.cfg.size() || Patterns()[s.sel].kind == K_HEADER) return;
    if (s.sel != g.sel) {
        CloseColorBrowser(false);              // it edits a colour of the pattern being left
        for (bool& l : g.lock) l = false;      // locks belong to that pattern's colours too
    }
    g.sel = s.sel;
    g.cfg[s.sel] = s.cfg;
}
bool Undo() {
    // skip snapshots identical to the current state (e.g. an edit that changed nothing)
    while (!g.undo.empty() && g.undo.back() == EditState{g.sel, Cur()}) g.undo.pop_back();
    if (g.undo.empty()) return false;
    g.redo.push_back({g.sel, Cur()});
    restore(g.undo.back());
    g.undo.pop_back();
    Changed();
    ColorBrowserSync();
    return true;
}
bool Redo() {
    if (g.redo.empty()) return false;
    g.undo.push_back({g.sel, Cur()});
    restore(g.redo.back());
    g.redo.pop_back();
    Changed();
    ColorBrowserSync();
    return true;
}
void Changed() {
    if (g.main) InvalidateRect(g.main, nullptr, FALSE);
}
uint32_t NextRandom() {
    if (!g.rnd) g.rnd = (uint32_t)GetTickCount() * 2654435761u ^ (uint32_t)GetCurrentProcessId() * 40503u ^ 0x5bd1e995u;
    g.rnd ^= g.rnd << 13; g.rnd ^= g.rnd >> 17; g.rnd ^= g.rnd << 5;
    return g.rnd ? g.rnd : 1u;
}
std::wstring ExePath() {
    wchar_t b[MAX_PATH];
    GetModuleFileNameW(nullptr, b, MAX_PATH);
    return b;
}

// ---------------------------------------------------------------------------------------------
// Persistence: %APPDATA%\IrixBackground\state.txt (UTF-8, INI-like, exact float round trip)
// ---------------------------------------------------------------------------------------------
static std::wstring statePath() { return AppDataDir() + L"\\state.txt"; }

static void migrateV1() {
    // v1 kept per-pattern colours/seed/fit and the applied background in settings.ini
    std::wstring old = AppDataDir() + L"\\settings.ini", text;
    if (!ReadTextFile(old, text)) {
        // v1 wrote the INI through WritePrivateProfileString, which may be ANSI
        HANDLE f = CreateFileW(old.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) return;
        DWORD sz = GetFileSize(f, nullptr), rd = 0;
        std::string b(sz, '\0');
        if (sz) ReadFile(f, &b[0], sz, &rd, nullptr);
        CloseHandle(f);
        int n = MultiByteToWideChar(CP_ACP, 0, b.data(), (int)b.size(), nullptr, 0);
        text.assign(n, L'\0');
        if (n) MultiByteToWideChar(CP_ACP, 0, b.data(), (int)b.size(), &text[0], n);
    }
    for (auto& s : ParseIni(text)) {
        std::map<std::wstring, std::wstring> kv = s.kv;
        if (s.name.rfind(L"Pattern ", 0) == 0) {
            kv[L"pattern"] = s.name.substr(8);
            Config c;
            // a retired pattern's section must not overwrite its successor's own settings
            if (FindPattern(kv[L"pattern"]) >= 0 && ConfigFromKeys(kv, c, nullptr)) g.cfg[c.pattern] = c;
        } else if (_wcsicmp(s.name.c_str(), L"Applied") == 0) {
            Config c;
            if (ConfigFromKeys(kv, c, nullptr)) { g.applied = c; g.hasApplied = true; }
        } else if (_wcsicmp(s.name.c_str(), L"General") == 0) {
            int p = FindPatternOrSuccessor(kv[L"selected"]);
            if (p >= 0) g.sel = p;
            // v1's single Pixel Size is not carried over: every pattern now has its own
            // best-looking default (copying v1's value to all of them made most open at 4x)
        }
    }
}

// Settings written by 2.0 - 2.1.0 (state file version 2) need their Pixel Sizes brought up to
// date once (the file is then saved as version 3):
//  - 1x was the default for every pattern then, so a stored 1x simply means "default": it
//    becomes the pattern's own default now;
//  - v1's single Pixel Size was copied to every tiled pattern on migration; a value stored
//    identically on that many patterns was not chosen pattern by pattern, so those go back to
//    their own default too;
//  - a 2x-4x set on just a few patterns was a choice and is kept.
// The applied wallpaper is left exactly as it is.
static void repairSpreadPixelSize(const std::vector<bool>& fromFile) {
    const auto& P = Patterns();
    int count[5] = {0, 0, 0, 0, 0};
    for (size_t i = 0; i < P.size(); i++)
        if (fromFile[i] && IsTiled(P[i].kind) && P[i].kind != K_SOLID) count[std::clamp(g.cfg[i].zoom, 1, 4)]++;
    int fixed = 0;
    for (size_t i = 0; i < P.size(); i++) {
        if (!fromFile[i] || P[i].kind == K_HEADER) continue;
        int z = g.cfg[i].zoom, d = DefaultZoom((int)i);
        bool tiled = IsTiled(P[i].kind) && P[i].kind != K_SOLID;
        if (!tiled || z == 1 || (z >= 2 && z <= 4 && count[z] >= 8)) {
            if (tiled && z != d && z != 1) fixed++;
            g.cfg[i].zoom = d;
        }
    }
    if (fixed) g.status = L"Pixel Size reset to each pattern's own default on " + std::to_wstring(fixed) +
                          L" patterns (an earlier version had copied your old v1 Pixel Size to all of them).";
}

void LoadState() {
    const auto& P = Patterns();
    g.cfg.clear();
    int first = -1;
    for (size_t i = 0; i < P.size(); i++) {
        if (P[i].kind == K_HEADER) { Config c; c.pattern = (int)i; g.cfg.push_back(c); continue; }
        if (first < 0) first = (int)i;
        g.cfg.push_back(DefaultConfig((int)i));
    }
    int bulge = FindPattern(L"Bulge");
    g.sel = bulge >= 0 ? bulge : first;
    std::wstring text;
    int version = 3;
    std::vector<bool> fromFile(P.size(), false);   // pattern settings that came from state.txt
    int savedTab = -1;
    if (!ReadTextFile(statePath(), text)) {
        migrateV1();
    } else {
        version = 2;
        for (auto& s : ParseIni(text)) {
            if (_wcsicmp(s.name.c_str(), L"General") == 0) {
                auto get = [&](const wchar_t* k) { auto it = s.kv.find(k); return it == s.kv.end() ? std::wstring() : it->second; };
                int p = FindPatternOrSuccessor(get(L"selected"));
                if (p >= 0) g.sel = p;
                std::wstring h = get(L"harmony");
                if (!h.empty()) g.harmony = std::clamp((int)wcstol(h.c_str(), nullptr, 10), 0, HM_COUNT - 1);
                std::wstring si = get(L"showicons");
                if (!si.empty()) g.showIcons = si != L"0";
                std::wstring tb = get(L"tab");
                if (!tb.empty()) savedTab = (int)wcstol(tb.c_str(), nullptr, 10);
                std::wstring wr = get(L"window");
                RECT r{};
                if (!wr.empty() && swscanf(wr.c_str(), L"%ld,%ld,%ld,%ld", &r.left, &r.top, &r.right, &r.bottom) == 4 && r.right > r.left && r.bottom > r.top)
                    g.winRect = r;
                g.winMax = get(L"maximized") == L"1";
                std::wstring lk = get(L"locks");
                for (int i = 0; i < 4 && i * 2 < (int)lk.size(); i++) g.lock[i] = lk[i * 2] == L'1';
                g.presetSel = -1;
                int th = FindScheme(get(L"theme"));
                if (th >= 0) SetScheme(th);
                std::wstring ver = get(L"version");
                if (!ver.empty()) version = (int)wcstol(ver.c_str(), nullptr, 10);
            } else if (s.name.rfind(L"Pattern ", 0) == 0) {
                std::map<std::wstring, std::wstring> kv = s.kv;
                if (!kv.count(L"pattern")) kv[L"pattern"] = s.name.substr(8);
                Config c;
                if (FindPattern(kv[L"pattern"]) >= 0 && ConfigFromKeys(kv, c, nullptr)) { g.cfg[c.pattern] = c; fromFile[c.pattern] = true; }
            } else if (_wcsicmp(s.name.c_str(), L"Applied") == 0) {
                Config c;
                if (ConfigFromKeys(s.kv, c, nullptr)) { g.applied = c; g.hasApplied = true; }
            }
        }
    }
    if (version < 3) repairSpreadPixelSize(fromFile);
    if (savedTab >= 0) {
        // 2.4 added the Transform tab after Pattern: Adjust and Presets & Export moved one along
        if (version < 4 && savedTab >= 2) savedTab++;
        g.tab = std::clamp(savedTab, 0, TAB_COUNT - 1);
    }
    g.watch = WatchRegistered();
    g.userPalettes = LoadUserPalettes();
    g.presets = LoadPresets();
}

void SaveState() {
    const auto& P = Patterns();
    std::wstring s = L"[General]\r\nVersion=4\r\nSelected=" + P[g.sel].name + L"\r\nHarmony=" + std::to_wstring(g.harmony) +
                     L"\r\nShowIcons=" + std::to_wstring(g.showIcons ? 1 : 0) + L"\r\nTab=" + std::to_wstring(g.tab) + L"\r\nLocks=";
    for (int i = 0; i < 4; i++) s += std::wstring(i ? L"," : L"") + (g.lock[i] ? L"1" : L"0");
    s += L"\r\nTheme=" + std::wstring(Scheme(CurrentScheme()).key) + L"\r\n";
    if (g.winRect.right > g.winRect.left) {
        wchar_t b[96];
        swprintf(b, 96, L"Window=%ld,%ld,%ld,%ld\r\nMaximized=%d\r\n", g.winRect.left, g.winRect.top, g.winRect.right, g.winRect.bottom, g.winMax ? 1 : 0);
        s += b;
    }
    for (size_t i = 0; i < P.size(); i++) {
        if (P[i].kind == K_HEADER) continue;
        if (g.cfg[i] == DefaultConfig((int)i)) continue;
        s += L"\r\n[Pattern " + P[i].name + L"]\r\n" + ConfigKeysText(g.cfg[i]);
    }
    if (g.hasApplied) s += L"\r\n[Applied]\r\n" + ConfigKeysText(g.applied);
    WriteTextFile(statePath(), s);
}

bool LoadApplied(Config& c) {
    std::wstring text;
    if (!ReadTextFile(statePath(), text)) return false;
    for (auto& s : ParseIni(text))
        if (_wcsicmp(s.name.c_str(), L"Applied") == 0) return ConfigFromKeys(s.kv, c, nullptr);
    return false;
}
void SaveApplied(const Config& c) {
    g.applied = c;
    g.hasApplied = true;
    SaveState();
}

// ---------------------------------------------------------------------------------------------
// Watcher: hidden process that re-applies the wallpaper when the display layout changes
// ---------------------------------------------------------------------------------------------
bool WatchRegistered() {
    HKEY k;
    bool on = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        on = RegQueryValueExW(k, kRunValue, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return on;
}
void SetWatch(bool on) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        if (on) {
            std::wstring cmd = L"\"" + ExePath() + L"\" --watch";
            RegSetValueExW(k, kRunValue, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.size() + 1) * 2);
        } else {
            RegDeleteValueW(k, kRunValue);
        }
        RegCloseKey(k);
    }
    HWND w = FindWindowW(kWatchClass, nullptr);
    if (on && !w && !g_selftest) ShellExecuteW(nullptr, L"open", ExePath().c_str(), L"--watch", nullptr, SW_HIDE);
    if (!on && w) PostMessageW(w, WM_CLOSE, 0, 0);
    g.watch = on;
}

// Keep Synced was switched on from another copy of the program - an earlier version, or this one
// before it was renamed or moved: point the sign-in entry at this copy and restart the watcher
// from it, so the setting keeps working instead of launching a missing or outdated exe.
void RefreshWatch() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return;
    wchar_t cmd[2048];
    DWORD sz = sizeof(cmd) - sizeof(wchar_t), type = 0;
    LONG r = RegQueryValueExW(k, kRunValue, nullptr, &type, (BYTE*)cmd, &sz);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return;
    cmd[std::min<DWORD>(sz / sizeof(wchar_t), 2047)] = 0;
    std::wstring want = L"\"" + ExePath() + L"\" --watch";
    if (_wcsicmp(cmd, want.c_str()) == 0) return;
    if (HWND old = FindWindowW(kWatchClass, nullptr)) {   // stop the other copy's watcher first
        PostMessageW(old, WM_CLOSE, 0, 0);
        for (int i = 0; i < 40 && FindWindowW(kWatchClass, nullptr); i++) Sleep(50);
    }
    SetWatch(true);
}

static std::wstring layoutSignature() {
    std::wstring s;
    for (auto& d : EnumDisplays()) {
        wchar_t b[96];
        swprintf(b, 96, L"%ld,%ld,%ld,%ld%ls;", d.rc.left, d.rc.top, d.rc.right, d.rc.bottom, d.primary ? L"p" : L"");
        s += b;
    }
    return s;
}
static std::wstring g_lastSig;
static void watcherApply(bool force) {
    try {
        std::wstring sig = layoutSignature();
        if (!force && sig == g_lastSig) return;
        g_lastSig = sig;
        // only while our wallpaper is still the desktop wallpaper: if it has since been changed in
        // Windows Settings (or by another program), leave the user's new choice alone
        wchar_t cur[MAX_PATH * 2] = L"";
        SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH * 2, cur, 0);
        std::wstring ours = AppDataDir() + L"\\wallpaper_";
        if (_wcsnicmp(cur, ours.c_str(), ours.size()) != 0) return;
        CrashContext(L"re-applying the wallpaper for displays %ls", sig.c_str());
        Config c;
        if (LoadApplied(c)) ApplyWallpaper(c, nullptr);
        EngineTrimCaches();   // the watcher idles in the background: keep it small
        CrashContext(L"waiting for display changes");
    } catch (...) {
        CrashRecovered(L"display watcher");   // keep watching; the next change tries again
    }
}
static LRESULT CALLBACK WatchProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_DISPLAYCHANGE: case WM_DPICHANGED: SetTimer(h, 1, 2000, nullptr); return 0;
    case WM_TIMER: KillTimer(h, 1); watcherApply(false); return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
int RunWatcher() {
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\IrixBackgroundWatcher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // another watcher is running - or one that was just asked to quit (RefreshWatch) is
        // still shutting down: wait a moment for it, otherwise leave the job to it
        DWORD w = mtx ? WaitForSingleObject(mtx, 3000) : WAIT_TIMEOUT;
        if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) { if (mtx) CloseHandle(mtx); return 0; }
    }
    EngineInit();
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WatchProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWatchClass;
    RegisterClassExW(&wc);
    // a real (never shown) top-level window, so it receives WM_DISPLAYCHANGE broadcasts
    CreateWindowExW(WS_EX_TOOLWINDOW, kWatchClass, APP_NAME L" display watcher", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    watcherApply(true);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
    return 0;
}
