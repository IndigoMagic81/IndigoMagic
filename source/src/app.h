// Application state shared by the main window, the colour browser and the render worker
#pragma once
#include "ui.h"
#include "palette.h"
#include "presets.h"
#include <deque>
#include <functional>

#define WM_APP_PREVIEW (WM_APP + 1)
#define WM_APP_THUMB (WM_APP + 2)
#define WM_APP_TASK (WM_APP + 3)

enum Tab { TAB_COLORS = 0, TAB_PATTERN, TAB_TRANSFORM, TAB_ADJUST, TAB_PRESETS, TAB_COUNT };

struct EditState {
    int sel;
    Config cfg;
    bool operator==(const EditState& o) const { return sel == o.sel && cfg == o.cfg; }
};

struct App {
    HINSTANCE inst = nullptr;
    HWND main = nullptr, cb = nullptr, search = nullptr;
    std::vector<Config> cfg;       // one remembered configuration per pattern
    int sel = 0;
    int harmony = HM_AUTO;
    bool lock[4] = {false, false, false, false};
    bool showIcons = true;
    bool watch = false;
    int tab = TAB_COLORS;
    int category = 0;              // 0 = everything, >0 = heading index + 1, -1 = text-friendly picks
    std::wstring searchText;
    std::vector<Palette> userPalettes;
    std::vector<Preset> presets;
    int presetSel = -1;
    std::wstring paletteSelName;   // highlighted palette in the library
    std::deque<EditState> undo, redo;
    std::wstring status;
    bool busy = false;
    HANDLE taskThread = nullptr;
    bool hasApplied = false;
    Config applied;
    uint32_t rnd = 0;
    RECT winRect{};                // last normal (not minimised / maximised) window rectangle
    bool winMax = false;           // ...and whether the window was maximised
};
extern App g;
Config& Cur();
void Edit();                       // snapshot for undo - call before changing Cur()/selection
void Changed();                    // repaint + mark settings dirty
uint32_t NextRandom();

// worker.cpp ----------------------------------------------------------------------------------
struct PreviewResult {
    std::wstring key;
    bool tiled = false;
    Config cfg;
    std::shared_ptr<const Image> tile;     // tiled kinds
    std::shared_ptr<const Image> image;    // display kinds (miniature of the monitor)
    Readability read;
    double ms = 0;
    std::wstring error;                    // the render failed (details went to crash.log)
};
std::wstring PreviewKey(const Config& c, int w, int h);
void WorkerStart(HWND notify);
void WorkerStop();
void RequestPreview(const Config& c, int w, int h);
bool LatestPreview(PreviewResult& out);
bool PreviewPending();
std::shared_ptr<const Image> GetThumb(const std::wstring& key);
void RequestThumb(const std::wstring& key, const Config& c, int w, int h);
std::wstring ThumbKey(const Config& c, int w, int h);
// background task (apply / export); fn runs on its own thread, result arrives as WM_APP_TASK
// (wParam = success, lParam = new std::wstring* message which the receiver deletes)
HANDLE StartTask(std::function<bool(std::wstring&)> fn, const std::wstring& what);

// colourbrowser.cpp ---------------------------------------------------------------------------
void RegisterColorBrowser(HINSTANCE inst);
void OpenColorBrowser(int slot);
void CloseColorBrowser(bool revert);
int ColorBrowserSlot();             // -1 when closed
HWND ColorBrowserWindow();
void ColorBrowserSync();            // current colour changed elsewhere (undo, randomise...)
bool IsColorBrowserMessage(MSG& m);

// themes.cpp ----------------------------------------------------------------------------------
void RegisterThemeOptions(HINSTANCE inst);
void OpenThemeOptions();            // the Theme Options window (IRIX colour schemes)
void CloseThemeOptions();
void ApplyTheme(int scheme);        // recolour every window and remember the choice
HWND ThemeOptionsWindow();
int ThemeOptionsSelection();        // highlighted scheme, -1 when closed
bool ThemeOptionsCapture(Image& out);
RECT ThemeOptionsButton(int i);
RECT ThemeOptionsRow(int i);

// app_state.cpp -------------------------------------------------------------------------------
extern bool g_selftest;            // --selftest: dialogs are answered automatically
bool Undo();
bool Redo();
void LoadState();                  // per-pattern settings, selection, UI state (migrates v1 settings.ini)
void SaveState();
bool LoadApplied(Config& c);
void SaveApplied(const Config& c);
bool WatchRegistered();
void SetWatch(bool on);
int RunWatcher();
void RefreshWatch();               // re-point Keep Synced at this copy of the exe
std::wstring ExePath();
