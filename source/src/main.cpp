// Indigo Magic - the IRIX "Background Setting" panel for Windows
// Main panel: pattern browser, live desktop-locked preview, Colors / Pattern / Adjust / Presets & Export
// tabs, undo/redo, wallpaper apply, display-change watcher and a built-in self test.
#define _WIN32_WINNT 0x0A00
#include "app.h"
#include "crash.h"
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <stdexcept>

static const wchar_t* kMainClass = L"IrixBackgroundSetting";
static bool g_active = true;
static BackBuf g_bb;
static HICON g_iconBig = nullptr;
static int g_iconSize = 0;

// ---------------------------------------------------------------------------------------------
// Immediate-mode widgets: rebuilt from the current state on every layout, so what is drawn,
// what is hit-tested and what is executed always come from the same description.
// ---------------------------------------------------------------------------------------------
enum WT { WT_BUTTON, WT_CHECK, WT_RADIO, WT_SLIDER, WT_OPTION, WT_TAB, WT_SWATCH, WT_LOCK, WT_DICE, WT_TEXT, WT_GROUP };
struct MenuItem { std::wstring group, label; bool checked = false; };
struct Wd {
    WT type = WT_TEXT;
    std::wstring key, label, current, help;
    RECT r{}, r2{};
    bool enabled = true, checked = false;
    float value = 0, vmin = 0, vmax = 1, def = 0;
    int decimals = 0, labelW = 0, slot = -1;
    std::function<void()> click;
    std::function<void(float)> set;
    std::vector<MenuItem> items;
    std::function<void(int)> pick;
    uint32_t color = 0;
    HFONT font = nullptr;
    COLORREF tcolor = 0;
    UINT align = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
    bool interactive() const { return type != WT_TEXT && type != WT_GROUP; }
};
static std::vector<Wd> g_w;
static std::wstring g_hotKey, g_pressKey, g_dragKey;
static int g_dragOff = 0;

static Wd& add(WT t, const std::wstring& key, RECT r) {
    g_w.emplace_back();
    Wd& w = g_w.back();
    w.type = t; w.key = key; w.r = r;
    return w;
}
static Wd* findW(const std::wstring& key) {
    for (auto& w : g_w) if (w.key == key) return &w;
    return nullptr;
}
static Wd& button(const std::wstring& key, RECT r, const std::wstring& label, std::function<void()> fn, const std::wstring& help, bool en = true) {
    Wd& w = add(WT_BUTTON, key, r);
    w.label = label; w.click = std::move(fn); w.help = help; w.enabled = en;
    return w;
}
static Wd& check(const std::wstring& key, RECT r, const std::wstring& label, bool on, std::function<void()> fn, const std::wstring& help, bool en = true) {
    Wd& w = add(WT_CHECK, key, r);
    w.label = label; w.checked = on; w.click = std::move(fn); w.help = help; w.enabled = en;
    return w;
}
static Wd& radio(const std::wstring& key, RECT r, const std::wstring& label, bool on, std::function<void()> fn, const std::wstring& help, bool en = true) {
    Wd& w = add(WT_RADIO, key, r);
    w.label = label; w.checked = on; w.click = std::move(fn); w.help = help; w.enabled = en;
    return w;
}
static Wd& textW(const std::wstring& key, RECT r, const std::wstring& s, HFONT f, COLORREF c = C::text,
                 UINT align = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    Wd& w = add(WT_TEXT, key, r);
    w.label = s; w.font = f; w.tcolor = c; w.align = align;
    return w;
}
static Wd& groupW(RECT r, const std::wstring& title) {
    Wd& w = add(WT_GROUP, L"grp:" + title, r);
    w.label = title;
    return w;
}
static Wd& slider(const std::wstring& key, RECT r, const std::wstring& label, int labelW, float v, float mn, float mx, float def, int dec,
                  std::function<void(float)> set, const std::wstring& help, bool en = true) {
    Wd& w = add(WT_SLIDER, key, r);
    w.label = label; w.labelW = labelW; w.value = v; w.vmin = mn; w.vmax = mx; w.def = def; w.decimals = dec;
    w.set = std::move(set); w.help = help; w.enabled = en;
    int cy = (r.top + r.bottom) / 2;
    w.r2 = RECT{r.left + labelW, cy - S(6), r.right - S(54), cy + S(6)};
    return w;
}
static Wd& option(const std::wstring& key, RECT r, const std::wstring& label, int labelW, const std::wstring& current,
                  std::vector<MenuItem> items, std::function<void(int)> pick, const std::wstring& help, bool en = true) {
    Wd& w = add(WT_OPTION, key, r);
    w.label = label; w.labelW = labelW; w.current = current; w.items = std::move(items); w.pick = std::move(pick);
    w.help = help; w.enabled = en;
    w.r2 = RECT{r.left + labelW, r.top, r.right, r.bottom};
    return w;
}
static float sliderT(const Wd& w) { return w.vmax > w.vmin ? (w.value - w.vmin) / (w.vmax - w.vmin) : 0.f; }
static float snapValue(const Wd& w, float v) {
    v = std::clamp(v, std::min(w.vmin, w.vmax), std::max(w.vmin, w.vmax));
    float k = std::pow(10.f, (float)w.decimals);
    return std::round(v * k) / k;
}
static std::wstring fmtValue(float v, int dec) {
    wchar_t b[32];
    swprintf(b, 32, L"%.*f", dec, v);
    return b;
}

// ---------------------------------------------------------------------------------------------
// Dialog helpers (answered automatically in --selftest)
// ---------------------------------------------------------------------------------------------
static int g_stubAnswer = 0;
static std::wstring g_stubText, g_stubPath;
static int Ask(const std::wstring& title, const std::wstring& msg, const std::vector<std::wstring>& buttons, int icon, int width = 440) {
    if (g_selftest) return std::min(g_stubAnswer, (int)buttons.size() - 1);
    return MotifDialog(g.main, title, msg, buttons, icon, width);
}
static bool PromptText(const std::wstring& title, const std::wstring& prompt, std::wstring& value) {
    if (g_selftest) { if (!g_stubText.empty()) value = g_stubText; return !value.empty(); }
    return MotifPrompt(g.main, title, prompt, value);
}
static bool PickFile(bool save, const wchar_t* filter, const wchar_t* defExt, std::wstring& path) {
    if (g_selftest) { path = g_stubPath; return !path.empty(); }
    static wchar_t buf[4096];   // long paths are fine on Windows 10+
    buf[0] = 0;
    wcsncpy(buf, path.c_str(), 4095);
    buf[4095] = 0;
    OPENFILENAMEW of{};
    of.lStructSize = sizeof(of);
    of.hwndOwner = g.main;
    of.lpstrFilter = filter;
    of.lpstrFile = buf;
    of.nMaxFile = 4096;
    of.lpstrDefExt = defExt;
    of.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR);
    BOOL ok = save ? GetSaveFileNameW(&of) : GetOpenFileNameW(&of);
    if (ok) path = buf;
    return ok != FALSE;
}
static std::wstring safeFileName(std::wstring s) {
    for (auto& ch : s) if (wcschr(L"\\/:*?\"<>|", ch)) ch = L'_';
    return s.empty() ? L"background" : s;
}
static std::wstring hexOf(uint32_t c) {
    wchar_t b[16];
    swprintf(b, 16, L"#%06X", c & 0xffffff);
    return b;
}

// ---------------------------------------------------------------------------------------------
// Lists (patterns, presets) - Motif scrolled lists with thumbnails
// ---------------------------------------------------------------------------------------------
struct ListView {
    RECT r{}, items{}, sb{}, up{}, dn{}, track{}, thumb{};
    int rowH = 20, top = 0, vis = 1, count = 0;
    void layout(RECT area, int rowHeight, int n) {
        r = area; rowH = rowHeight; count = n;
        RECT in{r.left + 2, r.top + 2, r.right - 2, r.bottom - 2};
        int sbw = S(17);
        sb = RECT{in.right - sbw, in.top, in.right, in.bottom};
        items = RECT{in.left, in.top + 1, sb.left - 1, in.bottom - 1};
        vis = std::max(1, (int)((items.bottom - items.top) / rowH));
        up = RECT{sb.left + 1, sb.top + 1, sb.right - 1, sb.top + sbw - 1};
        dn = RECT{sb.left + 1, sb.bottom - sbw + 1, sb.right - 1, sb.bottom - 1};
        track = RECT{sb.left + 1, up.bottom, sb.right - 1, dn.top};
        top = std::clamp(top, 0, maxTop());
        int th = track.bottom - track.top;
        int tH = count <= vis ? th : std::max(S(18), th * vis / std::max(1, count));
        int ty = track.top + (maxTop() ? (th - tH) * top / maxTop() : 0);
        thumb = RECT{track.left, ty, track.right, ty + tH};
    }
    int maxTop() const { return std::max(0, count - vis); }
    void ensure(int row) {
        if (row < 0) return;
        if (row < top) top = row;
        if (row >= top + vis) top = row - vis + 1;
        top = std::clamp(top, 0, maxTop());
    }
    int rowAt(POINT p) const {
        if (!In(items, p)) return -1;
        int i = top + (p.y - items.top) / rowH;
        return i < count ? i : -1;
    }
};
static ListView g_pl, g_prl;
static std::vector<int> g_rows;   // pattern list rows (pattern indices, headers included)

static std::wstring lower(std::wstring s) { for (auto& c : s) c = towlower(c); return s; }
static void computeRows() {
    const auto& P = Patterns();
    g_rows.clear();
    std::wstring q = lower(g.searchText);
    int hdr = -1;
    bool hdrAdded = false;
    for (int i = 0; i < (int)P.size(); i++) {
        if (P[i].kind == K_HEADER) { hdr = i; hdrAdded = false; continue; }
        bool catOk = g.category == 0 || (g.category > 0 && P[i].category == g.category - 1) || (g.category == -1 && P[i].textFriendly);
        bool qOk = q.empty() || lower(P[i].name).find(q) != std::wstring::npos || lower(P[i].blurb).find(q) != std::wstring::npos ||
                   (hdr >= 0 && lower(P[hdr].name).find(q) != std::wstring::npos);
        if (!catOk || !qOk) continue;
        if (!hdrAdded && hdr >= 0) { g_rows.push_back(hdr); hdrAdded = true; }
        g_rows.push_back(i);
    }
}
static int rowOfPattern(int pat) {
    for (int i = 0; i < (int)g_rows.size(); i++) if (g_rows[i] == pat) return i;
    return -1;
}

// ---------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------
enum TaskKind { TASK_NONE, TASK_APPLY, TASK_EXPORT };
static TaskKind g_taskKind = TASK_NONE;
static bool g_closePending = false;   // Close was pressed while a task was running
static Config g_taskCfg;

static void setStatus(const std::wstring& s) { g.status = s; Changed(); }
static void selectPattern(int pat, bool snapshot = true) {
    const auto& P = Patterns();
    if (pat < 0 || pat >= (int)P.size() || P[pat].kind == K_HEADER) return;
    if (pat == g.sel) return;
    if (snapshot) Edit();
    CrashTrail(L"select %ls", P[pat].name.c_str());
    CloseColorBrowser(false);
    for (bool& l : g.lock) l = false;   // locks belong to the colours of the pattern being edited
    g.sel = pat;
    g.status.clear();
    g_pl.ensure(rowOfPattern(pat));
    Changed();
}
static void moveSelection(int delta) {
    if (g_rows.empty()) return;
    int r = rowOfPattern(g.sel);
    if (r < 0) r = delta > 0 ? -1 : (int)g_rows.size();
    int step = delta > 0 ? 1 : -1, left = std::abs(delta);
    int target = -1;
    while (left > 0) {
        int j = r + step;
        while (j >= 0 && j < (int)g_rows.size() && Patterns()[g_rows[j]].kind == K_HEADER) j += step;
        if (j < 0 || j >= (int)g_rows.size()) break;
        r = j; target = j; left--;
    }
    if (target >= 0) selectPattern(g_rows[target]);
}

static void runTask(TaskKind kind, const Config& cfg, std::function<bool(std::wstring&)> fn) {
    if (g.busy) return;
    g_taskKind = kind;
    g_taskCfg = cfg;
    if (g_selftest) {   // synchronous so the test can check the result
        std::wstring msg;
        bool ok = fn(msg);
        if (ok && kind == TASK_APPLY) SaveApplied(cfg);
        g.status = msg;
        g_taskKind = TASK_NONE;
        return;
    }
    g.busy = true;
    g.status = kind == TASK_APPLY ? L"Applying the background..." : L"Rendering the image...";
    HANDLE h = StartTask(std::move(fn), (kind == TASK_APPLY ? L"applying \"" : L"exporting \"") + Patterns()[cfg.pattern].name + L"\" (" + ConfigKey(cfg) + L")");
    if (h) CloseHandle(h);
    else { g.busy = false; g.status = L"Could not start the background task."; }
    Changed();
}
static void doApply() {
    Config c = Cur();
    runTask(TASK_APPLY, c, [c](std::wstring& msg) { return ApplyWallpaper(c, &msg); });
}

static int usedSlots() { return std::clamp(Patterns()[g.sel].ncolors, 1, 4); }
static void randomizePalette() {
    int n = usedSlots();
    bool anyFree = false;
    for (int i = 0; i < n; i++) anyFree |= !g.lock[i];
    if (!anyFree) { setStatus(L"All colours are locked - unlock one to randomize."); return; }
    Edit();
    uint32_t out[4];
    RandomPalette(g.sel, g.harmony, Cur().colors, g.lock, NextRandom(), out);
    for (int i = 0; i < 4; i++) if (!(i < n && g.lock[i])) Cur().colors[i] = out[i] & 0xffffff;
    g.status = std::wstring(L"New ") + HarmonyName(g.harmony) + L" palette.";
    Changed();
    ColorBrowserSync();
}
static void randomizeSlot(int i) {
    if (i < 0 || i >= usedSlots() || g.lock[i]) return;
    Edit();
    Cur().colors[i] = RandomSlot(g.sel, g.harmony, Cur().colors, i, NextRandom()) & 0xffffff;
    Changed();
    ColorBrowserSync();
}
static void shufflePalette() {
    int n = usedSlots();
    std::vector<int> freeIdx;
    for (int i = 0; i < n; i++) if (!g.lock[i]) freeIdx.push_back(i);
    if (freeIdx.size() < 2) return;
    Edit();
    std::vector<uint32_t> vals;
    for (int i : freeIdx) vals.push_back(Cur().colors[i]);
    std::vector<uint32_t> orig = vals;
    for (int tries = 0; tries < 8 && vals == orig; tries++)
        for (int i = (int)vals.size() - 1; i > 0; i--) std::swap(vals[i], vals[NextRandom() % (i + 1)]);
    if (vals == orig) std::rotate(vals.begin(), vals.begin() + 1, vals.end());
    for (size_t k = 0; k < freeIdx.size(); k++) Cur().colors[freeIdx[k]] = vals[k];
    Changed();
    ColorBrowserSync();
}
static void applyPalette(const uint32_t pal[4], const std::wstring& name) {
    Edit();
    uint32_t out[4];
    ApplyPaletteToPattern(g.sel, pal, out);
    int n = usedSlots();
    for (int i = 0; i < 4; i++) if (!(i < n && g.lock[i])) Cur().colors[i] = out[i] & 0xffffff;
    g.status = L"Palette \"" + name + L"\" applied" + (std::any_of(g.lock, g.lock + n, [](bool b) { return b; }) ? L" (locked colours kept)." : L".");
    Changed();
    ColorBrowserSync();
}
static std::vector<Palette> allPalettes() {
    std::vector<Palette> v = BuiltinPalettes();
    for (auto& p : g.userPalettes) v.push_back(p);
    return v;
}
static void savePaletteAs() {
    std::wstring name = L"My Palette " + std::to_wstring(g.userPalettes.size() + 1);
    if (!PromptText(L"Save Palette", L"Name for this palette:", name)) return;
    name = CleanPresetName(name);
    for (auto& ch : name) if (ch == L'=') ch = L'-';   // the palette file stores Name=colours
    if (name.empty()) { setStatus(L"A palette needs a name."); return; }
    Palette p;
    p.name = name; p.group = L"My Palettes"; p.builtin = false;
    for (int i = 0; i < 4; i++) p.c[i] = Cur().colors[i];
    bool replaced = false;
    for (auto& e : g.userPalettes) if (_wcsicmp(e.name.c_str(), name.c_str()) == 0) { e = p; replaced = true; }
    if (!replaced) g.userPalettes.push_back(p);
    setStatus(SaveUserPalettes(g.userPalettes) ? L"Palette \"" + name + L"\" saved to the library." : L"Could not save the palette file.");
}
static void deleteUserPalette(int k) {
    if (k < 0 || k >= (int)g.userPalettes.size()) return;
    std::wstring name = g.userPalettes[k].name;
    if (Ask(L"Delete Palette", L"Delete the palette \"" + name + L"\" from the library?", {L"Delete", L"Cancel"}, 1) != 0) return;
    g.userPalettes.erase(g.userPalettes.begin() + k);
    setStatus(SaveUserPalettes(g.userPalettes) ? L"Palette \"" + name + L"\" deleted." : L"Could not update the palette file.");
}
static void importPalette() {
    std::wstring path;
    if (!PickFile(false, L"Palettes (*.gpl;*.txt)\0*.gpl;*.txt\0All files\0*.*\0", L"gpl", path)) return;
    std::wstring name, err;
    uint32_t c[4];
    if (!ImportPaletteFile(path, name, c, &err)) { setStatus(L"Import failed: " + err); return; }
    applyPalette(c, name);
    Palette p;
    p.name = CleanPresetName(name.empty() ? L"Imported" : name); p.group = L"My Palettes"; p.builtin = false;
    for (auto& ch : p.name) if (ch == L'=') ch = L'-';
    if (p.name.empty()) p.name = L"Imported";
    for (int i = 0; i < 4; i++) p.c[i] = c[i];
    bool replaced = false;
    for (auto& e : g.userPalettes) if (_wcsicmp(e.name.c_str(), p.name.c_str()) == 0) { e = p; replaced = true; }
    if (!replaced) g.userPalettes.push_back(p);
    SaveUserPalettes(g.userPalettes);
    setStatus(L"Imported palette \"" + p.name + L"\" (added to My Palettes).");
}
static void exportPalette() {
    std::wstring path = safeFileName(Patterns()[g.sel].name + L" palette") + L".gpl";
    if (!PickFile(true, L"GIMP palette (*.gpl)\0*.gpl\0", L"gpl", path)) return;
    std::wstring err;
    setStatus(ExportPaletteFile(path, Patterns()[g.sel].name + L" palette", Cur().colors, &err) ? L"Palette exported to " + path : L"Export failed: " + err);
}

// presets
static void reloadPresets(const std::wstring& selectName) {
    g.presets = LoadPresets();
    g.presetSel = selectName.empty() ? -1 : FindPreset(g.presets, selectName);
    if (g.presetSel >= 0) g_prl.ensure(g.presetSel);
}
static void loadPreset(int k) {
    if (k < 0 || k >= (int)g.presets.size()) return;
    Edit();
    CloseColorBrowser(false);
    Config c = g.presets[k].cfg;
    SanitizeConfig(c);
    if (c.pattern != g.sel) for (bool& l : g.lock) l = false;
    g.sel = c.pattern;
    g.cfg[c.pattern] = c;
    if (rowOfPattern(g.sel) < 0) { g.category = 0; g.searchText.clear(); if (g.search) SetWindowTextW(g.search, L""); computeRows(); }
    g_pl.ensure(rowOfPattern(g.sel));
    setStatus(L"Loaded preset \"" + g.presets[k].name + L"\".");
}
static void savePresetNew() {
    std::wstring name = Patterns()[g.sel].name;
    if (!PromptText(L"Save Preset", L"Name for this preset:", name)) return;
    name = CleanPresetName(name);
    if (name.empty()) { setStatus(L"A preset needs a name."); return; }
    if (FindPreset(g.presets, name) >= 0 &&
        Ask(L"Save Preset", L"A preset called \"" + name + L"\" already exists. Replace it?", {L"Replace", L"Cancel"}, 1) != 0)
        return;
    std::wstring err;
    if (!SavePreset(name, Cur(), &err)) { setStatus(L"Could not save: " + err); return; }
    reloadPresets(name);
    setStatus(L"Saved preset \"" + name + L"\".");
}
static void updatePreset() {
    if (g.presetSel < 0) return;
    std::wstring name = g.presets[g.presetSel].name, err;
    if (Ask(L"Update Preset", L"Replace \"" + name + L"\" with the current settings?", {L"Replace", L"Cancel"}, 1) != 0) return;
    if (!SavePreset(name, Cur(), &err)) { setStatus(L"Could not save: " + err); return; }
    reloadPresets(name);
    setStatus(L"Updated preset \"" + name + L"\".");
}
static void renamePreset() {
    if (g.presetSel < 0) return;
    std::wstring old = g.presets[g.presetSel].name, name = old, err;
    if (!PromptText(L"Rename Preset", L"New name:", name)) return;
    name = CleanPresetName(name);
    if (name.empty() || name == old) return;
    if (!RenamePreset(old, name, &err)) { setStatus(L"Could not rename: " + err); return; }
    reloadPresets(name);
    setStatus(L"Renamed to \"" + name + L"\".");
}
static void deletePreset() {
    if (g.presetSel < 0) return;
    std::wstring name = g.presets[g.presetSel].name, err;
    if (Ask(L"Delete Preset", L"Delete the preset \"" + name + L"\"?", {L"Delete", L"Cancel"}, 1) != 0) return;
    if (!DeletePreset(name, &err)) { setStatus(L"Could not delete: " + err); return; }
    reloadPresets(L"");
    setStatus(L"Deleted preset \"" + name + L"\".");
}
static void importPreset() {
    std::wstring path;
    if (!PickFile(false, L"Background presets (*.irixbg)\0*.irixbg\0All files\0*.*\0", L"irixbg", path)) return;
    Config c;
    std::wstring name, err;
    if (!ImportPresetFile(path, c, name, &err)) { setStatus(L"Import failed: " + err); return; }
    name = CleanPresetName(name.empty() ? Patterns()[c.pattern].name : name);
    std::wstring base = name;
    for (int k = 2; FindPreset(g.presets, name) >= 0; k++) name = base + L" (" + std::to_wstring(k) + L")";
    if (!SavePreset(name, c, &err)) { setStatus(L"Could not save: " + err); return; }
    reloadPresets(name);
    loadPreset(g.presetSel);
    setStatus(L"Imported preset \"" + name + L"\".");
}
static void exportPreset() {
    bool sel = g.presetSel >= 0;
    std::wstring name = sel ? g.presets[g.presetSel].name : Patterns()[g.sel].name;
    Config c = sel ? g.presets[g.presetSel].cfg : Cur();
    std::wstring path = safeFileName(name) + L".irixbg", err;
    if (!PickFile(true, L"Background presets (*.irixbg)\0*.irixbg\0", L"irixbg", path)) return;
    setStatus(ExportPresetFile(path, name, c, &err) ? L"Exported \"" + name + L"\" to " + path : L"Export failed: " + err);
}

// image export
struct ExportSize { std::wstring label; int code, w, h; };   // code: 0 desktop, 1 primary, 2 tile, 3 fixed
static int g_expSel = 0, g_customW = 1920, g_customH = 1080;
static std::vector<ExportSize> exportSizes() {
    std::vector<ExportSize> v;
    auto d = EnumDisplays();
    RECT vr = d[0].rc;
    RECT pr = d[0].rc;
    for (auto& x : d) { UnionRect(&vr, &vr, &x.rc); if (x.primary) pr = x.rc; }
    auto dim = [](int w, int h) { return std::to_wstring(w) + L" x " + std::to_wstring(h); };
    v.push_back({L"All displays (" + dim(vr.right - vr.left, vr.bottom - vr.top) + L")", 0, vr.right - vr.left, vr.bottom - vr.top});
    v.push_back({L"Primary display (" + dim(pr.right - pr.left, pr.bottom - pr.top) + L")", 1, pr.right - pr.left, pr.bottom - pr.top});
    v.push_back({L"Single seamless tile", 2, 0, 0});
    v.push_back({L"1920 x 1080 (Full HD)", 3, 1920, 1080});
    v.push_back({L"2560 x 1440 (QHD)", 3, 2560, 1440});
    v.push_back({L"3840 x 2160 (4K UHD)", 3, 3840, 2160});
    v.push_back({L"5120 x 2880 (5K)", 3, 5120, 2880});
    v.push_back({L"Custom: " + dim(g_customW, g_customH) + L"...", 4, g_customW, g_customH});
    return v;
}
static bool pickCustomSize() {
    std::wstring s = std::to_wstring(g_customW) + L" x " + std::to_wstring(g_customH);
    if (!PromptText(L"Custom Export Size", L"Width x Height in pixels (16 to 16384):", s)) return false;
    int w = 0, h = 0;
    if (swscanf(s.c_str(), L"%d %*[xX*,] %d", &w, &h) != 2 && swscanf(s.c_str(), L"%dx%d", &w, &h) != 2) {
        setStatus(L"Enter a size like 3440 x 1440.");
        return false;
    }
    if (w < 16 || h < 16 || w > 16384 || h > 16384) { setStatus(L"Sizes must be between 16 and 16384 pixels."); return false; }
    g_customW = w; g_customH = h;
    return true;
}
static void exportImage() {
    auto sizes = exportSizes();
    g_expSel = std::clamp(g_expSel, 0, (int)sizes.size() - 1);
    ExportSize es = sizes[g_expSel];
    bool tiled = IsTiled(Patterns()[g.sel].kind);
    if (es.code == 2 && !tiled) { setStatus(L"This background is composed per display - it has no repeating tile. Pick another size."); return; }
    std::wstring path = safeFileName(Patterns()[g.sel].name) + L".png";
    if (!PickFile(true, L"PNG image (*.png)\0*.png\0Windows bitmap (*.bmp)\0*.bmp\0", L"png", path)) return;
    Config c = Cur();
    runTask(TASK_EXPORT, c, [c, es, path](std::wstring& msg) {
        Image img;
        bool ok = true;
        if (es.code == 0) ok = ComposeDesktop(c, img, nullptr);
        else if (es.code == 2) {
            auto tile = RenderTile(c);
            int z = TileZoom(c);
            img.w = tile->w * z; img.h = tile->h * z;
            img.px.resize((size_t)img.w * img.h);
            RenderRegionFrom(c, tile.get(), img.px.data(), img.w, 0, 0, img.w, img.h);
        } else ok = RenderToSize(c, es.w, es.h, img);
        std::wstring err;
        if (!ok || !SaveImageFile(img, path, &err)) { msg = L"Export failed: " + (err.empty() ? std::wstring(L"could not render") : err); return false; }
        msg = L"Saved " + std::to_wstring(img.w) + L" x " + std::to_wstring(img.h) + L" image to " + path;
        return true;
    });
}

static void doReset() {
    int r = Ask(L"Reset Background", L"Reset the Background workarea?\n\nFactory restores every setting of this background - colours, pattern "
                L"controls and adjustments. Previous returns to the background that is currently applied.",
                {L"Factory", L"Previous", L"Cancel"}, 1);
    if (r == 0) {
        Edit();
        CloseColorBrowser(false);
        Cur() = DefaultConfig(g.sel);
        setStatus(L"Restored factory settings for " + Patterns()[g.sel].name + L".");
    } else if (r == 1) {
        Config c;
        if (!g.hasApplied && !LoadApplied(c)) { setStatus(L"Nothing has been applied yet."); return; }
        if (g.hasApplied) c = g.applied;
        Edit();
        CloseColorBrowser(false);
        SanitizeConfig(c);
        g.sel = c.pattern;
        g.cfg[c.pattern] = c;
        if (rowOfPattern(g.sel) < 0) { g.category = 0; g.searchText.clear(); if (g.search) SetWindowTextW(g.search, L""); computeRows(); }
        g_pl.ensure(rowOfPattern(g.sel));
        setStatus(L"Back to the applied background.");
    }
}

static const wchar_t* kHelpText =
    L"Pick a background on the left, shape it with the tabs, then press Apply.\n\n"
    L"Browse: \"Show\" filters by collection - IRIX Classics (exactly the IRIX list), IRIX Extras (unlisted IRIX pixmaps and the X11 "
    L"root bitmaps), the generated families Geometric, Wireframe, Fractals, Retro Raster, Decorative, Topographic, Abstract, "
    L"Procedural Textures, Demoscene, Neon 90s and Text Friendly, and Gradients - or Text-Friendly Picks. \"Find\" searches names and descriptions. "
    L"Thumbnails always show your current colours.\n\n"
    L"Colors: click a square for the Color Browser, right-click for more. The padlock keeps a colour when you randomize; "
    L"the die re-rolls just that colour. Randomize Palette builds a fresh scheme in the chosen Harmony (90s Neon, SGI "
    L"Workstation, Calm and more). The Palette Library applies, saves, imports and exports palettes (.gpl). Color "
    L"cycling repeats, shifts and mirrors the ramp of gradient-style patterns.\n\n"
    L"Pattern: each generated pattern has its own controls. Shading switches between smooth, banded, ordered "
    L"dither and IRIX error diffusion. New Variation rolls a new random seed; Set Seed types one in. The IRIX "
    L"backdrops (Citrus Citrus, Reef ...) can show or hide the sgi wordmark, which SGI's own image crops at the bottom edge.\n\n"
    L"Transform: Scale redraws a generated tile 2x, 3x or 4x larger with smooth edges (for IRIX art it is Pixel Size, "
    L"which enlarges the original pixels). Smoothing sets the antialiasing; Tile Size, Fit, Balance and Invert shape the "
    L"colours; Rotate, Mirror and Shift move the pattern (Shift slides a tile out from under your icons).\n\n"
    L"Adjust: brightness, contrast, saturation, hue, soften and Calm (flattens fine detail). Icon Labels and Strength "
    L"keep desktop icon text readable, and Make Text-Friendly finds the gentlest settings that read Good. The meter "
    L"under the preview rates label legibility live; Show Desktop Icons overlays sample icons.\n\n"
    L"Presets & Export: save complete configurations and restore them exactly, import and export .irixbg files, and "
    L"export images (PNG or BMP) at desktop, display, tile, 4K, 5K or custom sizes.\n\n"
    L"Synced tiling: tiles form one continuous plane across all displays, locked to the primary display's top-left "
    L"corner, and Windows spans the bitmap 1:1. The preview is locked the same way - drag this window and the tiles "
    L"stay put. Keep Synced re-applies at sign-in and whenever displays change.\n\n"
    L"Theme Options (top right, or the window menu): recolour this program with any of the IRIX 6.5 desktop schemes - "
    L"Indigo Magic, Rio, Gotham, Inverness, Key West, Potrero and the rest - taken from IRIX's own scheme files.\n\n"
    L"Keys: Ctrl+Z / Ctrl+Y undo and redo, Ctrl+R randomize palette, Ctrl+N new variation, Ctrl+S save preset, "
    L"Ctrl+F find, Ctrl+T theme options, Ctrl+Tab next tab, Enter apply, F1 help, Esc close.";
static const UINT kIdmAbout = 0x0010;   // "About Indigo Magic..." in the window menu (a WM_SYSCOMMAND id)
static void showAbout() {
    int n = 0, orig = 0;
    for (auto& p : Patterns()) if (p.kind != K_HEADER) { n++; if (p.kind == K_DTILE || p.kind == K_DSCENE) orig++; }
    std::wstring t = std::wstring(APP_NAME) + L"\nVersion " APP_VERSION L"  (built " __DATE__ L")\n"
                     L"For Windows 10 and 11, 64-bit.\n\n"
                     L"The IRIX 6.5 \"Background Setting\" panel from SGI's Indigo Magic desktop, recreated and expanded: " +
                     std::to_wstring(n) +
                     L" backgrounds - the IRIX classics exactly as IRIX listed them, IRIX extras, gradients and " + std::to_wstring(orig) +
                     L" generated patterns in families from Geometric to Procedural Textures - drawn smooth at any size, with synced tiling across "
                     L"every display, and the IRIX desktop colour schemes as themes.\n\nSettings, presets and palettes:\n" + AppDataDir() +
                     L"\n\nIRIX, Indigo Magic and the SGI logo are trademarks of Silicon Graphics, Inc. (now Hewlett Packard "
                     L"Enterprise). This is an unofficial fan recreation, not affiliated with or endorsed by SGI or HPE.";
    CrashTrail(L"about");
    if (Ask(L"About " APP_NAME, t, {L"Open Settings Folder", L"Close"}, 0, 540) == 0 && !g_selftest)
        ShellExecuteW(g.main, L"open", AppDataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
static void showHelp() {
    if (Ask(APP_NAME L" " APP_VERSION L" Help", kHelpText, {L"About...", L"Close"}, 0, 620) == 0) showAbout();
}

// swatch context menu (also used by the self test)
enum { SWM_EDIT = 1, SWM_RANDOM, SWM_LOCK, SWM_COPY, SWM_PASTE };
static bool clipboardHex(uint32_t& out) {
    if (!OpenClipboard(g.main)) return false;
    bool ok = false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* s = (const wchar_t*)GlobalLock(h)) {
            std::wstring t = s;
            GlobalUnlock(h);
            size_t a = t.find_first_not_of(L" \t\r\n#");
            if (a != std::wstring::npos) {
                std::wstring hx = t.substr(a, 6);
                if (hx.size() == 6 && hx.find_first_not_of(L"0123456789abcdefABCDEF") == std::wstring::npos) {
                    out = (uint32_t)wcstoul(hx.c_str(), nullptr, 16);
                    ok = true;
                }
            }
        }
    }
    CloseClipboard();
    return ok;
}
static void copyText(const std::wstring& s) {
    if (!OpenClipboard(g.main)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (h) {
        memcpy(GlobalLock(h), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}
static void swatchCommand(int slot, int cmd) {
    if (slot < 0 || slot >= usedSlots()) return;
    switch (cmd) {
    case SWM_EDIT: OpenColorBrowser(slot); break;
    case SWM_RANDOM: randomizeSlot(slot); break;
    case SWM_LOCK: g.lock[slot] = !g.lock[slot]; Changed(); break;
    case SWM_COPY: copyText(hexOf(Cur().colors[slot])); setStatus(L"Copied " + hexOf(Cur().colors[slot]) + L"."); break;
    case SWM_PASTE: {
        uint32_t c;
        if (!clipboardHex(c)) { setStatus(L"The clipboard does not hold a #RRGGBB colour."); break; }
        Edit();
        Cur().colors[slot] = c;
        ColorBrowserSync();
        setStatus(L"Pasted " + hexOf(c) + L".");
        break;
    }
    }
}
// Popup menus go through here; the random-input self test picks a random enabled item instead
// of waiting for a real user.
static bool g_monkey = false;
static uint32_t g_monkeyRnd = 1;
static uint32_t monkeyRand() { g_monkeyRnd ^= g_monkeyRnd << 13; g_monkeyRnd ^= g_monkeyRnd >> 17; g_monkeyRnd ^= g_monkeyRnd << 5; return g_monkeyRnd; }
static void collectMenuIds(HMENU m, std::vector<UINT>& ids) {
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        MENUITEMINFOW mi{sizeof(mi)};
        mi.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_STATE | MIIM_FTYPE;
        if (!GetMenuItemInfoW(m, i, TRUE, &mi)) continue;
        if (mi.hSubMenu) { collectMenuIds(mi.hSubMenu, ids); continue; }
        if ((mi.fType & MFT_SEPARATOR) || (mi.fState & MFS_DISABLED)) continue;
        ids.push_back(mi.wID);
    }
}
static int trackMenu(HMENU m, UINT flags, POINT pt) {
    if (g_monkey) {
        std::vector<UINT> ids;
        collectMenuIds(m, ids);
        if (ids.empty() || monkeyRand() % 5 == 0) return 0;   // sometimes dismissed
        return (int)ids[monkeyRand() % ids.size()];
    }
    ThemeMenu(m);
    return TrackPopupMenu(m, flags | TPM_RETURNCMD, pt.x, pt.y, 0, g.main, nullptr);
}
// The 4Dwm window menu (the button at the top left of the frame)
static const UINT kIdmTheme = 0x0020;   // "Theme Options..." (a WM_SYSCOMMAND id)
static int windowMenu(HWND h, POINT pt) {
    bool zoomed = IsZoomed(h) != FALSE;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (zoomed ? 0 : MF_GRAYED), SC_RESTORE, L"Restore");
    AppendMenuW(m, MF_STRING | (zoomed ? MF_GRAYED : 0), SC_MOVE, L"Move");
    AppendMenuW(m, MF_STRING | (zoomed ? MF_GRAYED : 0), SC_SIZE, L"Size");
    AppendMenuW(m, MF_STRING, SC_MINIMIZE, L"Minimize");
    AppendMenuW(m, MF_STRING | (zoomed ? MF_GRAYED : 0), SC_MAXIMIZE, L"Maximize");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kIdmTheme, L"Theme Options...\tCtrl+T");
    AppendMenuW(m, MF_STRING, 0x0010, L"About " APP_NAME L"...");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, SC_CLOSE, L"Close\tAlt+F4");
    int cmd;
    if (g_monkey) {   // never Move / Size (modal loops) or Close from random input
        static const UINT ok[] = {SC_RESTORE, SC_MINIMIZE, SC_MAXIMIZE, kIdmTheme, 0};
        cmd = (int)ok[monkeyRand() % 5];
    } else {
        ThemeMenu(m);
        cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTBUTTON, pt.x, pt.y, 0, h, nullptr);
    }
    DestroyMenu(m);
    return cmd;
}
static void swatchMenu(int slot, POINT screen) {
    HMENU m = CreatePopupMenu();
    uint32_t dummy;
    bool canPaste = clipboardHex(dummy);
    AppendMenuW(m, MF_STRING, SWM_EDIT, L"Edit Colour...");
    AppendMenuW(m, MF_STRING | (g.lock[slot] ? MF_GRAYED : 0), SWM_RANDOM, L"Randomize This Colour");
    AppendMenuW(m, MF_STRING | (g.lock[slot] ? MF_CHECKED : 0), SWM_LOCK, L"Lock");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, SWM_COPY, (L"Copy " + hexOf(Cur().colors[slot])).c_str());
    AppendMenuW(m, MF_STRING | (canPaste ? 0 : MF_GRAYED), SWM_PASTE, L"Paste Colour");
    int cmd = trackMenu(m, TPM_RIGHTBUTTON, screen);
    DestroyMenu(m);
    if (cmd) swatchCommand(slot, cmd);
}

// ---------------------------------------------------------------------------------------------
// Layout + widget construction
// ---------------------------------------------------------------------------------------------
struct Layout {
    FrameRects f;
    RECT icon, title, sub, search, searchLbl, preview, pvIn, panel, tabs, inner, msg;
};
static Layout L;
static const wchar_t* kTabNames[TAB_COUNT] = {L"Colors", L"Pattern", L"Transform", L"Adjust", L"Presets & Export"};

static std::wstring categoryLabel() {
    const auto& P = Patterns();
    if (g.category == -1) return L"Text-Friendly Picks";
    if (g.category > 0 && g.category <= (int)P.size() && P[g.category - 1].kind == K_HEADER) return P[g.category - 1].name;
    return L"All Backgrounds";
}

static void buildColorsTab(RECT in) {
    const Pattern& P = Patterns()[g.sel];
    Config& c = Cur();
    int n = usedSlots();
    // colour squares
    int gx = in.left, gy = in.top;
    for (int i = 0; i < n; i++) {
        int x = gx + i * S(96);
        textW(L"slotname" + std::to_wstring(i), RECT{x, gy, x + S(90), gy + S(16)}, SlotName(g.sel, i), F().small, C::text);
        Wd& sw = add(WT_SWATCH, L"sw" + std::to_wstring(i), RECT{x, gy + S(18), x + S(76), gy + S(62)});
        sw.color = c.colors[i]; sw.slot = i;
        sw.click = [i] { OpenColorBrowser(i); };
        sw.help = SlotName(g.sel, i) + L" " + hexOf(c.colors[i]) + L" - click to edit, right-click for copy, paste, lock and randomize.";
        Wd& lk = add(WT_LOCK, L"lock" + std::to_wstring(i), RECT{x, gy + S(66), x + S(24), gy + S(90)});
        lk.checked = g.lock[i]; lk.slot = i;
        lk.click = [i] { g.lock[i] = !g.lock[i]; Changed(); };
        lk.help = g.lock[i] ? L"Locked: Randomize keeps this colour. Click to unlock." : L"Lock this colour so Randomize keeps it.";
        Wd& dc = add(WT_DICE, L"dice" + std::to_wstring(i), RECT{x + S(30), gy + S(66), x + S(54), gy + S(90)});
        dc.slot = i; dc.enabled = !g.lock[i];
        dc.click = [i] { randomizeSlot(i); };
        dc.help = g.lock[i] ? L"This colour is locked - unlock it to re-roll." : L"Re-roll just this colour so it still fits the others.";
    }
    // random generator
    int rx = in.left + S(400);
    if (rx + S(230) > in.right) rx = in.right - S(230);
    std::vector<MenuItem> hm;
    for (int h = 0; h < HM_COUNT; h++) hm.push_back({L"", HarmonyName(h), h == g.harmony});
    option(L"harmony", RECT{rx, gy + S(4), in.right, gy + S(30)}, L"Harmony:", S(72), HarmonyName(g.harmony), hm,
           [](int k) { g.harmony = std::clamp(k, 0, HM_COUNT - 1); Changed(); }, std::wstring(L"Colour scheme for the random generator: ") + HarmonyHelp(g.harmony));
    bool anyFree = false;
    int freeCount = 0;
    for (int i = 0; i < n; i++) if (!g.lock[i]) { anyFree = true; freeCount++; }
    button(L"randomize", RECT{rx, gy + S(38), rx + S(150), gy + S(66)}, L"Randomize Palette", randomizePalette,
           anyFree ? L"Generate a new palette in the chosen harmony (Ctrl+R). Locked colours are kept." : L"Every colour is locked - unlock one to randomize.", anyFree);
    button(L"shuffle", RECT{rx + S(158), gy + S(38), in.right, gy + S(66)}, L"Shuffle", shufflePalette,
           freeCount >= 2 ? L"Swap the unlocked colours between their roles." : L"Needs at least two unlocked colours.", freeCount >= 2);
    // palette library
    int ly = gy + S(100);
    groupW(RECT{in.left, ly, in.right, ly + S(56)}, L"Palette Library");
    int by = ly + S(20), bh = S(28), bx = in.left + S(10);
    auto pals = allPalettes();
    std::vector<MenuItem> pm;
    for (auto& p : pals) pm.push_back({p.group, p.name, false});
    int wApply = S(150);
    option(L"pal.apply", RECT{bx, by, bx + wApply, by + bh}, L"", 0, L"Apply Palette", pm,
           [pals](int k) { if (k >= 0 && k < (int)pals.size()) applyPalette(pals[k].c, pals[k].name); },
           L"Apply a palette from the library: 90s Neon, SGI Classic, Fractal Glow, Text Friendly, Nature & Metal and your own.");
    bx += wApply + S(8);
    button(L"pal.save", RECT{bx, by, bx + S(98), by + bh}, L"Save...", savePaletteAs, L"Save the current colours to My Palettes.");
    bx += S(106);
    std::vector<MenuItem> um;
    for (auto& p : g.userPalettes) um.push_back({L"", p.name, false});
    option(L"pal.delete", RECT{bx, by, bx + S(98), by + bh}, L"", 0, L"Delete", um, [](int k) { deleteUserPalette(k); },
           g.userPalettes.empty() ? L"No saved palettes yet - use Save... first." : L"Delete one of your saved palettes.", !g.userPalettes.empty());
    bx += S(106);
    button(L"pal.import", RECT{bx, by, bx + S(86), by + bh}, L"Import...", importPalette, L"Import a GIMP .gpl palette (or any text file with #RRGGBB codes).");
    bx += S(94);
    button(L"pal.export", RECT{bx, by, std::min<int>(bx + S(86), in.right - S(10)), by + bh}, L"Export...", exportPalette, L"Export the current colours as a GIMP .gpl palette.");
    // colour cycling + order
    int cy = ly + S(66);
    bool sh = HasShading(g.sel);
    std::wstring why = L"Colour cycling applies to gradient-style patterns (Gradients and most of the originals).";
    int half = (in.right - in.left) / 2;
    slider(L"ramp.repeat", RECT{in.left, cy, in.left + half - S(8), cy + S(28)}, L"Ramp Repeat", S(96), (float)c.rampRepeat, 1, 8, 1, 0,
           [](float v) { Cur().rampRepeat = (int)v; }, sh ? L"Repeat the colour ramp across the pattern (1 to 8 times)." : why, sh);
    slider(L"ramp.offset", RECT{in.left + half + S(8), cy, in.right, cy + S(28)}, L"Ramp Offset", S(96), c.rampOffset, 0, 1, 0, 2,
           [](float v) { Cur().rampOffset = v; }, sh ? L"Shift the colours along the ramp - classic palette cycling." : why, sh);
    int oy = cy + S(36);
    check(L"ramp.mirror", RECT{in.left, oy, in.left + S(170), oy + S(26)}, L"Mirror Ramp", c.rampMirror != 0,
          [] { Edit(); Cur().rampMirror = !Cur().rampMirror; Changed(); }, sh ? L"Ping-pong repeated ramps instead of jumping back to the first colour." : why, sh);
    button(L"reverse", RECT{in.left + S(180), oy, in.left + S(320), oy + S(28)}, L"Reverse Order",
           [] {
               Edit();
               int k = usedSlots();
               std::reverse(Cur().colors, Cur().colors + k);
               Changed();
               ColorBrowserSync();
           },
           n >= 2 ? L"Reverse the order of the colours (flips light and dark roles)." : L"Solid backgrounds have one colour.", n >= 2);
    button(L"factory.colors", RECT{in.left + S(330), oy, in.left + S(470), oy + S(28)}, L"Factory Colors",
           [] {
               Edit();
               for (int i = 0; i < 4; i++) Cur().colors[i] = Patterns()[g.sel].def[i] & 0xffffff;
               Changed();
               ColorBrowserSync();
               setStatus(L"Factory colours restored.");
           },
           L"Restore this background's original colours (other settings stay).");
    (void)P;
}

static void buildPatternTab(RECT in) {
    const Pattern& P = Patterns()[g.sel];
    Config& c = Cur();
    int colW = (in.right - in.left) * 58 / 100;
    RECT lc{in.left, in.top, in.left + colW, in.bottom};
    int y = lc.top;
    int np = std::min<int>((int)P.params.size(), kMaxParams);
    int rowH = np > 5 ? S(28) : S(30);
    if (np == 0) {
        textW(L"noparams", RECT{lc.left, y, lc.right, y + S(60)},
              P.kind == K_HEADER ? L"" : L"This background has no shape controls - its look comes from the original artwork. Colours, the Transform tab and adjustments still apply.",
              F().small, C::text, DT_LEFT | DT_WORDBREAK);
    } else {
        for (int k = 0; k < np; k++) {
            const ParamDef& d = P.params[k];
            RECT rr{lc.left, y, lc.right, y + rowH - S(2)};
            int kk = k;
            if (!d.choices.empty()) {
                std::vector<MenuItem> items;
                int cur = std::clamp((int)std::lround(c.params[k]), 0, (int)d.choices.size() - 1);
                for (size_t i = 0; i < d.choices.size(); i++) items.push_back({L"", d.choices[i], (int)i == cur});
                option(L"param" + std::to_wstring(k), rr, d.name, S(118), d.choices[cur], items,
                       [kk](int i) { Edit(); Cur().params[kk] = (float)i; SanitizeConfig(Cur()); Changed(); }, L"Choose " + d.name + L".");
            } else {
                slider(L"param" + std::to_wstring(k), rr, d.name, S(118), c.params[k], d.min, d.max, d.def, d.decimals,
                       [kk](float v) { Cur().params[kk] = v; SanitizeConfig(Cur()); },
                       d.name + L": " + fmtValue(d.min, d.decimals) + L" to " + fmtValue(d.max, d.decimals) + L". Double-click the label to reset.");
            }
            y += rowH;
        }
    }
    // right column: shading, variation, reset
    int rx = in.left + colW + S(16);
    RECT rc{rx, in.top, in.right, in.bottom};
    int ry = rc.top;
    bool sh = HasShading(g.sel);
    std::wstring why = L"Shading applies to gradient-style patterns; this one uses fixed colour slots.";
    textW(L"shading.lbl", RECT{rc.left, ry, rc.right, ry + S(24)}, L"Shading:", F().body);
    ry += S(24);
    static const wchar_t* shNames[4] = {L"Smooth", L"Banded", L"Dither", L"IRIX"};
    static const wchar_t* shHelp[4] = {L"Smooth: continuous blend between the four colours.", L"Banded: hard-edged steps between the colours.",
                                       L"Dither: ordered (Bayer) dither using only the four colours - 8-bit workstation look.",
                                       L"IRIX: error-diffusion dither, like the original IRIX pixmaps."};
    int sw = std::max<int>(S(70), (rc.right - rc.left) / 2);
    for (int k = 0; k < 4; k++) {
        int cx = rc.left + (k % 2) * sw, cy = ry + (k / 2) * S(28);
        radio(L"shade" + std::to_wstring(k), RECT{cx, cy, cx + sw, cy + S(26)}, shNames[k], c.shading == k,
              [k] { Edit(); Cur().shading = k; Changed(); }, sh ? shHelp[k] : why, sh);
    }
    ry += 2 * S(28) + S(10);
    bool var = HasVariations(g.sel);
    std::wstring vwhy = L"This pattern is fully determined by its controls - there is no random seed to vary.";
    int bw = std::min<int>(S(126), (rc.right - rc.left - S(8)) / 2);
    button(L"variation", RECT{rc.left, ry, rc.left + bw, ry + S(28)}, L"New Variation",
           [] { Edit(); Cur().seed = NextRandom(); Changed(); setStatus(L"New variation, seed " + std::to_wstring(Cur().seed) + L"."); },
           var ? L"Roll a new random seed for this pattern (Ctrl+N)." : vwhy, var);
    button(L"setseed", RECT{rc.left + bw + S(8), ry, rc.left + 2 * bw + S(8), ry + S(28)}, L"Set Seed...",
           [] {
               std::wstring s = std::to_wstring(Cur().seed);
               if (!PromptText(L"Set Seed", L"Random seed (0 to 4294967295):", s)) return;
               wchar_t* end = nullptr;
               unsigned long long v = wcstoull(s.c_str(), &end, 10);
               if (end == s.c_str() || v > 0xffffffffull) { setStatus(L"Enter a whole number from 0 to 4294967295."); return; }
               Edit();
               Cur().seed = (uint32_t)v;
               setStatus(L"Seed set to " + std::to_wstring(Cur().seed) + L".");
           },
           var ? L"Type in a seed to recreate a variation exactly." : vwhy, var);
    ry += S(32);
    textW(L"seed.lbl", RECT{rc.left, ry, rc.right, ry + S(22)}, var ? L"Seed " + std::to_wstring(c.seed) : L"", F().small, C::text);
    ry += S(28);
    button(L"resetpattern", RECT{rc.left, ry, std::min<int>(rc.right, rc.left + S(200)), ry + S(28)}, L"Reset Pattern Controls",
           [] {
               Edit();
               Config d = DefaultConfig(g.sel);
               Config& cc = Cur();
               for (int i = 0; i < kMaxParams; i++) cc.params[i] = d.params[i];
               cc.shading = d.shading; cc.seed = d.seed;
               Changed();
               setStatus(L"Pattern controls reset (colours, transform and adjustments kept).");
           },
           L"Restore this pattern's own controls, shading and seed (colours, transform and adjustments stay).");
}

static void buildTransformTab(RECT in) {
    const Pattern& P = Patterns()[g.sel];
    Config& c = Cur();
    int colW = (in.right - in.left) * 52 / 100;
    int lx = in.left, lr = in.left + colW - S(8), y = in.top;
    bool gen = IsGenerated(g.sel);
    bool zoomOk = IsTiled(P.kind) && P.kind != K_SOLID;
    // Scale (generated tiles, redrawn) or Pixel Size (IRIX art, enlarged)
    textW(L"zoom.lbl", RECT{lx, y, lx + S(78), y + S(26)}, P.kind == K_DTILE ? L"Scale:" : L"Pixel Size:", F().body);
    int zw = std::max<int>(S(40), (lr - lx - S(78)) / 4);
    for (int z = 1; z <= 4; z++)
        radio(L"zoom" + std::to_wstring(z), RECT{lx + S(78) + (z - 1) * zw, y, lx + S(78) + z * zw, y + S(26)}, std::to_wstring(z) + L"x", zoomOk && c.zoom == z,
              [z] { Edit(); Cur().zoom = z; Changed(); },
              !zoomOk ? std::wstring(L"Scale applies to tiled patterns; this one is drawn for each display.")
              : P.kind == K_DTILE ? L"Draw the pattern N times larger - it is redrawn at that size, so edges stay smooth. " + P.name + L" starts at " +
                                        std::to_wstring(DefaultZoom(g.sel)) + L"x on this display."
                                  : L"Show every pixel of the original IRIX artwork as an N x N block. " + P.name + L" starts at " +
                                        std::to_wstring(DefaultZoom(g.sel)) + L"x on this display.",
              zoomOk);
    y += S(32);
    bool tileOk = P.usesTile;
    static const int sizes[] = {0, 64, 128, 192, 256, 384, 512};
    std::vector<MenuItem> tm;
    int curTs = 0;
    for (int k = 0; k < 7; k++) {
        std::wstring lbl = sizes[k] ? std::to_wstring(sizes[k]) + L" px" : (P.defTile > 0 ? L"Default (" + std::to_wstring(P.defTile) + L" px)" : std::wstring(L"Default"));
        tm.push_back({L"", lbl, c.tileSize == sizes[k]});
        if (c.tileSize == sizes[k]) curTs = k;
    }
    option(L"tilesize", RECT{lx, y, lr, y + S(28)}, L"Tile Size:", S(78), tm[curTs].label, tm,
           [](int k) { static const int sz[] = {0, 64, 128, 192, 256, 384, 512}; Edit(); Cur().tileSize = sz[std::clamp(k, 0, 6)]; SanitizeConfig(Cur()); Changed(); },
           tileOk ? L"Size of the repeating tile - bigger tiles hold more detail and repeat less often." : L"Only generated tiles have a variable tile size.", tileOk);
    y += S(34);
    textW(L"smooth.lbl", RECT{lx, y, lx + S(78), y + S(26)}, L"Smoothing:", F().body);
    static const wchar_t* smNames[3] = {L"Off", L"Normal", L"High"};
    static const wchar_t* smHelp[3] = {L"Smoothing off: one sample per pixel - crisp, hard-edged pixels.",
                                       L"Normal: edges and fine lines are supersampled - clean, with no jagged steps.",
                                       L"High: more samples per pixel for the finest detail (slower to draw)."};
    int smw = std::max<int>(S(50), (lr - lx - S(78)) / 3);
    for (int k = 0; k < 3; k++)
        radio(L"smooth" + std::to_wstring(k), RECT{lx + S(78) + k * smw, y, lx + S(78) + (k + 1) * smw, y + S(26)}, smNames[k], gen && c.smoothing == k,
              [k] { Edit(); Cur().smoothing = k; Changed(); }, gen ? smHelp[k] : L"Smoothing applies to generated patterns; IRIX artwork is drawn exactly as it was.", gen);
    y += S(32);
    bool fitOk = P.kind == K_FULL;
    textW(L"fit.lbl", RECT{lx, y, lx + S(78), y + S(26)}, L"Fit:", F().body);
    static const wchar_t* fitNames[3] = {L"Center", L"Scale", L"Stretch"};
    int fw = std::max<int>(S(50), (lr - lx - S(78)) / 3);
    for (int k = 0; k < 3; k++)
        radio(L"fit" + std::to_wstring(k), RECT{lx + S(78) + k * fw, y, lx + S(78) + (k + 1) * fw, y + S(26)}, fitNames[k],
              fitOk && c.fit == k, [k] { Edit(); Cur().fit = k; Changed(); },
              fitOk ? L"How the full-screen IRIX backdrop fits each display (Scale keeps the bar in its corner)." : L"Fit applies to the full-screen IRIX backdrops (Citrus Citrus, Reef, Sky (HDTV) ...).", fitOk);
    y += S(32);
    bool shOk = HasShading(g.sel);
    slider(L"balance", RECT{lx, y, lr, y + S(28)}, L"Balance", S(78), c.balance, -100, 100, 0, 0, [](float v) { Cur().balance = v; },
           shOk ? L"Shift a gradient-style pattern towards its low (negative) or high (positive) colours. Double-click the label to reset."
                : L"Balance applies to gradient-style patterns; this one uses fixed colour slots.", shOk);
    y += S(32);
    check(L"invert", RECT{lx, y, lr, y + S(26)}, L"Invert Colours", shOk && c.invert, [] { Edit(); Cur().invert = !Cur().invert; Changed(); },
          shOk ? L"Run the colours the other way: the high stop becomes the low one." : L"Invert applies to gradient-style patterns.", shOk);
    // right column: orientation
    int rx = in.left + colW + S(8), rr = in.right;
    int ry = in.top;
    bool orOk = P.kind != K_SOLID;
    textW(L"rot.lbl", RECT{rx, ry, rx + S(64), ry + S(26)}, L"Rotate:", F().body);
    static const wchar_t* rotNames[4] = {L"0\x00B0", L"90\x00B0", L"180\x00B0", L"270\x00B0"};
    int rw = std::max<int>(S(46), (rr - rx - S(64)) / 4);
    for (int k = 0; k < 4; k++)
        radio(L"rot" + std::to_wstring(k), RECT{rx + S(64) + k * rw, ry, rx + S(64) + (k + 1) * rw, ry + S(26)}, rotNames[k], orOk && c.rotate == k,
              [k] { Edit(); Cur().rotate = k; Changed(); }, orOk ? L"Turn the pattern a quarter turn at a time (clockwise)." : L"A solid colour has nothing to turn.", orOk);
    ry += S(32);
    int hw = (rr - rx) / 2;
    check(L"mirrorx", RECT{rx, ry, rx + hw, ry + S(26)}, L"Mirror Left-Right", orOk && c.mirrorX, [] { Edit(); Cur().mirrorX = !Cur().mirrorX; Changed(); },
          L"Flip the pattern horizontally.", orOk);
    check(L"mirrory", RECT{rx + hw, ry, rr, ry + S(26)}, L"Mirror Top-Bottom", orOk && c.mirrorY, [] { Edit(); Cur().mirrorY = !Cur().mirrorY; Changed(); },
          L"Flip the pattern vertically.", orOk);
    ry += S(34);
    bool offOk = zoomOk;
    std::wstring offWhy = L"Shifting applies to tiled patterns - it moves the tile under your icons.";
    slider(L"offx", RECT{rx, ry, rr, ry + S(28)}, L"Shift Across", S(96), std::round(c.offsetX * 100), 0, 100, 0, 0, [](float v) { Cur().offsetX = v / 100.0f; },
           offOk ? L"Slide the tile sideways (percent of a tile), e.g. to move a busy spot from under your icons." : offWhy, offOk);
    ry += S(32);
    slider(L"offy", RECT{rx, ry, rr, ry + S(28)}, L"Shift Down", S(96), std::round(c.offsetY * 100), 0, 100, 0, 0, [](float v) { Cur().offsetY = v / 100.0f; },
           offOk ? L"Slide the tile downwards (percent of a tile)." : offWhy, offOk);
    ry += S(38);
    button(L"resettransform", RECT{rx, ry, std::min<int>(rr, rx + S(170)), ry + S(28)}, L"Reset Transform",
           [] {
               Edit();
               Config d = DefaultConfig(g.sel);
               Config& cc = Cur();
               cc.zoom = d.zoom; cc.tileSize = d.tileSize; cc.fit = d.fit; cc.smoothing = d.smoothing; cc.rotate = 0; cc.mirrorX = cc.mirrorY = 0;
               cc.offsetX = cc.offsetY = 0; cc.balance = 0; cc.invert = 0;
               Changed();
               setStatus(L"Transform reset (pattern controls, colours and adjustments kept).");
           },
           L"Back to this pattern's own scale, tile size, smoothing and orientation.");
}

static PreviewResult g_prev;
static bool g_prevValid = false;

static void buildAdjustTab(RECT in) {
    Config& c = Cur();
    int colW = (in.right - in.left) * 55 / 100;
    int x0 = in.left, x1 = in.left + colW, y = in.top;
    struct A { const wchar_t* key; const wchar_t* name; float* v; float mn, mx; const wchar_t* help; };
    A rows[6] = {{L"adj.bright", L"Brightness", &c.adj.brightness, -100, 100, L"Lighten or darken the whole background."},
                 {L"adj.contrast", L"Contrast", &c.adj.contrast, -100, 100, L"Flatten (negative) or punch up (positive) the tonal range."},
                 {L"adj.sat", L"Saturation", &c.adj.saturation, -100, 100, L"From greyscale (-100) to vivid (+100)."},
                 {L"adj.hue", L"Hue Shift", &c.adj.hue, -180, 180, L"Rotate every hue around the colour wheel."},
                 {L"adj.soften", L"Soften", &c.adj.soften, 0, 100, L"Blur away harsh pixel detail."},
                 {L"adj.calm", L"Calm", &c.adj.calm, 0, 100, L"Keep the broad colour layout but flatten fine detail - the main thing that makes icon labels hard to read."}};
    for (int k = 0; k < 6; k++) {
        float* p = rows[k].v;
        size_t off = (char*)p - (char*)&c;   // re-resolve against Cur() at call time
        slider(rows[k].key, RECT{x0, y, x1 - S(10), y + S(28)}, rows[k].name, S(86), *p, rows[k].mn, rows[k].mx, 0, 0,
               [off](float v) { *(float*)((char*)&Cur() + off) = v; }, std::wstring(rows[k].help) + L" Double-click the label to reset.");
        y += S(31);
    }
    // labels
    int rx = x1 + S(6);
    groupW(RECT{rx, in.top - S(4), in.right, in.bottom}, L"Icon Labels");
    int ry = in.top + S(18), rl = rx + S(12), rr = in.right - S(12);
    static const wchar_t* tones[3] = {L"Off", L"Light", L"Dark"};
    static const wchar_t* toneHelp[3] = {L"No label protection.", L"Protect light (white) labels - the Windows default: keeps the background from getting too bright.",
                                         L"Protect dark labels: keeps the background from getting too dark."};
    int tw = (rr - rl) / 3;
    for (int k = 0; k < 3; k++)
        radio(L"tone" + std::to_wstring(k), RECT{rl + k * tw, ry, rl + (k + 1) * tw, ry + S(26)}, tones[k], c.adj.textTone == k,
              [k] { Edit(); Cur().adj.textTone = k; Changed(); }, toneHelp[k]);
    ry += S(30);
    bool toneOn = c.adj.textTone != TT_OFF;
    slider(L"adj.strength", RECT{rl, ry, rr, ry + S(28)}, L"Strength", S(70), c.adj.textStrength, 0, 100, 60, 0,
           [](float v) { Cur().adj.textStrength = v; }, toneOn ? L"How firmly the label colour is protected." : L"Choose Light or Dark labels first.", toneOn);
    ry += S(34);
    std::wstring rd = L"Measuring...";
    if (g_prevValid) {
        wchar_t b[96];
        swprintf(b, 96, L"%ls - %.1f:1 contrast, %ls detail", ReadabilityWord(g_prev.read.score), g_prev.read.ratio,
                 g_prev.read.busy < 0.25f ? L"calm" : (g_prev.read.busy < 0.55f ? L"moderate" : L"busy"));
        rd = b;
    }
    textW(L"adj.readout", RECT{rl, ry, rr, ry + S(22)}, L"Labels: " + rd, F().smallBold, C::text);
    ry += S(28);
    button(L"textfriendly", RECT{rl, ry, rl + S(160), ry + S(28)}, L"Make Text-Friendly",
           [] {
               Edit();
               int tone = Cur().adj.textTone == TT_DARK ? TT_DARK : TT_LIGHT;
               int score = MakeTextFriendly(Cur(), tone);
               setStatus(std::wstring(L"Icon labels now read: ") + ReadabilityWord(score) + L" (calm and label protection adjusted).");
           },
           L"Find the gentlest Calm and label settings that make desktop icon text read Good.");
    button(L"resetadjust", RECT{rl + S(168), ry, std::min<int>(rr, rl + S(300)), ry + S(28)}, L"Reset Adjust",
           [] { Edit(); Cur().adj = Adjust(); Changed(); setStatus(L"Adjustments reset."); }, L"Clear every adjustment on this tab.");
}

static void buildPresetsTab(RECT in) {
    int listW = std::max<int>(S(220), (in.right - in.left) * 42 / 100);
    RECT lr{in.left, in.top, in.left + listW, in.bottom};
    g_prl.layout(lr, S(34), (int)g.presets.size());
    bool sel = g.presetSel >= 0 && g.presetSel < (int)g.presets.size();
    int bx = lr.right + S(10), bw = S(118), by = in.top, bh = S(26), gap = S(4);
    std::wstring nosel = L"Select a preset in the list first.";
    button(L"pre.save", RECT{bx, by, bx + bw, by + bh}, L"Save New...", savePresetNew, L"Save everything about the current background as a preset (Ctrl+S).");
    by += bh + gap;
    button(L"pre.update", RECT{bx, by, bx + bw, by + bh}, L"Update", updatePreset, sel ? L"Overwrite the selected preset with the current settings." : nosel, sel);
    by += bh + gap;
    button(L"pre.load", RECT{bx, by, bx + bw, by + bh}, L"Load", [] { loadPreset(g.presetSel); }, sel ? L"Restore the selected preset exactly (or double-click it)." : nosel, sel);
    by += bh + gap;
    button(L"pre.rename", RECT{bx, by, bx + bw, by + bh}, L"Rename...", renamePreset, sel ? L"Rename the selected preset." : nosel, sel);
    by += bh + gap;
    button(L"pre.delete", RECT{bx, by, bx + bw, by + bh}, L"Delete", deletePreset, sel ? L"Delete the selected preset." : nosel, sel);
    by += bh + gap;
    button(L"pre.import", RECT{bx, by, bx + bw, by + bh}, L"Import...", importPreset, L"Add a .irixbg preset file to the library and load it.");
    by += bh + gap;
    button(L"pre.export", RECT{bx, by, bx + bw, by + bh}, L"Export...", exportPreset,
           sel ? L"Save the selected preset as a .irixbg file to share or back up." : L"Save the current settings as a .irixbg file (select a preset to export that instead).");
    // export image + display
    int ex = bx + bw + S(12);
    if (ex + S(160) > in.right) return;
    groupW(RECT{ex, in.top - S(4), in.right, in.top + S(104)}, L"Export Image");
    auto sizes = exportSizes();
    g_expSel = std::clamp(g_expSel, 0, (int)sizes.size() - 1);
    std::vector<MenuItem> sm;
    for (int k = 0; k < (int)sizes.size(); k++) sm.push_back({L"", sizes[k].label, k == g_expSel});
    int el = ex + S(10), er = in.right - S(10);
    option(L"exp.size", RECT{el, in.top + S(16), er, in.top + S(44)}, L"", 0, sizes[g_expSel].label, sm,
           [](int k) {
               auto sz = exportSizes();
               if (k < 0 || k >= (int)sz.size()) return;
               if (sz[k].code == 4 && !pickCustomSize()) return;
               g_expSel = k;
               Changed();
           },
           L"Size of the exported image: your desktop, one display, one seamless tile, standard sizes or custom.");
    bool tiled = IsTiled(Patterns()[g.sel].kind);
    bool expOk = !(sizes[g_expSel].code == 2 && !tiled) && !g.busy;
    button(L"exp.go", RECT{el, in.top + S(52), er, in.top + S(80)}, L"Export Image...", exportImage,
           g.busy ? L"Busy - wait for the current task to finish." : (expOk ? L"Render and save as PNG or BMP." : L"This background has no repeating tile - choose another size."), expOk);
    groupW(RECT{ex, in.top + S(112), in.right, in.bottom}, L"Desktop");
    check(L"watch", RECT{el, in.top + S(130), er, in.top + S(156)}, L"Keep Synced to Displays", g.watch,
          [] {
              SetWatch(!g.watch);
              setStatus(g.watch ? L"Will re-apply at sign-in and when displays change (uses your last Apply)." : L"Automatic re-sync turned off.");
          },
          L"Re-apply at sign-in and whenever displays are added, removed, moved or change resolution.");
    button(L"openfolder", RECT{el, in.top + S(162), er, in.top + S(188)}, L"Open Settings Folder",
           [] { if (!g_selftest) ShellExecuteW(g.main, L"open", AppDataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); setStatus(L"Settings, presets and palettes live in " + AppDataDir()); },
           L"Open %APPDATA%\\IrixBackground (presets, palettes, rendered wallpaper).");
}

static void buildUI(int W, int H) {
    g_w.clear();
    L.f = FrameLayout(W, H, true);
    RECT b = L.f.body;
    L.msg = RECT{b.left + S(4), b.bottom - S(4) - S(22), b.right - S(4), b.bottom - S(4)};
    int yb = L.msg.top - S(8) - S(30);
    L.icon = RECT{b.left + S(8), b.top + S(8), b.left + S(8) + S(54), b.top + S(8) + S(54)};
    RECT thb{b.right - S(10) - S(150), L.icon.top + S(28), b.right - S(10), L.icon.top + S(28) + S(28)};
    L.title = RECT{L.icon.right + S(14), L.icon.top + S(6), b.right - S(230), L.icon.top + S(30)};
    L.sub = RECT{L.icon.right + S(14), L.icon.top + S(30), thb.left - S(12), L.icon.bottom};
    button(L"theme", thb, L"Theme Options...", OpenThemeOptions,
           std::wstring(L"Recolour this program with an IRIX desktop scheme (Ctrl+T). Now: ") + Scheme(CurrentScheme()).name + L".");
    textW(L"version", RECT{b.right - S(220), L.icon.top + S(2), b.right - S(10), L.icon.top + S(24)}, L"Version " APP_VERSION, F().small,
          C::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE)
        .help = APP_NAME L" " APP_VERSION L" (built " __DATE__ L"). About: Help (F1) or the window menu button at the top left.";
    int y0 = L.icon.bottom + S(10);
    int lw = S(262), lx = b.left + S(6);
    // left column: filter, find, list
    const auto& P = Patterns();
    std::vector<MenuItem> cm;
    std::vector<int> cats;
    auto countIn = [&](int cat) { int n = 0; for (auto& p : P) if (p.kind != K_HEADER && (cat == 0 || (cat > 0 && p.category == cat - 1) || (cat == -1 && p.textFriendly))) n++; return n; };
    cm.push_back({L"", L"All Backgrounds (" + std::to_wstring(countIn(0)) + L")", g.category == 0});
    cats.push_back(0);
    for (int i = 0; i < (int)P.size(); i++)
        if (P[i].kind == K_HEADER) { cm.push_back({L"", P[i].name + L" (" + std::to_wstring(countIn(i + 1)) + L")", g.category == i + 1}); cats.push_back(i + 1); }
    cm.push_back({L"", L"Text-Friendly Picks (" + std::to_wstring(countIn(-1)) + L")", g.category == -1});
    cats.push_back(-1);
    option(L"category", RECT{lx, y0, lx + lw, y0 + S(28)}, L"Show:", S(46), categoryLabel(), cm,
           [cats](int k) {
               if (k < 0 || k >= (int)cats.size()) return;
               g.category = cats[k];
               computeRows();
               g_pl.top = 0;
               g_pl.ensure(rowOfPattern(g.sel));
               Changed();
           },
           L"Filter the list by collection.");
    L.searchLbl = RECT{lx, y0 + S(34), lx + S(46), y0 + S(34) + S(26)};
    L.search = RECT{lx + S(46) + 2, y0 + S(34) + 2, lx + lw - 2, y0 + S(34) + S(26) - 2};
    textW(L"find.lbl", L.searchLbl, L"Find:", F().body);
    RECT list{lx, L.searchLbl.bottom + S(8), lx + lw, yb - S(10)};
    g_pl.layout(list, S(38), (int)g_rows.size());
    // right side
    int rx = lx + lw + S(14), rr = b.right - S(8);
    int panelH = S(266);
    L.panel = RECT{rx, yb - S(10) - panelH, rr, yb - S(10)};
    L.tabs = RECT{rx, L.panel.top - S(28), rr, L.panel.top};
    RECT under{rx, L.tabs.top - S(34), rr, L.tabs.top - S(8)};
    L.preview = RECT{rx, y0, rr, under.top - S(6)};
    L.pvIn = RECT{L.preview.left + 2, L.preview.top + 2, L.preview.right - 2, L.preview.bottom - 2};
    add(WT_TEXT, L"preview", L.preview).help = IsTiled(P[g.sel].kind)
        ? L"Preview is locked to the desktop: these tiles sit exactly where they will be drawn."
        : L"Miniature of the display this window is on.";
    check(L"icons", RECT{rx, under.top, rx + S(210), under.bottom}, L"Show Desktop Icons", g.showIcons, [] { g.showIcons = !g.showIcons; Changed(); },
          L"Overlay sample desktop icons and labels on the preview to judge legibility.");
    std::wstring rd = L"Labels: measuring...";
    COLORREF rc = C::text;
    if (g_prevValid) {
        wchar_t bb[128];
        swprintf(bb, 128, L"Labels: %ls  (%.1f:1, %ls)", ReadabilityWord(g_prev.read.score), g_prev.read.ratio,
                 g_prev.read.busy < 0.25f ? L"calm" : (g_prev.read.busy < 0.55f ? L"moderate detail" : L"busy"));
        rd = bb;
        rc = StatusColor(g_prev.read.score);
    }
    textW(L"readability", RECT{rx + S(216), under.top, rr, under.bottom}, rd, F().smallBold, rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE).help =
        L"How readable white desktop-icon labels are over this background (contrast against its brightest areas, and how busy it is).";
    // tabs
    int tw = std::min(S(150), (rr - rx) / TAB_COUNT);
    for (int t = 0; t < TAB_COUNT; t++) {
        Wd& w = add(WT_TAB, L"tab" + std::to_wstring(t), RECT{rx + t * tw, L.tabs.top, rx + (t + 1) * tw, L.tabs.bottom});
        w.label = kTabNames[t]; w.checked = g.tab == t;
        w.click = [t] { g.tab = t; Changed(); };
        static const wchar_t* th[TAB_COUNT] = {L"Colours, random palettes and the palette library.", L"This pattern's own controls, shading and variation.",
                                               L"Scale, tile size, smoothing, fit, balance, rotation, mirroring and shifting.",
                                               L"Colour adjustments and icon-label readability.", L"Save, restore, import and export presets; export images."};
        w.help = th[t];
    }
    L.inner = RECT{L.panel.left + S(12), L.panel.top + S(14), L.panel.right - S(12), L.panel.bottom - S(10)};
    switch (g.tab) {
    case TAB_COLORS: buildColorsTab(L.inner); break;
    case TAB_PATTERN: buildPatternTab(L.inner); break;
    case TAB_TRANSFORM: buildTransformTab(L.inner); break;
    case TAB_ADJUST: buildAdjustTab(L.inner); break;
    default: buildPresetsTab(L.inner); break;
    }
    // bottom row
    button(L"undo", RECT{lx, yb, lx + S(86), yb + S(30)}, L"Undo", [] { if (!Undo()) setStatus(L"Nothing to undo."); }, L"Undo the last change (Ctrl+Z).", !g.undo.empty());
    button(L"redo", RECT{lx + S(94), yb, lx + S(180), yb + S(30)}, L"Redo", [] { if (!Redo()) setStatus(L"Nothing to redo."); }, L"Redo (Ctrl+Y).", !g.redo.empty());
    static const wchar_t* names[4] = {L"Apply", L"Reset ...", L"Close", L"Help"};
    static const wchar_t* keys[4] = {L"apply", L"reset", L"close", L"help"};
    static const wchar_t* helps[4] = {L"Apply the above settings to your background.", L"Revert to factory settings or to the applied background.",
                                      L"Close this window (your settings are kept).", L"Show more information about this window."};
    std::function<void()> fns[4] = {doApply, doReset, [] { PostMessageW(g.main, WM_CLOSE, 0, 0); }, showHelp};
    for (int i = 0; i < 4; i++) {
        int r = rr - (3 - i) * S(98);
        button(keys[i], RECT{r - S(88), yb, r, yb + S(30)}, (i == 0 && g.busy && g_taskKind == TASK_APPLY) ? L"Applying" : names[i], fns[i], helps[i],
               !(i == 0 && g.busy));
    }
}

// ---------------------------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------------------------
static void blitImage(const Image& img, RECT dst) {
    GdiFlush();
    int w = std::min<int>(img.w, dst.right - dst.left), h = std::min<int>(img.h, dst.bottom - dst.top);
    for (int y = 0; y < h; y++) {
        int Y = dst.top + y;
        if (Y < 0 || Y >= g_bb.h) continue;
        for (int x = 0; x < w; x++) {
            int X = dst.left + x;
            if (X < 0 || X >= g_bb.w) continue;
            g_bb.bits[(size_t)Y * g_bb.w + X] = img.px[(size_t)y * img.w + x];
        }
    }
}
static void blitClipped(const Image& img, RECT dst, RECT clip) {
    GdiFlush();
    for (int y = std::max<int>(dst.top, clip.top); y < std::min<int>(dst.bottom, clip.bottom); y++) {
        int sy = y - dst.top;
        if (y < 0 || y >= g_bb.h || sy >= img.h) continue;
        for (int x = std::max<int>(dst.left, clip.left); x < std::min<int>(dst.right, clip.right); x++) {
            int sx = x - dst.left;
            if (x < 0 || x >= g_bb.w || sx >= img.w) continue;
            g_bb.bits[(size_t)y * g_bb.w + x] = img.px[(size_t)sy * img.w + sx];
        }
    }
}
static void blitScaledNearest(const Image& img, RECT dst) {
    GdiFlush();
    int W = dst.right - dst.left, H = dst.bottom - dst.top;
    if (W <= 0 || H <= 0 || img.w <= 0 || img.h <= 0) return;
    for (int y = 0; y < H; y++) {
        int Y = dst.top + y;
        if (Y < 0 || Y >= g_bb.h) continue;
        int sy = std::min(img.h - 1, y * img.h / H);
        for (int x = 0; x < W; x++) {
            int X = dst.left + x;
            if (X < 0 || X >= g_bb.w) continue;
            g_bb.bits[(size_t)Y * g_bb.w + X] = img.px[(size_t)sy * img.w + std::min(img.w - 1, x * img.w / W)];
        }
    }
}
static void previewSizeFor(HWND h, int& mw, int& mh) {
    int w = L.pvIn.right - L.pvIn.left, hh = L.pvIn.bottom - L.pvIn.top;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    int dw = mi.rcMonitor.right - mi.rcMonitor.left, dh = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (dw <= 0 || dh <= 0) { dw = 16; dh = 9; }
    double sc = std::max(0.0, std::min((double)(w - S(16)) / dw, (double)(hh - S(16)) / dh));
    mw = std::max(8, (int)(dw * sc));
    mh = std::max(8, (int)(dh * sc));
}
static void drawIcons(HDC dc, RECT area) {
    // three sample desktop icons with Windows-style labels
    static HFONT lf = nullptr;
    static int lfDpi = 0;
    if (!lf || lfDpi != g_dpi) {
        if (lf) DeleteObject(lf);
        lf = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        lfDpi = g_dpi;
    }
    bool dark = Cur().adj.textTone == TT_DARK;
    static const wchar_t* names[3] = {L"This PC", L"Projects", L"Recycle Bin"};
    for (int i = 0; i < 3; i++) {
        int x = area.left + S(10), y = area.top + S(10) + i * S(78);
        if (y + S(70) > area.bottom) break;
        RECT ic{x + S(18), y, x + S(50), y + S(32)};
        if (i == 0) {
            Fill(dc, RECT{ic.left, ic.top + S(3), ic.right, ic.bottom - S(8)}, RGB(0x30, 0x30, 0x40));
            Fill(dc, RECT{ic.left + S(2), ic.top + S(5), ic.right - S(2), ic.bottom - S(10)}, RGB(0x4A, 0x9F, 0xE0));
            Fill(dc, RECT{ic.left + S(10), ic.bottom - S(8), ic.right - S(10), ic.bottom - S(4)}, RGB(0x50, 0x50, 0x58));
            Fill(dc, RECT{ic.left + S(6), ic.bottom - S(4), ic.right - S(6), ic.bottom - S(2)}, RGB(0x50, 0x50, 0x58));
        } else if (i == 1) {
            Fill(dc, RECT{ic.left, ic.top + S(4), ic.left + S(14), ic.top + S(9)}, RGB(0xD8, 0xA2, 0x2E));
            Fill(dc, RECT{ic.left, ic.top + S(8), ic.right, ic.bottom - S(2)}, RGB(0xF4, 0xC4, 0x4E));
            HLine(dc, ic.left, ic.right, ic.top + S(8), RGB(0xB0, 0x80, 0x20));
        } else {
            Fill(dc, RECT{ic.left + S(6), ic.top + S(6), ic.right - S(6), ic.bottom - S(1)}, RGB(0xC8, 0xD0, 0xD8));
            Fill(dc, RECT{ic.left + S(4), ic.top + S(3), ic.right - S(4), ic.top + S(6)}, RGB(0x90, 0x98, 0xA0));
            for (int k = 0; k < 3; k++) VLine(dc, ic.left + S(11) + k * S(5), ic.top + S(9), ic.bottom - S(4), RGB(0x80, 0x88, 0x90));
        }
        RECT t{x - S(8), y + S(36), x + S(76), y + S(70)};
        COLORREF fg = dark ? RGB(0x10, 0x10, 0x10) : RGB(0xFF, 0xFF, 0xFF), sh = dark ? RGB(0xF0, 0xF0, 0xF0) : RGB(0, 0, 0);
        RECT s1 = t;
        OffsetRect(&s1, 1, 1);
        Text(dc, lf, names[i], s1, sh, DT_CENTER | DT_TOP | DT_WORDBREAK);
        Text(dc, lf, names[i], t, fg, DT_CENTER | DT_TOP | DT_WORDBREAK);
    }
}
static void drawPreview(HWND h) {
    HDC dc = g_bb.dc;
    Bevel(dc, L.preview, 1, C::light, C::dark);
    Bevel(dc, RECT{L.preview.left + 1, L.preview.top + 1, L.preview.right - 1, L.preview.bottom - 1}, 1, C::dark, C::light);
    const Config& c = Cur();
    bool tiled = IsTiled(Patterns()[g.sel].kind);
    int mw = 0, mh = 0;
    if (!tiled) previewSizeFor(h, mw, mh);
    std::wstring key = PreviewKey(c, mw, mh);
    PreviewResult res;
    bool have = LatestPreview(res);
    if (!have || res.key != key) RequestPreview(c, mw, mh);
    if (have) { g_prev = res; g_prevValid = true; }
    Fill(dc, L.pvIn, C::drawing);   // an XmDrawingArea: the scheme's DrawingAreaBackground
    RECT drawn = L.pvIn;
    RECT pv = L.pvIn, bbr{0, 0, g_bb.w, g_bb.h};
    IntersectRect(&pv, &pv, &bbr);   // never draw outside the back buffer
    if (have && res.tiled && res.tile && !IsRectEmpty(&pv)) {
        // locked to the desktop: the preview's screen position is the tiling phase
        POINT o{pv.left, pv.top};
        ClientToScreen(h, &o);
        GdiFlush();
        RenderRegionFrom(res.cfg, res.tile.get(), g_bb.bits + (size_t)pv.top * g_bb.w + pv.left, g_bb.w, o.x, o.y, pv.right - pv.left, pv.bottom - pv.top);
    } else if (have && res.image) {
        int w = L.pvIn.right - L.pvIn.left, hh = L.pvIn.bottom - L.pvIn.top;
        int iw = mw ? mw : res.image->w, ih = mh ? mh : res.image->h;
        RECT d{L.pvIn.left + (w - iw) / 2, L.pvIn.top + (hh - ih) / 2, 0, 0};
        d.right = d.left + iw; d.bottom = d.top + ih;
        Bevel(dc, RECT{d.left - 1, d.top - 1, d.right + 1, d.bottom + 1}, 1, RGB(0x1A, 0x1A, 0x1A), RGB(0x1A, 0x1A, 0x1A));
        if (res.image->w == iw && res.image->h == ih) blitImage(*res.image, d); else blitScaledNearest(*res.image, d);
        drawn = d;
    }
    if (have && !res.error.empty() && res.key == key) {
        RECT t = L.pvIn;
        InflateRect(&t, -S(16), -S(16));
        Text(dc, F().body, L"This background could not be rendered.\n\n" + res.error + L"\n\nA report was saved to " + CrashLogPath(), t,
             Readable(RGB(0xFF, 0xFF, 0xFF), C::drawing, 4.5f), DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    } else if (g.showIcons) drawIcons(dc, drawn);
    if (PreviewPending()) {
        RECT t{L.pvIn.right - S(96), L.pvIn.bottom - S(22), L.pvIn.right - S(4), L.pvIn.bottom - S(4)};
        Fill(dc, t, RGB(0x20, 0x20, 0x28));
        Text(dc, F().small, L"Rendering...", t, RGB(0xE0, 0xE0, 0xE0), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}
static void drawScroll(HDC dc, const ListView& lv, const std::wstring& prefix) {
    Fill(dc, lv.sb, C::trough);
    Bevel(dc, lv.sb, 1, C::dark, C::light);
    Arrow(dc, lv.up, true, g_pressKey == prefix + L".up" && g_hotKey == prefix + L".up");
    Arrow(dc, lv.dn, false, g_pressKey == prefix + L".dn" && g_hotKey == prefix + L".dn");
    Fill(dc, lv.thumb, C::sb);
    Bevel(dc, lv.thumb, 2, C::sbLight, C::sbDark);
}
static void drawPatternList() {
    HDC dc = g_bb.dc;
    const auto& P = Patterns();
    Sunken(dc, g_pl.r, 2);
    Fill(dc, RECT{g_pl.r.left + 2, g_pl.r.top + 2, g_pl.r.right - 2, g_pl.r.bottom - 2}, C::list);
    int tw = S(56), th = S(32);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, g_pl.items.left, g_pl.items.top, g_pl.items.right, g_pl.items.bottom);
    if (g_rows.empty()) Text(dc, F().small, L"Nothing matches - clear Find or choose All Backgrounds.", g_pl.items, C::text, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    for (int i = g_pl.top; i < (int)g_rows.size() && i < g_pl.top + g_pl.vis + 1; i++) {
        int pi = g_rows[i];
        RECT r{g_pl.items.left, g_pl.items.top + (i - g_pl.top) * g_pl.rowH, g_pl.items.right, g_pl.items.top + (i - g_pl.top + 1) * g_pl.rowH};
        if (P[pi].kind == K_HEADER) {
            RECT t{r.left + S(6), r.top + S(10), r.right - S(4), r.bottom};
            SelectObject(dc, F().bold);
            SIZE sz;
            GetTextExtentPoint32W(dc, P[pi].name.c_str(), (int)P[pi].name.size(), &sz);
            Text(dc, F().bold, P[pi].name, t, C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            int ly = (t.top + t.bottom) / 2, lx = t.left + sz.cx + S(6);
            if (lx < r.right - S(6)) { HLine(dc, lx, r.right - S(6), ly, C::listDark); HLine(dc, lx, r.right - S(6), ly + 1, C::listLight); }
            continue;
        }
        bool selRow = pi == g.sel;
        if (selRow) {
            RECT s{r.left + S(2), r.top + 1, r.right - S(2), r.bottom - 1};
            Fill(dc, s, C::listSelBg);   // Motif reverse-video selection
        }
        RECT tr{r.left + S(6), r.top + (g_pl.rowH - th) / 2, r.left + S(6) + tw, r.top + (g_pl.rowH - th) / 2 + th};
        const Config& cfg = g.cfg[pi];
        std::wstring k = ThumbKey(cfg, tw, th);
        auto img = GetThumb(k);
        if (img) blitClipped(*img, tr, g_pl.items);
        else {
            Fill(dc, tr, RGB(0x9A, 0x9A, 0x9A));
            RequestThumb(k, cfg, tw, th);
        }
        Bevel(dc, RECT{tr.left - 1, tr.top - 1, tr.right + 1, tr.bottom + 1}, 1, RGB(0x40, 0x40, 0x40), RGB(0x40, 0x40, 0x40));
        RECT t{tr.right + S(8), r.top, r.right - S(4), r.bottom};
        Text(dc, F().list, P[pi].name, t, selRow ? C::listSelFg : C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (P[pi].textFriendly) {
            RECT tf{r.right - S(22), r.top + S(4), r.right - S(6), r.top + S(16)};
            Text(dc, F().small, L"Aa", tf, selRow ? C::listSelFg : Readable(Brightness(C::list) > 0.5f ? RGB(0x10, 0x60, 0x10) : RGB(0x9C, 0xF0, 0x9C), C::list, 3.0f), DT_RIGHT | DT_TOP | DT_SINGLELINE);
        }
    }
    RestoreDC(dc, saved);
    drawScroll(dc, g_pl, L"pl");
}
static void drawPresetList() {
    HDC dc = g_bb.dc;
    Sunken(dc, g_prl.r, 2);
    Fill(dc, RECT{g_prl.r.left + 2, g_prl.r.top + 2, g_prl.r.right - 2, g_prl.r.bottom - 2}, C::list);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, g_prl.items.left, g_prl.items.top, g_prl.items.right, g_prl.items.bottom);
    if (g.presets.empty())
        Text(dc, F().small, L"No presets yet.\nPress Save New... to keep the current background, or Import... a .irixbg file.", g_prl.items,
             C::text, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    int tw = S(44), th = S(26);
    for (int i = g_prl.top; i < (int)g.presets.size() && i < g_prl.top + g_prl.vis; i++) {
        RECT r{g_prl.items.left, g_prl.items.top + (i - g_prl.top) * g_prl.rowH, g_prl.items.right, g_prl.items.top + (i - g_prl.top + 1) * g_prl.rowH};
        bool selRow = i == g.presetSel;
        if (selRow) { RECT s{r.left + S(2), r.top + 1, r.right - S(2), r.bottom - 1}; Fill(dc, s, C::listSelBg); }
        RECT tr{r.left + S(6), r.top + (g_prl.rowH - th) / 2, r.left + S(6) + tw, r.top + (g_prl.rowH - th) / 2 + th};
        std::wstring k = ThumbKey(g.presets[i].cfg, tw, th);
        if (auto img = GetThumb(k)) blitClipped(*img, tr, g_prl.items); else { Fill(dc, tr, RGB(0x9A, 0x9A, 0x9A)); RequestThumb(k, g.presets[i].cfg, tw, th); }
        Bevel(dc, RECT{tr.left - 1, tr.top - 1, tr.right + 1, tr.bottom + 1}, 1, RGB(0x40, 0x40, 0x40), RGB(0x40, 0x40, 0x40));
        RECT t{tr.right + S(8), r.top + S(2), r.right - S(4), r.top + g_prl.rowH / 2 + S(2)};
        Text(dc, F().body, g.presets[i].name, t, selRow ? C::listSelFg : C::text, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT t2{tr.right + S(8), r.top + g_prl.rowH / 2 + S(2), r.right - S(4), r.bottom};
        Text(dc, F().small, Patterns()[g.presets[i].cfg.pattern].name, t2, selRow ? Mix(C::listSelFg, C::listSelBg, 0.25f) : Mix(C::text, C::list, 0.25f), DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    RestoreDC(dc, saved);
    drawScroll(dc, g_prl, L"pr");
}
static void drawWidget(const Wd& w) {
    HDC dc = g_bb.dc;
    bool pressed = g_pressKey == w.key && g_hotKey == w.key;
    switch (w.type) {
    case WT_BUTTON: PushButton(g_bb, w.r, w.label, pressed, w.enabled); break;
    case WT_CHECK: {
        int cy = (w.r.top + w.r.bottom) / 2, s = S(14);
        CheckBox(dc, RECT{w.r.left + S(2), cy - s / 2, w.r.left + S(2) + s, cy - s / 2 + s}, w.checked, w.enabled);
        Label(g_bb, F().body, w.label, RECT{w.r.left + S(24), w.r.top, w.r.right, w.r.bottom}, w.enabled, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    }
    case WT_RADIO: {
        int cy = (w.r.top + w.r.bottom) / 2;
        Diamond(dc, w.r.left + S(10), cy, S(8), w.checked, w.enabled);
        Label(g_bb, F().body, w.label, RECT{w.r.left + S(23), w.r.top, w.r.right, w.r.bottom}, w.enabled, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    }
    case WT_SLIDER: {
        Label(g_bb, F().body, w.label, RECT{w.r.left, w.r.top, w.r.left + w.labelW - S(6), w.r.bottom}, w.enabled, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        ScaleDraw(g_bb, w.r2, sliderT(w), w.enabled, g_dragKey == w.key);
        Label(g_bb, F().body, fmtValue(w.value, w.decimals), RECT{w.r2.right + S(6), w.r.top, w.r.right, w.r.bottom}, w.enabled, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    case WT_OPTION:
        if (w.labelW > 0) Label(g_bb, F().body, w.label, RECT{w.r.left, w.r.top, w.r.left + w.labelW - S(4), w.r.bottom}, w.enabled, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        OptionButton(g_bb, w.r2, w.current, pressed, w.enabled);
        break;
    case WT_TAB: TabButton(g_bb, w.r, w.label, w.checked, g_hotKey == w.key); break;
    case WT_SWATCH: {
        Fill(dc, w.r, RGB((w.color >> 16) & 255, (w.color >> 8) & 255, w.color & 255));
        Bevel(dc, w.r, 1, C::swatchBorder, C::swatchBorder);
        if (ColorBrowserSlot() == w.slot) Bevel(dc, RECT{w.r.left - 3, w.r.top - 3, w.r.right + 3, w.r.bottom + 3}, 2, C::text, C::text);
        RECT hx{w.r.left, w.r.bottom - S(16), w.r.right, w.r.bottom};
        float l = Luma(w.color);
        Text(dc, F().small, hexOf(w.color), hx, l > 0.6f ? RGB(0x20, 0x20, 0x20) : RGB(0xF0, 0xF0, 0xF0), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    case WT_LOCK: GlyphLock(dc, w.r, w.checked, true); break;
    case WT_DICE: GlyphDice(dc, w.r, 5, w.enabled); break;
    case WT_TEXT:
        if (w.key != L"preview") Text(dc, w.font ? w.font : F().body, w.label, w.r, w.tcolor, w.align | DT_NOPREFIX);
        break;
    case WT_GROUP: EtchedGroup(g_bb, w.r, w.label); break;
    }
}
// The panel needs at least this much room; anything smaller is a minimised window (see below).
static SIZE g_testClient{0, 0};   // self test: pretend the client area has this size
static RECT clientOf(HWND h) {
    RECT rc;
    GetClientRect(h, &rc);
    if (g_testClient.cx) rc = RECT{0, 0, g_testClient.cx, g_testClient.cy};
    return rc;
}
static bool layoutFits(HWND h, const RECT& rc) { return (g_testClient.cx || !IsIconic(h)) && rc.right >= S(480) && rc.bottom >= S(360); }
static void paintMain(HWND h, HDC hdc) {
    UseDpi(h);
    RECT rc = clientOf(h);
    if (!layoutFits(h, rc)) {
        // Minimised: because this window draws its own frame, Windows still gives it a small
        // (160 x 31) client area and asks it to paint. Laying the panel out in that space gave the
        // preview a negative width - the cause of the crash on minimise / Win+D. Nothing to show.
        HBRUSH br = CreateSolidBrush(C::body);
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        return;
    }
    g_bb.ensure(rc.right, rc.bottom);
    buildUI(rc.right, rc.bottom);
    HDC dc = g_bb.dc;
    Fill(dc, rc, C::body);
    bool down = g_pressKey == g_hotKey;   // a pressed title-bar button pops back up when the mouse leaves it
    int tb = g_pressKey == L"tb.menu" ? 1 : (down && g_pressKey == L"tb.min") ? 2 : (down && g_pressKey == L"tb.max") ? 3 : 0;
    DrawFrame(dc, rc.right, rc.bottom, APP_NAME, g_active, true, tb);
    // header
    Fill(dc, L.icon, RGB(0x7D, 0x9E, 0xC0));
    Bevel(dc, L.icon, 1, RGB(0x30, 0x30, 0x30), RGB(0x30, 0x30, 0x30));
    int isz = L.icon.right - L.icon.left - 2;
    if (!g_iconBig || g_iconSize != isz) {
        if (g_iconBig) DestroyIcon(g_iconBig);
        g_iconBig = (HICON)LoadImageW(g.inst, MAKEINTRESOURCEW(1), IMAGE_ICON, isz, isz, 0);
        g_iconSize = isz;
    }
    if (g_iconBig) DrawIconEx(dc, L.icon.left + 1, L.icon.top + 1, g_iconBig, isz, isz, 0, nullptr, DI_NORMAL);
    Text(dc, F().bold, APP_NAME, L.title, C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const Pattern& P = Patterns()[g.sel];
    std::wstring sub = P.name;
    if (Patterns()[P.category].kind == K_HEADER) sub += L"  (" + Patterns()[P.category].name + L")";
    Text(dc, F().italic, sub, L.sub, C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    // left column
    Sunken(dc, RECT{L.search.left - 2, L.search.top - 2, L.search.right + 2, L.search.bottom + 2}, 2);
    drawPatternList();
    // preview
    drawPreview(h);
    // tab panel
    Fill(dc, L.panel, C::body);
    Bevel(dc, L.panel, 1, C::light, C::darker);
    Bevel(dc, RECT{L.panel.left + 1, L.panel.top + 1, L.panel.right - 1, L.panel.bottom - 1}, 1, C::light, C::dark);
    if (g.tab == TAB_PRESETS) drawPresetList();
    for (auto& w : g_w) drawWidget(w);
    // message line
    Fill(dc, L.msg, C::body);
    Bevel(dc, L.msg, 1, C::dark, C::light);
    std::wstring m;
    if (Wd* hw = findW(g_hotKey)) m = hw->help;
    if (m.empty()) m = !g.status.empty() ? g.status : P.blurb;
    Text(dc, F().small, m, RECT{L.msg.left + S(8), L.msg.top, L.msg.right - S(8), L.msg.bottom}, C::text,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    GdiFlush();
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------
static void relayout(HWND h) {
    UseDpi(h);
    RECT rc = clientOf(h);
    if (!layoutFits(h, rc)) return;   // minimised: keep the last real layout
    buildUI(rc.right, rc.bottom);
}
static std::wstring hitKey(POINT p) {
    if (In(L.f.menu, p)) return L"tb.menu";
    if (In(L.f.minimize, p)) return L"tb.min";
    if (In(L.f.maximize, p)) return L"tb.max";
    for (int i = (int)g_w.size() - 1; i >= 0; i--)
        if (In(g_w[i].r, p) && (g_w[i].interactive() || !g_w[i].help.empty())) return g_w[i].key;
    auto sbHit = [&](const ListView& lv, const std::wstring& pre) -> std::wstring {
        if (In(lv.up, p)) return pre + L".up";
        if (In(lv.dn, p)) return pre + L".dn";
        if (In(lv.thumb, p)) return pre + L".thumb";
        if (In(lv.track, p)) return p.y < lv.thumb.top ? pre + L".pgup" : pre + L".pgdn";
        if (In(lv.items, p)) return pre + L".items";
        return L"";
    };
    std::wstring k = sbHit(g_pl, L"pl");
    if (!k.empty()) return k;
    if (g.tab == TAB_PRESETS) { k = sbHit(g_prl, L"pr"); if (!k.empty()) return k; }
    return L"";
}
static ListView* listFor(const std::wstring& key) {
    if (key.rfind(L"pl.", 0) == 0) return &g_pl;
    if (key.rfind(L"pr.", 0) == 0) return &g_prl;
    return nullptr;
}
static void scrollAction(const std::wstring& key) {
    ListView* lv = listFor(key);
    if (!lv) return;
    std::wstring part = key.substr(3);
    if (part == L"up") lv->top--;
    else if (part == L"dn") lv->top++;
    else if (part == L"pgup") lv->top -= lv->vis;
    else if (part == L"pgdn") lv->top += lv->vis;
    lv->top = std::clamp(lv->top, 0, lv->maxTop());
    Changed();
}
static void sliderFromX(Wd& w, int x) {
    RECT th = ScaleThumb(w.r2, 0);
    int tw = th.right - th.left;
    float t = (float)(x - w.r2.left - 1 - tw / 2) / std::max(1, (int)(w.r2.right - w.r2.left - 2 - tw));
    float v = snapValue(w, w.vmin + std::clamp(t, 0.f, 1.f) * (w.vmax - w.vmin));
    if (v != w.value && w.set) { w.set(v); w.value = v; Changed(); ColorBrowserSync(); }
}
static void openOption(Wd& w) {
    if (!w.enabled || !w.pick || w.items.empty()) return;
    HMENU m = CreatePopupMenu();
    std::vector<std::pair<std::wstring, HMENU>> subs;
    for (size_t i = 0; i < w.items.size(); i++) {
        const MenuItem& it = w.items[i];
        UINT fl = MF_STRING | (it.checked ? MF_CHECKED : 0);
        auto esc = [](std::wstring t) { for (size_t k = 0; (k = t.find(L'&', k)) != std::wstring::npos; k += 2) t.insert(k, 1, L'&'); return t; };
        MenuItem it2 = it;
        it2.label = esc(it.label);
        it2.group = esc(it.group);
        if (it2.group.empty()) { AppendMenuW(m, fl, i + 1, it2.label.c_str()); continue; }
        HMENU sm = nullptr;
        for (auto& s : subs) if (s.first == it2.group) sm = s.second;
        if (!sm) { sm = CreatePopupMenu(); subs.push_back({it2.group, sm}); AppendMenuW(m, MF_POPUP, (UINT_PTR)sm, it2.group.c_str()); }
        AppendMenuW(sm, fl, i + 1, it2.label.c_str());
    }
    POINT pt{w.r2.left, w.r2.bottom};
    ClientToScreen(g.main, &pt);
    int cmd = trackMenu(m, TPM_LEFTBUTTON, pt);
    DestroyMenu(m);
    if (cmd > 0) {
        CrashTrail(L"choose %ls #%d", w.key.c_str(), cmd - 1);
        w.pick(cmd - 1);
    }
}
static void activate(Wd& w) {
    if (!w.enabled) return;
    CrashTrail(L"click %ls", w.key.c_str());
    if (w.type == WT_OPTION) { Wd copy = w; openOption(copy); return; }
    // run a copy: an action that opens a dialog lets the window repaint, and every repaint
    // rebuilds the widget list - which would otherwise destroy this function while it runs
    std::function<void()> fn = w.click;
    if (fn) fn();
}

// After a move to another screen, a display or taskbar change, or a DPI change: the UI scale may
// have changed (see FitDpi), so keep the window at least the panel's minimum, no bigger than the
// screen it is on, and inside that screen's work area when it had to be resized.
// window position and size are remembered between sessions (normal rectangle + maximised flag)
static void rememberPlacement(HWND h) {
    if (g_selftest || IsIconic(h)) return;
    g.winMax = IsZoomed(h) != FALSE;
    if (!g.winMax) GetWindowRect(h, &g.winRect);
}
static void fitToMonitor(HWND h) {
    if (IsIconic(h) || IsZoomed(h)) return;
    UseDpi(h);
    RECT wr;
    GetWindowRect(h, &wr);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return;
    int ww = mi.rcWork.right - mi.rcWork.left, wh = mi.rcWork.bottom - mi.rcWork.top;
    int w0 = wr.right - wr.left, h0 = wr.bottom - wr.top;
    int w = std::max<int>(w0, S(960)), hh = std::max<int>(h0, S(700));
    if (w > ww) w = std::max<int>(S(960), ww);
    if (hh > wh) hh = std::max<int>(S(700), wh);
    if (w == w0 && hh == h0) return;
    int x = std::max<int>(mi.rcWork.left, std::min<int>(wr.left, mi.rcWork.right - w));
    int y = std::max<int>(mi.rcWork.top, std::min<int>(wr.top, mi.rcWork.bottom - hh));
    SetWindowPos(h, nullptr, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
}
static LRESULT mainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        g.main = h;
        SetupFrameWindow(h);
        if (HMENU sm = GetSystemMenu(h, FALSE)) {   // the 4Dwm window menu (top-left button, Alt+Space)
            AppendMenuW(sm, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(sm, MF_STRING, kIdmTheme, L"Theme Options...");
            AppendMenuW(sm, MF_STRING, kIdmAbout, L"About " APP_NAME L"...");
        }
        g.search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)100, g.inst, nullptr);
        SendMessageW(g.search, EM_SETLIMITTEXT, 64, 0);
        WorkerStart(h);
        return 0;
    }
    case WM_SIZE: {
        if (wp == SIZE_MINIMIZED) { CrashTrail(L"minimised"); return 0; }
        rememberPlacement(h);
        CrashTrail(L"window %dx%d at %d dpi", LOWORD(lp), HIWORD(lp), g_dpi);
        relayout(h);
        if (g.search) {
            SendMessageW(g.search, WM_SETFONT, (WPARAM)F().body, TRUE);
            MoveWindow(g.search, L.search.left, L.search.top + S(3), L.search.right - L.search.left, L.search.bottom - L.search.top - S(4), TRUE);
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_CTLCOLOREDIT: return EditColors((HDC)wp);
    case WM_COMMAND:
        if (LOWORD(wp) == 100 && HIWORD(wp) == EN_CHANGE) {
            wchar_t b[80];
            GetWindowTextW(g.search, b, 80);
            g.searchText = b;
            computeRows();
            g_pl.top = 0;
            g_pl.ensure(rowOfPattern(g.sel));
            Changed();
        }
        return 0;
    case WM_MEASUREITEM: if (MenuMeasure((MEASUREITEMSTRUCT*)lp)) return TRUE; return DefWindowProcW(h, m, wp, lp);
    case WM_DRAWITEM: if (MenuDraw((DRAWITEMSTRUCT*)lp)) return TRUE; return DefWindowProcW(h, m, wp, lp);
    case WM_NCCALCSIZE: return FrameNcCalcSize(h, wp, lp);
    case WM_NCHITTEST: return FrameHitTest(h, lp, true, true);
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == kIdmAbout) { showAbout(); return 0; }
        if ((wp & 0xFFF0) == kIdmTheme) { OpenThemeOptions(); return 0; }
        return DefWindowProcW(h, m, wp, lp);
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTCAPTION) {   // double-click the title bar: maximize / restore, as on the 4Dwm
            CrashTrail(L"title double-click");
            ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        return DefWindowProcW(h, m, wp, lp);
    case WM_NCACTIVATE: g_active = wp != 0; InvalidateRect(h, nullptr, FALSE); return DefWindowProcW(h, m, wp, -1);
    case WM_ERASEBKGND: return 1;
    case WM_GETMINMAXINFO: {
        UseDpi(h);
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = S(960);
        mm->ptMinTrackSize.y = S(700);
        // A frameless popup would otherwise maximise over the whole monitor, taskbar included:
        // maximise to the work area of the monitor the window is on (relative to that monitor).
        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) {
            mm->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mm->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            mm->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            mm->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            mm->ptMaxTrackSize.x = std::max<LONG>(mm->ptMaxTrackSize.x, mm->ptMaxSize.x);
            mm->ptMaxTrackSize.y = std::max<LONG>(mm->ptMaxTrackSize.y, mm->ptMaxSize.y);
        }
        return 0;
    }
    case WM_EXITSIZEMOVE: case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE:
        fitToMonitor(h);
        InvalidateRect(h, nullptr, FALSE);
        return m == WM_SETTINGCHANGE ? DefWindowProcW(h, m, wp, lp) : 0;
    case WM_MOVE:
        rememberPlacement(h);
        if (!IsIconic(h) && IsTiled(Patterns()[g.sel].kind)) InvalidateRect(h, &L.pvIn, FALSE);   // keep the desktop-locked preview in step
        return 0;
    case WM_DPICHANGED: {
        CrashTrail(L"dpi changed to %d", LOWORD(wp));
        RECT* r = (RECT*)lp;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        fitToMonitor(h);
        return 0;
    }
    case WM_APP_PREVIEW: case WM_APP_THUMB: InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_APP_TASK: {
        std::wstring* msg = (std::wstring*)lp;
        bool ok = wp != 0;
        if (ok && g_taskKind == TASK_APPLY) SaveApplied(g_taskCfg);
        g.status = msg ? *msg : (ok ? L"Done." : L"Failed.");
        delete msg;
        g.busy = false;
        g_taskKind = TASK_NONE;
        Changed();
        if (g_closePending) PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        paintMain(h, hdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        SetFocus(h);
        relayout(h);
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        std::wstring k = hitKey(p);
        bool dbl = m == WM_LBUTTONDBLCLK;
        if (k == L"tb.menu") {
            if (dbl) { PostMessageW(h, WM_CLOSE, 0, 0); return 0; }
            RECT wr;
            GetWindowRect(h, &wr);
            g_pressKey = k; InvalidateRect(h, nullptr, FALSE); UpdateWindow(h);
            int cmd = windowMenu(h, POINT{wr.left + L.f.menu.left, wr.top + L.f.menu.bottom});
            g_pressKey.clear(); InvalidateRect(h, nullptr, FALSE);
            if (cmd) PostMessageW(h, WM_SYSCOMMAND, cmd, 0);
            return 0;
        }
        if (k == L"tb.min" || k == L"tb.max") {
            // 4Dwm iconify / maximize buttons: pressed while the mouse is down, act on release over
            // the button (moving off cancels, like any push button)
            g_pressKey = g_hotKey = k;
            SetCapture(h);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (k == L"pl.items") {
            int row = g_pl.rowAt(p);
            if (row >= 0) {
                int pat = g_rows[row];
                if (Patterns()[pat].kind != K_HEADER) { selectPattern(pat); if (dbl && pat == g.sel) doApply(); }
            }
            return 0;
        }
        if (k == L"pr.items") {
            int row = g_prl.rowAt(p);
            g.presetSel = row;
            if (dbl && row >= 0) loadPreset(row);
            Changed();
            return 0;
        }
        if (k == L"pl.thumb" || k == L"pr.thumb") { ListView* lv = listFor(k); g_dragKey = k; g_pressKey = k; g_dragOff = p.y - lv->thumb.top; SetCapture(h); return 0; }
        if (k == L"pl.up" || k == L"pl.dn" || k == L"pl.pgup" || k == L"pl.pgdn" || k == L"pr.up" || k == L"pr.dn" || k == L"pr.pgup" || k == L"pr.pgdn") {
            g_pressKey = g_hotKey = k; SetCapture(h); scrollAction(k); SetTimer(h, 1, 350, nullptr); return 0;
        }
        Wd* w = findW(k);
        if (!w || !w->interactive()) return 0;
        if (!w->enabled) { g_hotKey = k; Changed(); return 0; }
        if (w->type == WT_SLIDER) {
            if (dbl && p.x < w->r2.left) { Edit(); w->set(w->def); Changed(); ColorBrowserSync(); return 0; }   // reset by double-clicking the label
            if (p.x < w->r2.left - S(2) || p.x > w->r2.right + S(2)) return 0;
            Edit();
            g_dragKey = k;
            SetCapture(h);
            sliderFromX(*w, p.x);
            return 0;
        }
        if (w->type == WT_OPTION) {
            g_pressKey = k; InvalidateRect(h, nullptr, FALSE); UpdateWindow(h);
            Wd copy = *w;
            g_pressKey.clear();
            openOption(copy);
            Changed();
            return 0;
        }
        if (w->type == WT_TAB || w->type == WT_SWATCH || w->type == WT_LOCK || w->type == WT_DICE) { activate(*w); return 0; }
        g_pressKey = g_hotKey = k;
        SetCapture(h);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_RBUTTONDOWN: {
        relayout(h);
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        Wd* w = findW(hitKey(p));
        if (w && w->type == WT_SWATCH) {
            POINT s = p;
            ClientToScreen(h, &s);
            swatchMenu(w->slot, s);
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) {
            if (listFor(g_pressKey) && g_hotKey == g_pressKey && g_pressKey.find(L"thumb") == std::wstring::npos) { scrollAction(g_pressKey); SetTimer(h, 1, 55, nullptr); }
            else if (g_pressKey.empty()) KillTimer(h, 1);
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (!g_dragKey.empty()) {
            relayout(h);
            if (ListView* lv = listFor(g_dragKey)) {
                int off = g_dragOff;
                int th = lv->track.bottom - lv->track.top, tH = lv->thumb.bottom - lv->thumb.top;
                if (th > tH && lv->maxTop() > 0) {
                    int nt = (int)std::lround((double)(p.y - off - lv->track.top) * lv->maxTop() / (th - tH));
                    nt = std::clamp(nt, 0, lv->maxTop());
                    if (nt != lv->top) { lv->top = nt; Changed(); }
                }
            } else if (Wd* w = findW(g_dragKey)) {
                sliderFromX(*w, p.x);
            }
            return 0;
        }
        relayout(h);
        std::wstring k = hitKey(p);
        if (!g_pressKey.empty()) k = k == g_pressKey ? k : L"";
        if (k != g_hotKey) { g_hotKey = k; InvalidateRect(h, nullptr, FALSE); }
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_pressKey.empty() && g_dragKey.empty() && !g_hotKey.empty()) { g_hotKey.clear(); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONUP: {
        std::wstring pk = g_pressKey;
        bool click = !pk.empty() && g_hotKey == pk;
        g_pressKey.clear();
        g_dragKey.clear();
        ReleaseCapture();
        KillTimer(h, 1);
        InvalidateRect(h, nullptr, FALSE);
        if (click && !listFor(pk) && pk.rfind(L"tb.", 0) != 0) {
            relayout(h);
            if (Wd* w = findW(pk)) activate(*w);
        } else if (click && pk == L"tb.min") {
            CrashTrail(L"click minimize");
            ShowWindow(h, SW_MINIMIZE);
        } else if (click && pk == L"tb.max") {
            CrashTrail(L"click %ls", IsZoomed(h) ? L"restore" : L"maximize");
            ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if ((HWND)lp != h && (!g_pressKey.empty() || !g_dragKey.empty())) { g_pressKey.clear(); g_dragKey.clear(); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &p);
        relayout(h);
        int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        if (!steps) return 0;
        std::wstring k = hitKey(p);
        if (ListView* lv = listFor(k)) { lv->top = std::clamp(lv->top - steps * 3, 0, lv->maxTop()); Changed(); return 0; }
        if (In(g_pl.r, p)) { g_pl.top = std::clamp(g_pl.top - steps * 3, 0, g_pl.maxTop()); Changed(); return 0; }
        if (Wd* w = findW(k)) {
            if (w->type == WT_SLIDER && w->enabled && w->set) {
                float step = w->decimals ? (w->vmax - w->vmin) / 50.f : std::max(1.f, std::round((w->vmax - w->vmin) / 50.f));
                float v = snapValue(*w, w->value + steps * step);
                if (v != w->value) { Edit(); w->set(v); Changed(); ColorBrowserSync(); }
            }
        }
        return 0;
    }
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        CrashTrail(L"key %ls0x%02X", ctrl ? L"Ctrl+" : L"", (unsigned)wp);
        relayout(h);
        if (ctrl) {
            switch (wp) {
            case 'Z': if (shift) Redo(); else Undo(); return 0;
            case 'Y': Redo(); return 0;
            case 'R': randomizePalette(); return 0;
            case 'N': if (HasVariations(g.sel)) { Edit(); Cur().seed = NextRandom(); Changed(); } return 0;
            case 'S': savePresetNew(); return 0;
            case 'F': SetFocus(g.search); SendMessageW(g.search, EM_SETSEL, 0, -1); return 0;
            case 'T': OpenThemeOptions(); return 0;
            case VK_TAB: g.tab = (g.tab + (shift ? TAB_COUNT - 1 : 1)) % TAB_COUNT; Changed(); return 0;
            }
            return 0;
        }
        switch (wp) {
        case VK_UP: moveSelection(-1); break;
        case VK_DOWN: moveSelection(1); break;
        case VK_PRIOR: moveSelection(-g_pl.vis); break;
        case VK_NEXT: moveSelection(g_pl.vis); break;
        case VK_HOME: moveSelection(-100000); break;
        case VK_END: moveSelection(100000); break;
        case VK_RETURN: doApply(); break;
        case VK_ESCAPE: PostMessageW(h, WM_CLOSE, 0, 0); break;
        case VK_F1: showHelp(); break;
        }
        return 0;
    }
    case WM_CLOSE:
        if (g.busy && !g_closePending) {
            // an Apply or Export is still writing its image: finish it, then close (closing again
            // closes at once)
            g_closePending = true;
            setStatus(L"Finishing the current task, then closing...");
            return 0;
        }
        CloseColorBrowser(false);
        CloseThemeOptions();
        SaveState();
        DestroyWindow(h);
        return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION:
        // signing out / restarting: Windows ends the program without WM_CLOSE, so save now
        if (wp) { CrashTrail(L"session ending"); SaveState(); }
        return 0;
    case WM_DESTROY:
        WorkerStop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ---------------------------------------------------------------------------------------------
// Recovery boundary: an error inside any message handler is reported (crash.log) and the panel
// keeps running instead of the whole program closing. The first such error in a session is
// also shown in a message box.
// ---------------------------------------------------------------------------------------------
static const wchar_t* msgName(UINT m) {
    switch (m) {
    case WM_CREATE: return L"WM_CREATE"; case WM_SIZE: return L"WM_SIZE"; case WM_PAINT: return L"WM_PAINT";
    case WM_LBUTTONDOWN: return L"WM_LBUTTONDOWN"; case WM_LBUTTONUP: return L"WM_LBUTTONUP"; case WM_LBUTTONDBLCLK: return L"WM_LBUTTONDBLCLK";
    case WM_RBUTTONDOWN: return L"WM_RBUTTONDOWN"; case WM_MOUSEMOVE: return L"WM_MOUSEMOVE"; case WM_MOUSEWHEEL: return L"WM_MOUSEWHEEL";
    case WM_KEYDOWN: return L"WM_KEYDOWN"; case WM_TIMER: return L"WM_TIMER"; case WM_COMMAND: return L"WM_COMMAND";
    case WM_GETMINMAXINFO: return L"WM_GETMINMAXINFO"; case WM_DPICHANGED: return L"WM_DPICHANGED"; case WM_CLOSE: return L"WM_CLOSE";
    case WM_APP_PREVIEW: return L"preview ready"; case WM_APP_THUMB: return L"thumbnail ready"; case WM_APP_TASK: return L"task finished";
    }
    return nullptr;
}
static bool g_crashTestUi = false;
static void recoveredInUi(const wchar_t* window, HWND h, UINT m) {
    static bool shown = false, inBox = false;
    wchar_t where[96], num[16];
    const wchar_t* mn = msgName(m);
    if (!mn) { swprintf(num, 16, L"msg 0x%04X", m); mn = num; }
    swprintf(where, 96, L"%ls, %ls", window, mn);
    std::wstring sum = CrashRecovered(where);
    if (m == WM_PAINT) ValidateRect(h, nullptr);   // do not repaint-fail in a loop
    g_pressKey.clear();
    g_dragKey.clear();
    if (GetCapture() == h) ReleaseCapture();
    g.busy = g.busy && g_taskKind != TASK_NONE;
    g.status = L"Internal error: " + sum + L" - details in crash.log";
    if (g.main) InvalidateRect(g.main, nullptr, FALSE);
    if (!shown && !inBox && !g_selftest) {
        shown = inBox = true;
        std::wstring msg = std::wstring(APP_NAME) + L" ran into an internal error but kept running.\n\n" + sum + L"\n\nA report was saved to:\n" + CrashLogPath() +
                           L"\n\nIf something looks wrong, save your work as a preset and restart the program.";
        MessageBoxW(h, msg.c_str(), APP_NAME, MB_OK | MB_ICONWARNING);
        inBox = false;
    }
}
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    try {
        if (g_crashTestUi && m == WM_LBUTTONDOWN) { g_crashTestUi = false; std::wstring t = L"x"; t.at(5) = L'y'; }
        return mainProc(h, m, wp, lp);
    } catch (...) {
        recoveredInUi(L"main window", h, m);
        return m == WM_CREATE ? 0 : DefWindowProcW(h, m, wp, lp);
    }
}
static std::wstring describeState() {
    if (g.cfg.empty() || g.sel < 0 || g.sel >= (int)g.cfg.size()) return L"";
    static const wchar_t* tabs[TAB_COUNT] = {L"Colors", L"Pattern", L"Transform", L"Adjust", L"Presets & Export"};
    RECT rc{};
    if (g.main) GetClientRect(g.main, &rc);
    wchar_t b[200];
    swprintf(b, 200, L"window %ldx%ld at %d dpi, tab %ls, %d presets, %d own palettes\r\n           pattern \"", rc.right, rc.bottom, g_dpi,
             tabs[std::clamp(g.tab, 0, TAB_COUNT - 1)], (int)g.presets.size(), (int)g.userPalettes.size());
    return b + Patterns()[g.sel].name + L"\" " + ConfigKey(Cur());
}

// ---------------------------------------------------------------------------------------------
// Self test: drives every control of every tab on every pattern, plus the file workflows.
// ---------------------------------------------------------------------------------------------
static FILE* g_log = nullptr;
static int g_fail = 0, g_pass = 0;
static void T(bool ok, const wchar_t* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    wchar_t b[1024];
    vswprintf(b, 1024, fmt, a);
    va_end(a);
    if (ok) g_pass++; else g_fail++;
    if (!ok || !g_log) {
        if (g_log) fwprintf(g_log, L"FAIL %ls\n", b);
    }
}
static void Note(const wchar_t* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    if (g_log) { vfwprintf(g_log, fmt, a); fwprintf(g_log, L"\n"); fflush(g_log); }
    va_end(a);
}
static bool configValid(const Config& c) { Config s = c; SanitizeConfig(s); return s == c; }
static void layoutNow() { RECT rc; GetClientRect(g.main, &rc); UseDpi(g.main); buildUI(rc.right, rc.bottom); }
static bool fileOk(const std::wstring& p, DWORD minSize) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return false;
    return d.nFileSizeLow >= minSize || d.nFileSizeHigh > 0;
}
static std::vector<std::wstring> g_skip = {L"apply", L"close", L"exp.go", L"pre.save", L"pre.update", L"pre.load", L"pre.rename", L"pre.delete",
                                            L"pre.import", L"pre.export", L"watch", L"openfolder", L"pal.import", L"pal.export", L"pal.save", L"pal.delete", L"exp.size"};
static bool skipped(const std::wstring& k) { return std::find(g_skip.begin(), g_skip.end(), k) != g_skip.end(); }

static double nowMs() { LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f); return t.QuadPart * 1000.0 / f.QuadPart; }
static void Note(const wchar_t* fmt, ...);
static void exerciseWidgets(int pat, int& actions) {
    for (int t = 0; t < TAB_COUNT; t++) {
        g.tab = t;
        layoutNow();
        std::vector<std::wstring> keys;
        for (auto& w : g_w) if (w.interactive()) keys.push_back(w.key);
        for (auto& k : keys) {
            layoutNow();
            Wd* w = findW(k);
            if (!w) continue;   // controls can legitimately disappear after an earlier action (e.g. a choice list)
            // no dead controls: everything interactive does something, everything disabled says why
            bool hasAction = w->click || w->set || (w->pick && w->type == WT_OPTION);
            T(hasAction, L"%ls: control '%ls' has no action", Patterns()[pat].name.c_str(), k.c_str());
            if (!w->enabled) { T(!w->help.empty(), L"%ls: disabled control '%ls' does not explain why", Patterns()[pat].name.c_str(), k.c_str()); continue; }
            T(!w->help.empty(), L"%ls: control '%ls' has no help text", Patterns()[pat].name.c_str(), k.c_str());
            if (skipped(k) || k == L"help") continue;
            static int browserOpens = 0;   // opening the Color Browser is tested a few times, not 600
            if (k.rfind(L"sw", 0) == 0 && ++browserOpens > 6) continue;
            Wd copy = *w;
            double t0 = nowMs();
            struct Slow { double t0; std::wstring k; int pat; ~Slow() { double d = nowMs() - t0; if (d > 400) Note(L"  slow: %ls / %ls %.0f ms", Patterns()[pat].name.c_str(), k.c_str(), d); } } slow{t0, k, pat};
            if (k.rfind(L"tab", 0) == 0) { copy.click(); T(g.tab == _wtoi(k.c_str() + 3), L"tab %ls did not switch", k.c_str()); g.tab = t; actions++; continue; }
            if (copy.type == WT_SLIDER) {
                float vals[3] = {copy.vmin, copy.vmax, copy.def};
                for (float v : vals) {
                    float want = snapValue(copy, v);
                    copy.set(want);
                    layoutNow();
                    Wd* again = findW(k);
                    T(again && std::fabs(again->value - want) <= 1e-4f + std::pow(10.f, (float)-copy.decimals),
                      L"%ls: slider '%ls' set %g read back %g", Patterns()[pat].name.c_str(), k.c_str(), want, again ? again->value : -999.f);
                    actions++;
                }
            } else if (copy.type == WT_OPTION) {
                for (int i = 0; i < (int)copy.items.size() && i < 64; i++) { copy.pick(i); actions++; }
                if (k == L"category") { g.category = 0; computeRows(); }
            } else if (copy.click) {
                copy.click();
                actions++;
                if (k.rfind(L"sw", 0) == 0) CloseColorBrowser(false);
            }
            T(configValid(Cur()), L"%ls: config invalid after '%ls'", Patterns()[pat].name.c_str(), k.c_str());
            T(g.sel >= 0 && g.sel < (int)Patterns().size() && Patterns()[g.sel].kind != K_HEADER, L"selection invalid after '%ls'", k.c_str());
            if (g.sel != pat) g.sel = pat;   // keep exercising the same pattern
        }
    }
}


// ---- random input: thousands of real mouse, wheel and keyboard messages (and window moves,
// minimise / maximise, search typing, Color Browser use) sent through the window procedures, with
// the program's invariants checked after every step. Dialogs are answered at random; nothing
// that deletes or overwrites the user's files is clicked. IRIXBG_MONKEY=<steps> runs longer.
static void monkeyTest(int steps, uint32_t seed, void (*pump)(int)) {
    g_monkey = true;
    g_monkeyRnd = seed ? seed : 1;
    auto R = [](int n) { return n > 0 ? (int)(monkeyRand() % (uint32_t)n) : 0; };
    const auto& P = Patterns();
    int before = CrashRecoveredCount(), clicks = 0, keys = 0, drags = 0, wins = 0, cbs = 0, ths = 0, broken = 0;
    BYTE ks[256];
    auto keyState = [&](bool ctrl, bool shift) {
        GetKeyboardState(ks);
        ks[VK_CONTROL] = ctrl ? 0x80 : 0; ks[VK_LCONTROL] = ks[VK_CONTROL];
        ks[VK_SHIFT] = shift ? 0x80 : 0; ks[VK_LSHIFT] = ks[VK_SHIFT];
        SetKeyboardState(ks);
    };
    static const wchar_t* finds[] = {L"", L"ne", L"fract", L"x", L"sgi", L"zzzz", L"chaos", L"demo", L"a"};
    for (int step = 0; step < steps; step++) {
        if (IsIconic(g.main) && R(3)) { ShowWindow(g.main, SW_RESTORE); pump(30); }
        relayout(g.main);
        RECT rc;
        GetClientRect(g.main, &rc);
        g_stubAnswer = R(3);
        g_stubText = R(4) == 0 ? L"" : L"Monkey " + std::to_wstring(R(4));
        g_stubPath.clear();   // file dialogs: always cancelled
        auto pickPoint = [&]() -> POINT {
            for (int t = 0; t < 30; t++) {
                POINT p;
                if (!g_w.empty() && R(10) < 7) {
                    const Wd& w = g_w[R((int)g_w.size())];
                    p = POINT{w.r.left + R(std::max<LONG>(1, w.r.right - w.r.left)), w.r.top + R(std::max<LONG>(1, w.r.bottom - w.r.top))};
                } else {
                    p = POINT{R(rc.right), R(rc.bottom)};
                }
                std::wstring k = hitKey(p);
                if (k == L"tb.menu" || k == L"close" || skipped(k)) continue;
                return p;
            }
            return POINT{rc.right / 2, rc.bottom / 2};
        };
        int a = R(100);
        HWND cbw = ColorBrowserWindow();
        if (cbw && a < 25) {   // the Color Browser, when open
            RECT cr;
            GetClientRect(cbw, &cr);
            POINT p{R(cr.right), R(cr.bottom)};
            LPARAM at = MAKELPARAM(p.x, p.y);
            int b = R(10);
            if (b < 6) {
                SendMessageW(cbw, WM_MOUSEMOVE, 0, at);
                SendMessageW(cbw, WM_LBUTTONDOWN, MK_LBUTTON, at);
                for (int k = R(4); k > 0; k--) SendMessageW(cbw, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(R(cr.right), R(cr.bottom)));
                SendMessageW(cbw, WM_LBUTTONUP, 0, at);
            } else if (b < 8) {
                SendMessageW(cbw, WM_KEYDOWN, R(2) ? VK_RETURN : VK_ESCAPE, 0);
            } else {
                SendMessageW(cbw, WM_MOUSEWHEEL, MAKEWPARAM(0, (R(2) ? 1 : -1) * WHEEL_DELTA), at);
            }
            cbs++;
        } else if (HWND thw = ThemeOptionsWindow(); thw && a < 35) {   // Theme Options, when open
            RECT tr;
            GetClientRect(thw, &tr);
            POINT p{R(tr.right), R(tr.bottom)};
            LPARAM at = MAKELPARAM(p.x, p.y);
            int b = R(12);
            if (b < 7) {
                SendMessageW(thw, WM_MOUSEMOVE, 0, at);
                SendMessageW(thw, b == 0 ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN, MK_LBUTTON, at);
                for (int k = R(3); k > 0; k--) SendMessageW(thw, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(R(tr.right), R(tr.bottom)));
                SendMessageW(thw, WM_LBUTTONUP, 0, at);
            } else if (b < 10) {
                static const UINT tk[] = {VK_UP, VK_DOWN, VK_HOME, VK_END, VK_PRIOR, VK_NEXT, VK_RETURN, VK_ESCAPE};
                SendMessageW(thw, WM_KEYDOWN, tk[R(8)], 0);
            } else {
                SendMessageW(thw, WM_MOUSEWHEEL, MAKEWPARAM(0, (R(2) ? 1 : -1) * WHEEL_DELTA), at);
            }
            ths++;
        } else if (a < 40) {
            POINT p = pickPoint();
            LPARAM at = MAKELPARAM(p.x, p.y);
            SendMessageW(g.main, WM_MOUSEMOVE, 0, at);
            SendMessageW(g.main, WM_LBUTTONDOWN, MK_LBUTTON, at);
            SendMessageW(g.main, WM_LBUTTONUP, 0, at);
            clicks++;
        } else if (a < 47) {
            POINT p = pickPoint();
            LPARAM at = MAKELPARAM(p.x, p.y);
            SendMessageW(g.main, WM_LBUTTONDOWN, MK_LBUTTON, at);
            SendMessageW(g.main, WM_LBUTTONUP, 0, at);
            SendMessageW(g.main, WM_LBUTTONDBLCLK, MK_LBUTTON, at);
            SendMessageW(g.main, WM_LBUTTONUP, 0, at);
            clicks++;
        } else if (a < 57) {
            POINT p = pickPoint();
            SendMessageW(g.main, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(p.x, p.y));
            for (int k = 1 + R(6); k > 0; k--) {
                p.x += R(S(160)) - S(80); p.y += R(S(60)) - S(30);
                SendMessageW(g.main, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM((WORD)(short)p.x, (WORD)(short)p.y));
            }
            SendMessageW(g.main, WM_LBUTTONUP, 0, MAKELPARAM((WORD)(short)p.x, (WORD)(short)p.y));
            drags++;
        } else if (a < 64) {
            POINT p = pickPoint();
            ClientToScreen(g.main, &p);
            SendMessageW(g.main, WM_MOUSEWHEEL, MAKEWPARAM(0, (R(2) ? 1 : -1) * WHEEL_DELTA * (1 + R(3))), MAKELPARAM(p.x, p.y));
        } else if (a < 78) {
            static const UINT plain[] = {VK_UP, VK_DOWN, VK_PRIOR, VK_NEXT, VK_HOME, VK_END, VK_F1, VK_UP, VK_DOWN};
            static const UINT ctrl[] = {'Z', 'Y', 'R', 'N', VK_TAB, 'Z', 'R', 'T'};
            bool c = R(2) != 0, sh = R(4) == 0;
            keyState(c, sh);
            SendMessageW(g.main, WM_KEYDOWN, c ? ctrl[R(8)] : plain[R(9)], 0);
            keyState(false, false);
            keys++;
        } else if (a < 84) {
            POINT p = pickPoint();
            SendMessageW(g.main, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(p.x, p.y));
            SendMessageW(g.main, WM_RBUTTONUP, 0, MAKELPARAM(p.x, p.y));
            clicks++;
        } else if (a < 88) {
            SetWindowTextW(g.search, finds[R(9)]);   // typing in Find (EN_CHANGE)
            keys++;
        } else if (a < 93) {
            int w = R(5);
            if (w == 0) ShowWindow(g.main, SW_MINIMIZE);
            else if (w == 1) ShowWindow(g.main, IsZoomed(g.main) ? SW_RESTORE : SW_MAXIMIZE);
            else if (w == 2) ShowWindow(g.main, SW_RESTORE);
            else {
                MONITORINFO mi{sizeof(mi)};
                GetMonitorInfoW(MonitorFromWindow(g.main, MONITOR_DEFAULTTONEAREST), &mi);
                int ww = mi.rcWork.right - mi.rcWork.left, wh = mi.rcWork.bottom - mi.rcWork.top;
                SetWindowPos(g.main, nullptr, mi.rcWork.left + R(std::max(1, ww / 4)), mi.rcWork.top + R(std::max(1, wh / 4)), S(900) + R(std::max(1, ww - S(900))),
                             S(650) + R(std::max(1, wh - S(650))), SWP_NOZORDER | SWP_NOACTIVATE);
                SendMessageW(g.main, WM_EXITSIZEMOVE, 0, 0);
            }
            wins++;
        } else {
            for (int i = 0; i < 40 && PreviewPending(); i++) Sleep(10);
        }
        pump(step % 25 == 0 ? 40 : 2);
        if (step && step % 1000 == 0) Note(L"  random input: %d steps", step);
        // invariants
        bool ok = g.sel >= 0 && g.sel < (int)P.size() && P[g.sel].kind != K_HEADER && configValid(Cur()) && IsWindow(g.main) &&
                  g.tab >= 0 && g.tab < TAB_COUNT && g.presetSel < (int)g.presets.size() && (g.searchText.empty() || g_rows.size() <= P.size());
        if (!ok && broken++ < 5) T(false, L"random input step %d left the program in a bad state (pattern %d, tab %d)", step, g.sel, g.tab);
        if (CrashRecoveredCount() != before) {
            T(false, L"random input step %d raised an internal error (see crash.log)", step);
            break;
        }
    }
    keyState(false, false);
    if (IsIconic(g.main) || IsZoomed(g.main)) ShowWindow(g.main, SW_RESTORE);
    CloseColorBrowser(false);
    CloseThemeOptions();
    SetWindowTextW(g.search, L"");
    g_monkey = false;
    g_stubAnswer = 0; g_stubText.clear(); g_stubPath.clear();
    Note(L"random input: %d steps (seed %u) - %d clicks, %d drags, %d keys, %d window changes, %d Color Browser and %d Theme Options actions", steps, seed,
         clicks, drags, keys, wins, cbs, ths);
}

// --selftest log --monkey: only the random-input test (IRIXBG_MONKEY steps, IRIXBG_MONKEY_SEED)
static int runMonkeyOnly(const std::wstring& logPath) {
    g_log = _wfopen(logPath.c_str(), L"w, ccs=UTF-8");
    std::wstring files[2] = {AppDataDir() + L"\\state.txt", AppDataDir() + L"\\palettes.txt"};
    std::wstring saved[2];
    bool had[2];
    for (int i = 0; i < 2; i++) had[i] = ReadTextFile(files[i], saved[i]);
    Config userApplied;
    bool userHadApplied = LoadApplied(userApplied);
    wchar_t ev[32] = L"";
    int steps = GetEnvironmentVariableW(L"IRIXBG_MONKEY", ev, 32) ? _wtoi(ev) : 2500;
    uint32_t seed = GetEnvironmentVariableW(L"IRIXBG_MONKEY_SEED", ev, 32) ? (uint32_t)wcstoul(ev, nullptr, 10) : 20261015u;
    auto pump = [](int ms) {
        double t0 = nowMs();
        MSG m;
        do {
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
            if (nowMs() - t0 < ms) Sleep(1);
        } while (nowMs() - t0 < ms);
    };
    monkeyTest(std::max(0, steps), seed, pump);
    T(CrashRecoveredCount() == 0, L"%d internal errors were recovered (see crash.log)", CrashRecoveredCount());
    for (int i = 0; i < 2; i++) { if (had[i]) WriteTextFile(files[i], saved[i]); else DeleteFileW(files[i].c_str()); }
    if (userHadApplied) ApplyWallpaper(userApplied, nullptr);
    Note(L"RESULT: %d passed, %d failed", g_pass, g_fail);
    if (g_log) fclose(g_log);
    return g_fail;
}
static int runSelfTest(const std::wstring& logPath) {
    g_log = _wfopen(logPath.c_str(), L"w, ccs=UTF-8");
    // keep the user's own settings: back up the state and palette files, restore them at the end
    std::wstring files[2] = {AppDataDir() + L"\\state.txt", AppDataDir() + L"\\palettes.txt"};
    std::wstring saved[2];
    bool had[2];
    for (int i = 0; i < 2; i++) had[i] = ReadTextFile(files[i], saved[i]);
    Config userApplied;
    bool userHadApplied = LoadApplied(userApplied);
    Note(APP_NAME L" " APP_VERSION L" self test");
    const auto& P = Patterns();
    int npat = 0, ndream = 0;
    for (auto& p : P) if (p.kind != K_HEADER) { npat++; if (p.kind == K_DTILE || p.kind == K_DSCENE) ndream++; }
    Note(L"patterns: %d selectable, %d generated in %d families", npat, ndream, 11);
    T(ndream == 138, L"expected 138 generated patterns, found %d", ndream);
    // IRIX Classics is exactly the list of the IRIX 6.5 Background Setting panel, names untouched
    {
        static const wchar_t* irixList[] = {L"Brain", L"Bulge", L"Burlap", L"Camouflage", L"Cirrostratus", L"Citrus Citrus", L"Cracked", L"Crumpled",
                                            L"Dither 1x1", L"Dither 2x2", L"Escher Knot", L"Fabric", L"Granite", L"Iron", L"Linen", L"Luna Pearl",
                                            L"Midnight", L"Moorish", L"Nebula", L"Reef", L"Reef (HDTV)", L"Rhodo", L"Rockface", L"Sand", L"Scales",
                                            L"Scallop", L"Scatter", L"Scribble", L"Sky (HDTV)", L"Solid", L"Sprinkle", L"Swirl", L"Tide Pool",
                                            L"Twilight", L"Verde Marble", L"Vertigo", L"Wicker"};
        std::vector<std::wstring> got;
        for (auto& p : P) if (p.kind != K_HEADER && P[p.category].name == L"IRIX Classics") got.push_back(p.name);
        bool same = got.size() == sizeof(irixList) / sizeof(irixList[0]);
        for (size_t i = 0; same && i < got.size(); i++) same = got[i] == irixList[i];
        T(same, L"IRIX Classics must be exactly the 37 IRIX backgrounds in IRIX's order (found %d)", (int)got.size());
        T(P[0].kind == K_HEADER && P[0].name == L"IRIX Classics", L"IRIX Classics must come first");
    }
    int nextra = 0;
    for (auto& p : P) if (p.kind != K_HEADER && P[p.category].name == L"IRIX Extras") nextra++;
    T(nextra == 20, L"expected 20 IRIX Extras, found %d", nextra);
    // patterns retired in 2.3 resolve to a relative (old presets keep working); real names win
    for (const wchar_t* r : {L"Burning Ship", L"Metaballs", L"Copper Bars", L"Iso Cubes", L"Static"})
        T(FindPattern(r) < 0 && FindPatternOrSuccessor(r) >= 0 && IsRetiredPattern(r), L"retired pattern %ls does not map to a successor", r);
    {
        Config rc;
        std::map<std::wstring, std::wstring> kv{{L"pattern", L"Lorenz Butterfly"}, {L"colors", L"#112233,#445566,#778899,#AABBCC"}};
        T(ConfigFromKeys(kv, rc, nullptr) && P[rc.pattern].name == L"Clifford Attractor" && rc.colors[0] == 0x112233,
          L"a preset naming a retired pattern did not open its successor with its colours");
    }
    // every collection filter shows exactly its own patterns (IRIX Classics used to show everything)
    for (int h = 0; h < (int)P.size(); h++) {
        if (P[h].kind != K_HEADER) continue;
        g.category = h + 1; g.searchText.clear(); computeRows();
        int n = 0, want = 0;
        for (int r : g_rows) if (P[r].kind != K_HEADER) { n++; T(P[r].category == h, L"%ls shows %ls", P[h].name.c_str(), P[r].name.c_str()); }
        for (auto& p : P) if (p.kind != K_HEADER && p.category == h) want++;
        T(n == want && n > 0, L"collection %ls lists %d of its %d patterns", P[h].name.c_str(), n, want);
    }
    g.category = 0; computeRows();
    // ---- 2.4 rendering: antialiasing, Transform settings, the sgi wordmark, the Transform tab
    {
        auto colours = [](const Image& im) { std::vector<uint32_t> v(im.px); std::sort(v.begin(), v.end()); return (int)(std::unique(v.begin(), v.end()) - v.begin()); };
        int conf = FindPattern(L"Confetti");
        Config c = DefaultConfig(conf);
        c.smoothing = SM_OFF;
        auto hard = RenderTile(c);
        c.smoothing = SM_NORMAL;
        auto soft = RenderTile(c);
        T(hard && soft && colours(*hard) <= 4 && colours(*soft) > 8, L"smoothing does not antialias the edges of a flat-colour pattern (%d / %d colours)",
          hard ? colours(*hard) : -1, soft ? colours(*soft) : -1);
        // quarter turns and mirrors
        Config r = DefaultConfig(FindPattern(L"Granite"));
        auto base = RenderTile(r);
        r.rotate = 1;
        auto rot = RenderTile(r);
        bool ok = base && rot && rot->w == base->h && rot->h == base->w;
        for (int y = 0; ok && y < base->h; y += 7)
            for (int x = 0; ok && x < base->w; x += 7) ok = rot->px[(size_t)x * rot->w + (base->h - 1 - y)] == base->px[(size_t)y * base->w + x];
        T(ok, L"a quarter turn does not rotate the tile clockwise");
        r.rotate = 0; r.mirrorX = 1;
        auto mir = RenderTile(r);
        ok = mir && mir->px[0] == base->px[(size_t)base->w - 1];
        T(ok, L"Mirror Left-Right does not flip the tile");
        r.mirrorX = 0; r.offsetX = 0.5f;
        auto sh = RenderTile(r);
        int dx = (int)std::lround(0.5f * base->w);
        ok = sh && sh->px[(size_t)dx] == base->px[0];
        T(ok, L"Shift Across does not move the tile");
        // scenes turn with the display: a quarter-turned scene keeps the display's size
        Config sc = DefaultConfig(FindPattern(L"Wire Mesh"));
        sc.rotate = 1;
        Image si;
        RenderDisplay(sc, si, 320, 180);
        T(si.w == 320 && si.h == 180, L"a turned scene came back %dx%d", si.w, si.h);
        // balance / invert move a gradient-style pattern's colours; round trip of every new key
        Config bc = DefaultConfig(FindPattern(L"Fractal Noise"));
        bc.balance = 60; bc.invert = 1; bc.rotate = 3; bc.mirrorY = 1; bc.offsetX = 0.25f; bc.offsetY = 0.75f; bc.smoothing = SM_HIGH; bc.params[3] = 2;
        Config back;
        T(ConfigFromKeys(ParseIni(L"[x]\r\n" + ConfigKeysText(bc))[0].kv, back, nullptr) && back == bc, L"Transform settings do not survive a save and load");
        Config plain = DefaultConfig(FindPattern(L"Fractal Noise"));
        T(ConfigKey(plain) != ConfigKey(bc), L"Transform settings are not part of the render key");
        // the IRIX backdrops: the sgi wordmark (cropped by SGI's own image) can be hidden
        int cit = FindPattern(L"Citrus Citrus");
        Config lc = DefaultConfig(cit);
        T(P[cit].params.size() == 1 && lc.params[0] == 0, L"the backdrops should offer the wordmark, hidden by default");
        Image hidden, shown;
        RenderDisplay(lc, hidden, 960, 600);
        lc.params[0] = 1;
        RenderDisplay(lc, shown, 960, 600);
        int dh = 0, ds = 0;
        for (int y = 500; y < 600; y++)
            for (int x = 0; x < 960; x++) { dh += hidden.px[(size_t)y * 960 + x] != hidden.px[(size_t)599 * 960]; ds += shown.px[(size_t)y * 960 + x] != shown.px[(size_t)599 * 960]; }
        T(dh == 0 && ds > 500, L"the sgi wordmark option does not work (%d / %d pixels)", dh, ds);
        // a 2.3 state file remembered the Adjust tab as 2: it is now 3 (Transform came in between)
        std::wstring sp = AppDataDir() + L"\\state.txt", keep;
        bool had = ReadTextFile(sp, keep);
        WriteTextFile(sp, L"[General]\r\nVersion=3\r\nSelected=Bulge\r\nTab=2\r\n");
        LoadState();
        T(g.tab == TAB_ADJUST, L"the saved tab of a 2.3 state file was not moved past the new Transform tab");
        WriteTextFile(sp, L"[General]\r\nVersion=4\r\nSelected=Bulge\r\nTab=2\r\n");
        LoadState();
        T(g.tab == TAB_TRANSFORM, L"a 2.4 state file's tab was moved");
        if (had) WriteTextFile(sp, keep); else DeleteFileW(sp.c_str());
        LoadState();
        g.tab = TAB_COLORS;
        Note(L"2.4: smoothing, quarter turns, mirrors, shifts, balance, the sgi wordmark and the Transform tab checked");
    }
    // unique names (presets store pattern names)
    for (size_t i = 0; i < P.size(); i++)
        for (size_t j = i + 1; j < P.size(); j++)
            if (P[i].kind != K_HEADER && P[j].kind != K_HEADER) T(_wcsicmp(P[i].name.c_str(), P[j].name.c_str()) != 0, L"duplicate pattern name %ls", P[i].name.c_str());
    // 90s neon palettes present
    int neon = 0;
    for (auto& p : BuiltinPalettes()) if (p.group == L"90s Neon") neon++;
    T(neon >= 8, L"90s Neon palette group has only %d palettes", neon);
    Note(L"90s Neon palettes: %d", neon);

    // ---- name and version: header label, About box, window menu, and the exe's own version
    // resource and manifest agree with appinfo.h
    {
        layoutNow();
        Wd* v = findW(L"version");
        T(v && v->label.find(APP_VERSION) != std::wstring::npos, L"the header does not show the version");
        HMENU sm = GetSystemMenu(g.main, FALSE);
        T(sm && GetMenuState(sm, kIdmAbout, MF_BYCOMMAND) != (UINT)-1, L"the window menu has no About item");
        int before = CrashRecoveredCount();
        g_stubAnswer = 1;   // Close
        SendMessageW(g.main, WM_SYSCOMMAND, kIdmAbout, 0);
        showHelp();
        g_stubAnswer = 0;   // About... from Help, then Open Settings Folder (skipped in the self test)
        showHelp();
        T(CrashRecoveredCount() == before, L"About / Help raised an internal error");
        auto resourceHas = [](LPCWSTR type, const void* needle, size_t len) {
            HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(1), type);
            if (!r) return false;
            const BYTE* d = (const BYTE*)LockResource(LoadResource(nullptr, r));
            DWORD n = SizeofResource(nullptr, r);
            for (DWORD i = 0; d && i + len <= n; i++) if (memcmp(d + i, needle, len) == 0) return true;
            return false;
        };
        std::wstring ver = APP_VERSION, name = APP_NAME;
        T(resourceHas(RT_VERSION, ver.c_str(), ver.size() * 2), L"the version resource does not say %ls", APP_VERSION);
        T(resourceHas(RT_VERSION, name.c_str(), name.size() * 2), L"the version resource does not say %ls", APP_NAME);
        std::string mver;
        for (wchar_t ch : ver) mver += (char)ch;
        mver = "version=\"" + mver + ".0\"";
        T(resourceHas(RT_MANIFEST, mver.data(), mver.size()), L"the manifest version is not %ls.0", APP_VERSION);
        Note(L"version %ls shown in the header, the About box (window menu and Help) and the exe properties", APP_VERSION);
    }

    // ---- themes: every IRIX scheme recolours the panel, the Theme Options window and the menus,
    // and the choice survives a restart
    {
        int before = CrashRecoveredCount(), keep = CurrentScheme();
        wchar_t ev[MAX_PATH] = L"";
        std::wstring shots = GetEnvironmentVariableW(L"IRIXBG_THEMESHOTS", ev, MAX_PATH) ? ev : L"";
        T(SchemeCount() >= 20, L"only %d themes", SchemeCount());
        T(FindScheme(L"IndigoMagic") == 0 && FindScheme(L"Lascaux") == 0 && FindScheme(L"Key West") == FindScheme(L"KeyWest") &&
              FindScheme(L"VanGogh") == FindScheme(L"Buckingham") && FindScheme(L"whistler") == FindScheme(L"Gotham") && FindScheme(L"Rio") > 0 &&
              FindScheme(L"No Such Scheme") == -1,
          L"IRIX scheme names and aliases do not resolve");
        layoutNow();
        if (Wd* w = findW(L"theme")) activate(*w);
        T(ThemeOptionsWindow() != nullptr, L"Theme Options did not open from its button");
        auto snap = [&](const std::wstring& name, bool themeWin) {
            if (shots.empty()) return;
            Image img;
            if (themeWin) { if (!ThemeOptionsCapture(img)) return; }
            else {
                GdiFlush();
                img.w = g_bb.w; img.h = g_bb.h;
                img.px.assign(g_bb.bits, g_bb.bits + (size_t)img.w * img.h);
                for (auto& v : img.px) v &= 0xffffff;
            }
            SaveImageFile(img, shots + L"\\" + name + L".png", nullptr);
        };
        for (int i = 0; i < SchemeCount(); i++) {
            ApplyTheme(i);
            T(CurrentScheme() == i, L"theme %ls was not applied", Scheme(i).name);
            HWND tw = ThemeOptionsWindow();
            if (tw) {
                SendMessageW(tw, WM_KEYDOWN, VK_DOWN, 0);
                SendMessageW(tw, WM_KEYDOWN, VK_UP, 0);
            }
            HDC dc = GetDC(g.main);
            paintMain(g.main, dc);
            ReleaseDC(g.main, dc);
            snap(std::wstring(L"panel_") + Scheme(i).key, false);
            snap(std::wstring(L"options_") + Scheme(i).key, true);
            T(std::fabs(Brightness(C::text) - Brightness(C::body)) > 0.3f, L"theme %ls: text does not stand out from the window", Scheme(i).name);
            T(std::fabs(Brightness(C::fieldText) - Brightness(C::field)) > 0.3f, L"theme %ls: text-field text is unreadable", Scheme(i).name);
            SaveState();
            std::wstring st;
            ReadTextFile(AppDataDir() + L"\\state.txt", st);
            T(st.find(std::wstring(L"Theme=") + Scheme(i).key) != std::wstring::npos, L"theme %ls was not saved", Scheme(i).name);
        }
        // the choice is read back at start-up (aliases too)
        ApplyTheme(FindScheme(L"Inverness"));
        SetScheme(0);
        LoadState();
        T(CurrentScheme() == FindScheme(L"Inverness"), L"the saved theme was not restored at start-up");
        // Theme Options window: list clicks, double-click applies, Revert, keyboard, scrolling
        if (HWND tw = ThemeOptionsWindow()) {
            RECT rc;
            GetClientRect(tw, &rc);
            for (int k = 0; k < 40; k++) {
                LPARAM at = MAKELPARAM(rc.right * (k % 8) / 8 + S(20), rc.bottom * (k / 8) / 5 + S(40));
                SendMessageW(tw, WM_LBUTTONDOWN, MK_LBUTTON, at);
                SendMessageW(tw, WM_LBUTTONUP, 0, at);
                SendMessageW(tw, WM_MOUSEWHEEL, MAKEWPARAM(0, (k & 1 ? 1 : -1) * WHEEL_DELTA), at);
            }
            SendMessageW(tw, WM_KEYDOWN, VK_END, 0);
            SendMessageW(tw, WM_KEYDOWN, VK_RETURN, 0);
            T(CurrentScheme() == SchemeCount() - 1, L"Enter in Theme Options did not apply the highlighted scheme");
            SendMessageW(tw, WM_KEYDOWN, VK_HOME, 0);
            SendMessageW(tw, WM_KEYDOWN, VK_RETURN, 0);
            T(CurrentScheme() == 0, L"Theme Options: Home + Enter did not apply Indigo Magic");
            SendMessageW(tw, WM_KEYDOWN, VK_ESCAPE, 0);
        }
        T(ThemeOptionsWindow() == nullptr, L"Esc did not close Theme Options");
        // real mouse clicks on a row and on Apply / Revert / Close (keyboard alone missed a bug
        // where none of the three buttons responded to the mouse)
        {
            OpenThemeOptions();
            HWND tw = ThemeOptionsWindow();
            auto clickAt = [&](RECT r) {
                LPARAM at = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
                SendMessageW(tw, WM_MOUSEMOVE, 0, at);
                SendMessageW(tw, WM_LBUTTONDOWN, MK_LBUTTON, at);
                SendMessageW(tw, WM_MOUSEMOVE, MK_LBUTTON, at);
                SendMessageW(tw, WM_LBUTTONUP, 0, at);
            };
            int start = CurrentScheme(), want = FindScheme(L"Sargent");
            clickAt(ThemeOptionsRow(want));
            T(ThemeOptionsSelection() == want && CurrentScheme() == start, L"clicking a scheme should only preview it");
            clickAt(ThemeOptionsButton(0));
            T(CurrentScheme() == want, L"clicking Apply did not apply the scheme");
            clickAt(ThemeOptionsButton(1));
            T(CurrentScheme() == start, L"clicking Revert did not go back");
            // a press dragged off the button and released elsewhere does nothing
            RECT b = ThemeOptionsButton(2);
            SendMessageW(tw, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(b.left + 2, b.top + 2));
            SendMessageW(tw, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(b.left - S(200), b.top - S(100)));
            SendMessageW(tw, WM_LBUTTONUP, 0, MAKELPARAM(b.left - S(200), b.top - S(100)));
            T(ThemeOptionsWindow() != nullptr, L"releasing outside Close still closed Theme Options");
            clickAt(ThemeOptionsButton(2));
            T(ThemeOptionsWindow() == nullptr, L"clicking Close did not close Theme Options");
        }
        // Ctrl+T and the window menu open it again
        BYTE ks[256];
        GetKeyboardState(ks);
        ks[VK_CONTROL] = ks[VK_LCONTROL] = 0x80;
        SetKeyboardState(ks);
        SendMessageW(g.main, WM_KEYDOWN, 'T', 0);
        ks[VK_CONTROL] = ks[VK_LCONTROL] = 0;
        SetKeyboardState(ks);
        T(ThemeOptionsWindow() != nullptr, L"Ctrl+T did not open Theme Options");
        CloseThemeOptions();
        SendMessageW(g.main, WM_SYSCOMMAND, kIdmTheme, 0);
        T(ThemeOptionsWindow() != nullptr, L"the window menu's Theme Options item did not open it");
        CloseThemeOptions();
        // owner-drawn menus measure and draw in every scheme
        for (int i = 0; i < SchemeCount(); i++) {
            SetScheme(i);
            HMENU m = CreatePopupMenu(), sub = CreatePopupMenu();
            AppendMenuW(m, MF_STRING | MF_CHECKED, 1, L"Item &One\tCtrl+1");
            AppendMenuW(m, MF_STRING | MF_GRAYED, 2, L"Fish && Chips");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(sub, MF_STRING, 3, L"Three");
            AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, L"More");
            ThemeMenu(m);
            BackBuf mb;
            mb.ensure(S(300), S(40));
            for (HMENU mm : {m, sub})
                for (int k = 0; k < GetMenuItemCount(mm); k++) {
                    MENUITEMINFOW it{sizeof(it)};
                    it.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_ID | MIIM_STATE;
                    GetMenuItemInfoW(mm, k, TRUE, &it);
                    T((it.fType & MFT_OWNERDRAW) && it.dwItemData, L"menu item %d is not owner-drawn", k);
                    MEASUREITEMSTRUCT mi{ODT_MENU, 0, it.wID, 0, 0, it.dwItemData};
                    T(MenuMeasure(&mi) && mi.itemWidth > 0 && mi.itemHeight > 0, L"menu item %d did not measure", k);
                    for (UINT stt : {0u, (UINT)ODS_SELECTED, (UINT)(ODS_CHECKED | ODS_SELECTED), (UINT)ODS_GRAYED}) {
                        DRAWITEMSTRUCT di{ODT_MENU, 0, it.wID, ODA_DRAWENTIRE, stt, (HWND)mm, mb.dc, RECT{0, 0, (LONG)mi.itemWidth, (LONG)mi.itemHeight}, it.dwItemData};
                        int sv = SaveDC(mb.dc);
                        T(MenuDraw(&di), L"menu item %d did not draw", k);
                        RestoreDC(mb.dc, sv);
                    }
                }
            DestroyMenu(m);
        }
        SetScheme(keep);
        ApplyTheme(keep);
        T(CrashRecoveredCount() == before, L"themes raised %d internal errors (see crash.log)", CrashRecoveredCount() - before);
        Note(L"themes: %d IRIX schemes recolour the panel, Theme Options and menus, and are saved and restored", SchemeCount());
    }

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"irixbg_selftest";
    CreateDirectoryW(dir.c_str(), nullptr);

    // ---- every control on every pattern
    int actions = 0;
    for (int i = 0; i < (int)P.size(); i++) {
        if (P[i].kind == K_HEADER) continue;
        g.sel = i;
        g.cfg[i] = DefaultConfig(i);
        g.status.clear();
        double tp = nowMs();
        exerciseWidgets(i, actions);
        Note(L"%-24ls %6.0f ms", P[i].name.c_str(), nowMs() - tp);
        // render the result of all that fiddling (tile or display) to prove it is still drawable
        Image img;
        if (IsTiled(P[i].kind)) { auto t = RenderTile(Cur()); T(t && t->w > 0 && t->px.size() == (size_t)t->w * t->h, L"%ls: tile render", P[i].name.c_str()); }
        else { RenderDisplay(Cur(), img, 160, 90); T(img.w == 160 && img.h == 90, L"%ls: display render", P[i].name.c_str()); }
        g.cfg[i] = DefaultConfig(i);
    }
    Note(L"widget actions executed: %d", actions);

    // ---- randomizer honours locks, fills every slot, produces distinct colours
    {
        int pat = FindPattern(L"Bulge");
        g.sel = pat;
        Cur() = DefaultConfig(pat);
        g.lock[0] = true; g.lock[1] = g.lock[2] = g.lock[3] = false;
        uint32_t keep = Cur().colors[0];
        int distinctOk = 0;
        for (int hm = 0; hm < HM_COUNT; hm++) {
            g.harmony = hm;
            randomizePalette();
            T(Cur().colors[0] == keep, L"randomize (%ls) changed a locked colour", HarmonyName(hm));
            std::vector<uint32_t> v(Cur().colors, Cur().colors + 4);
            std::sort(v.begin(), v.end());
            if (std::unique(v.begin(), v.end()) == v.end()) distinctOk++;
        }
        T(distinctOk >= HM_COUNT - 1, L"random palettes repeated colours in %d harmonies", HM_COUNT - distinctOk);
        g.lock[0] = false;
        uint32_t before = Cur().colors[2];
        randomizeSlot(2);
        T(Cur().colors[2] != before, L"randomize single colour did nothing");
        g.lock[1] = true;
        before = Cur().colors[1];
        randomizeSlot(1);
        T(Cur().colors[1] == before, L"randomize single colour ignored its lock");
        g.lock[1] = false;
    }

    // ---- undo / redo
    {
        g.sel = FindPattern(L"Bulge");
        Cur() = DefaultConfig(g.sel);
        g.undo.clear(); g.redo.clear();
        Config a = Cur();
        Edit(); Cur().colors[0] = 0x123456;
        Config b = Cur();
        T(Undo() && Cur() == a, L"undo did not restore");
        T(Redo() && Cur() == b, L"redo did not re-apply");
        int other = FindPattern(L"Granite");
        selectPattern(other);
        T(Undo() && g.sel == FindPattern(L"Bulge"), L"undo did not restore the selection");
    }

    // ---- presets: save, exact restore, update, rename, export, import, delete
    {
        g_stubAnswer = 0;
        int pat = -1;
        for (int i = 0; i < (int)P.size(); i++) if (P[i].kind == K_DSCENE && !P[i].params.empty()) { pat = i; break; }
        g.sel = pat;
        Config c = DefaultConfig(pat);
        c.colors[1] = 0x00ff7f; c.seed = 987654321; c.params[0] = P[pat].params[0].min; c.shading = 2; c.rampRepeat = 3; c.rampOffset = 0.37f;
        c.rampMirror = 1; c.adj.hue = -33; c.adj.calm = 17; c.adj.textTone = TT_LIGHT; c.adj.textStrength = 44;
        SanitizeConfig(c);
        Cur() = c;
        g_stubText = L"Selftest Preset";
        savePresetNew();
        int k = FindPreset(g.presets, L"Selftest Preset");
        T(k >= 0, L"preset was not saved");
        if (k >= 0) {
            Cur() = DefaultConfig(pat);
            g.sel = FindPattern(L"Bulge");
            loadPreset(k);
            T(g.sel == pat && Cur() == c, L"preset did not restore exactly");
            g.presetSel = k;
            Cur().colors[0] = 0x010203;
            updatePreset();
            k = FindPreset(g.presets, L"Selftest Preset");
            T(k >= 0 && g.presets[k].cfg.colors[0] == 0x010203, L"preset update failed");
            g_stubText = L"Selftest Renamed";
            g.presetSel = k;
            renamePreset();
            T(FindPreset(g.presets, L"Selftest Renamed") >= 0 && FindPreset(g.presets, L"Selftest Preset") < 0, L"preset rename failed");
            g.presetSel = FindPreset(g.presets, L"Selftest Renamed");
            g_stubPath = dir + L"\\exported.irixbg";
            exportPreset();
            T(fileOk(g_stubPath, 50), L"preset export file missing");
            Config expected = g.presets[g.presetSel].cfg;
            deletePreset();
            T(FindPreset(g.presets, L"Selftest Renamed") < 0, L"preset delete failed");
            importPreset();
            int ki = FindPreset(g.presets, L"Selftest Renamed");
            T(ki >= 0 && g.presets[ki].cfg == expected && Cur() == expected, L"preset import did not round-trip exactly");
            g.presetSel = ki;
            deletePreset();
        }
        g_stubText.clear();
    }

    // ---- palettes: save, apply from library, export/import .gpl, delete
    {
        g.sel = FindPattern(L"Bulge");
        Cur() = DefaultConfig(g.sel);
        size_t before = g.userPalettes.size();
        Cur().colors[0] = 0xff2d95; Cur().colors[1] = 0x00d4c8; Cur().colors[2] = 0xffe14a; Cur().colors[3] = 0x120a1e;
        g_stubText = L"Selftest Palette";
        savePaletteAs();
        T(g.userPalettes.size() == before + 1, L"palette save failed");
        T(LoadUserPalettes().size() == before + 1, L"palette file not written");
        g_stubPath = dir + L"\\pal.gpl";
        exportPalette();
        T(fileOk(g_stubPath, 20), L"palette export missing");
        Cur() = DefaultConfig(g.sel);
        importPalette();
        std::vector<uint32_t> got(Cur().colors, Cur().colors + 4);
        std::sort(got.begin(), got.end());
        std::vector<uint32_t> want = {0xff2d95, 0x00d4c8, 0xffe14a, 0x120a1e};
        std::sort(want.begin(), want.end());
        T(got == want, L"palette import did not restore the colours");
        // apply every built-in palette to every kind of pattern without breaking configs
        for (int pat : {FindPattern(L"Bulge"), FindPattern(L"Linen"), FindPattern(L"Solid"), FindPattern(L"Twilight")}) {
            if (pat < 0) continue;
            g.sel = pat;
            for (auto& p : BuiltinPalettes()) { applyPalette(p.c, p.name); T(configValid(Cur()), L"palette %ls broke %ls", p.name.c_str(), P[pat].name.c_str()); }
        }
        // remove everything this test added (the saved palette and the one imported from the .gpl)
        for (int i = (int)g.userPalettes.size() - 1; i >= (int)before; i--) deleteUserPalette(i);
        T(LoadUserPalettes().size() == before, L"palette delete failed");
        g_stubText.clear();
    }

    // ---- image export at every size option (tiled and per-display patterns)
    {
        int tiledPat = FindPattern(L"Bulge"), dispPat = -1;
        for (int i = 0; i < (int)P.size(); i++) if (P[i].kind == K_DSCENE) { dispPat = i; break; }
        for (int pat : {tiledPat, dispPat}) {
            g.sel = pat;
            Cur() = DefaultConfig(pat);
            auto sizes = exportSizes();
            for (int s = 0; s < (int)sizes.size(); s++) {
                if (sizes[s].code == 2 && !IsTiled(P[pat].kind)) continue;
                if (sizes[s].w * (long long)sizes[s].h > 3840LL * 2160) continue;   // keep the run short
                g_expSel = s;
                g_stubPath = dir + L"\\export_" + std::to_wstring(pat) + L"_" + std::to_wstring(s) + (s % 2 ? L".bmp" : L".png");
                exportImage();
                T(fileOk(g_stubPath, 100), L"export %ls of %ls failed: %ls", sizes[s].label.c_str(), P[pat].name.c_str(), g.status.c_str());
            }
        }
        g_stubText = L"640 x 360";
        T(pickCustomSize() && g_customW == 640 && g_customH == 360, L"custom size parse failed");
        g_stubText = L"nonsense";
        T(!pickCustomSize(), L"custom size accepted nonsense");
        g_stubText.clear();
    }

    // ---- make text-friendly reaches Good on every pattern
    {
        int good = 0, total = 0;
        for (int i = 0; i < (int)P.size(); i++) {
            if (P[i].kind == K_HEADER) continue;
            Config c = DefaultConfig(i);
            int s = MakeTextFriendly(c, TT_LIGHT);
            total++;
            if (s >= 2) good++;
        }
        T(good == total, L"text-friendly reached Good on %d/%d", good, total);
        Note(L"text-friendly: Good on %d/%d patterns", good, total);
    }

    // ---- apply wallpaper (writes the bitmap, registry and the applied record)
    {
        g.sel = FindPattern(L"Bulge");
        Cur() = DefaultConfig(g.sel);
        doApply();
        Config ap;
        T(LoadApplied(ap) && ap == Cur(), L"apply did not record the applied background: %ls", g.status.c_str());
        T(g.status.find(L"applied") != std::wstring::npos, L"apply status: %ls", g.status.c_str());
    }

    // ---- watcher registration toggles
    {
        bool was = g.watch;
        SetWatch(true);
        T(WatchRegistered(), L"watch did not register");
        SetWatch(false);
        T(!WatchRegistered(), L"watch did not unregister");
        if (was) SetWatch(true);
    }

    // ---- state persistence round trip
    {
        int pat = FindPattern(L"Granite");
        g.sel = pat;
        Cur().colors[2] = 0x445566;
        Config keep = Cur();
        SaveState();
        Cur() = DefaultConfig(pat);
        g.sel = 1;
        LoadState();
        T(g.sel == pat && Cur() == keep, L"state did not persist");
        g.cfg[pat] = DefaultConfig(pat);
        SaveState();
    }

    // ---- Pixel Size defaults: each pattern's own best fit, scaled with the display for pixel art,
    // never spread from v1's single setting, and a 2.0/2.1 state file carrying that spread value
    // is repaired once (deliberate values on a few patterns survive).
    {
        int dbl = 0, pix = 0;
        for (size_t i = 0; i < P.size(); i++) {
            if (!IsTiled(P[i].kind) || P[i].kind == K_SOLID) continue;
            int z = DefaultConfig((int)i).zoom;
            T(z >= 1 && z <= 4, L"%ls default pixel size %d", P[i].name.c_str(), z);
            int want = std::clamp((int)(P[i].defZoom * DisplayScale() + 0.5), 1, 4);
            T(z == want, L"%ls default pixel size %d, expected %d", P[i].name.c_str(), z, want);
            if (P[i].defZoom == 2) dbl++;
            if (P[i].pixelArt) pix++;
        }
        T(FindPattern(L"Granite") >= 0 && P[FindPattern(L"Granite")].defZoom == 1, L"Granite should be 1x at 100%");
        T(FindPattern(L"Houndstooth") >= 0 && P[FindPattern(L"Houndstooth")].defZoom == 2, L"Houndstooth should be 2x at 100%");
        {   // generated tiles are redrawn at their Scale (never blown up pixel by pixel)
            int dw = FindPattern(L"Domain Warp");
            Config c = DefaultConfig(dw);
            c.zoom = 3;
            auto t = RenderTile(c);
            T(dw >= 0 && !P[dw].pixelArt && t && t->w == EffectiveTileSize(c) * 3 && TileZoom(c) == 1, L"a generated tile at 3x is not drawn natively at 3x");
            Config irx = DefaultConfig(FindPattern(L"Granite"));
            irx.zoom = 3;
            auto t2 = RenderTile(irx);
            irx.zoom = 1;
            auto t1 = RenderTile(irx);
            T(t2 && t1 && t2->w == t1->w && TileZoom(irx) == 1 && (irx.zoom = 3, TileZoom(irx) == 3), L"IRIX art must keep its own pixels (enlarged when laid out)");
        }
        Note(L"pixel size: %d patterns default to 2x at 100%%, %d pixel-art patterns follow the display scaling (%.2fx here)", dbl, pix, DisplayScale());

        std::wstring statePath = AppDataDir() + L"\\state.txt", iniPath = AppDataDir() + L"\\settings.ini";
        std::wstring iniBackup = iniPath + L".selftest";
        bool hadIni = CopyFileW(iniPath.c_str(), iniBackup.c_str(), FALSE) != FALSE;
        // an old (Version=2) state file: 30 patterns carry the spread 4x, two were set to 3x by hand
        std::wstring st = L"[General]\r\nVersion=2\r\nSelected=Granite\r\n";
        std::vector<int> spread, kept;
        int oldDefault = FindPattern(L"Houndstooth");   // stored at 1x, the old default for everything
        for (size_t i = 0; i < P.size() && (spread.size() < 30 || kept.size() < 2); i++) {
            if (!IsTiled(P[i].kind) || P[i].kind == K_SOLID || (int)i == oldDefault) continue;
            Config c = DefaultConfig((int)i);
            if (spread.size() < 30) { c.zoom = 4; spread.push_back((int)i); }
            else { c.zoom = 3; kept.push_back((int)i); }
            st += L"\r\n[Pattern " + P[i].name + L"]\r\n" + ConfigKeysText(c);
        }
        {
            Config c = DefaultConfig(oldDefault);
            c.zoom = 1;
            c.colors[0] = 0x123456;   // stored because the colour was changed
            st += L"\r\n[Pattern " + P[oldDefault].name + L"]\r\n" + ConfigKeysText(c);
        }
        Config ap = DefaultConfig(FindPattern(L"Granite"));
        ap.zoom = 4;
        st += L"\r\n[Applied]\r\n" + ConfigKeysText(ap);
        WriteTextFile(statePath, st);
        LoadState();
        bool spreadFixed = true, keptOk = true;
        for (int i : spread) spreadFixed &= g.cfg[i].zoom == DefaultZoom(i);
        for (int i : kept) keptOk &= g.cfg[i].zoom == 3;
        T(spreadFixed, L"spread pixel size was not reset to each pattern's default");
        T(keptOk, L"pixel sizes set on a few patterns did not survive the repair");
        T(g.cfg[oldDefault].zoom == DefaultZoom(oldDefault), L"a pattern stored at the old 1x default did not get its new default");
        T(g.hasApplied && g.applied.zoom == 4, L"the applied wallpaper's pixel size must not change");
        T(g.status.find(L"Pixel Size") != std::wstring::npos, L"no status message after the pixel size repair");
        SaveState();   // now version 3: the repair must not run again
        for (int i : kept) g.cfg[i].zoom = 3;
        g.cfg[spread[0]].zoom = 4;   // chosen by hand after the repair
        SaveState();
        LoadState();
        T(g.cfg[spread[0]].zoom == 4, L"a version-3 state file was 'repaired' again");
        // v1 settings.ini with a global PixelSize=4 and no state.txt: nothing is spread
        DeleteFileW(statePath.c_str());
        WriteTextFile(iniPath, L"[General]\r\nSelected=Bulge\r\nPixelSize=4\r\n\r\n[Pattern Granite]\r\nColors=#112233,#445566,#778899,#AABBCC\r\nSeed=1\r\nFit=0\r\n");
        LoadState();
        int g4 = 0;
        for (size_t i = 0; i < P.size(); i++) if (IsTiled(P[i].kind) && P[i].kind != K_SOLID && g.cfg[i].zoom != DefaultZoom((int)i)) g4++;
        T(g4 == 0, L"v1 migration changed the pixel size of %d patterns", g4);
        T(g.cfg[FindPattern(L"Granite")].colors[0] == 0x112233, L"v1 migration lost the pattern colours");
        DeleteFileW(iniPath.c_str());
        if (hadIni) MoveFileExW(iniBackup.c_str(), iniPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        // back to a clean slate for the rest of the test
        for (size_t i = 0; i < P.size(); i++) if (P[i].kind != K_HEADER) g.cfg[i] = DefaultConfig((int)i);
        g.status.clear();
        SaveState();
    }

    // ---- filtering and search
    {
        g.category = 0; g.searchText = L"neon"; computeRows();
        bool any = false;
        for (int r : g_rows) if (P[r].kind != K_HEADER) any = true;
        T(any, L"search for 'neon' found nothing");
        g.searchText = L"zzzzqqq"; computeRows();
        T(g_rows.empty(), L"search for nonsense found rows");
        g.searchText.clear();
        g.category = -1; computeRows();
        bool allTf = true;
        for (int r : g_rows) if (P[r].kind != K_HEADER && !P[r].textFriendly) allTf = false;
        T(allTf && !g_rows.empty(), L"text-friendly filter wrong");
        g.category = 0; computeRows();
    }

    // ---- window states: minimise / restore / maximise. While minimised, Windows gives this
    // self-drawn window a 160x31 client area and still paints it; v2.0 crashed right there.
    {
        auto pump = [](int ms) {
            double t0 = nowMs();
            MSG m;
            while (nowMs() - t0 < ms) {
                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
                Sleep(5);
            }
        };
        int before = CrashRecoveredCount();
        std::vector<int> pats;
        for (int k : {K_TILE4, K_FULL, K_DSCENE, K_GRAD, K_DTILE})
            for (int i = 0; i < (int)P.size(); i++) if (P[i].kind == k) { pats.push_back(i); break; }
        RECT rc{};
        for (int pat : pats) {
            g.sel = pat;
            ShowWindow(g.main, SW_MINIMIZE);
            pump(250);
            GetClientRect(g.main, &rc);
            T(IsIconic(g.main), L"window did not minimise");
            SendMessageW(g.main, WM_MOVE, 0, MAKELPARAM(-32000, -32000));
            InvalidateRect(g.main, nullptr, FALSE);
            UpdateWindow(g.main);
            HDC dc = GetDC(g.main);
            SendMessageW(g.main, WM_PAINT, 0, 0);
            try {
                paintMain(g.main, dc);   // and directly, at the minimised client size
            } catch (...) {
                T(false, L"painting the minimised window failed: %ls", CrashRecovered(L"self test minimise").c_str());
            }
            ReleaseDC(g.main, dc);
            PostMessageW(g.main, WM_APP_PREVIEW, 0, 0);
            PostMessageW(g.main, WM_APP_THUMB, 0, 0);
            pump(250);
            ShowWindow(g.main, SW_RESTORE);
            pump(250);
            ShowWindow(g.main, SW_MAXIMIZE);
            pump(250);
            ShowWindow(g.main, SW_RESTORE);
            pump(250);
            T(!IsIconic(g.main), L"window did not restore");
        }
        // the title-bar buttons themselves: press and release on maximize (twice) and minimize
        auto pressTb = [&](const wchar_t* which) {
            relayout(g.main);
            RECT r = wcscmp(which, L"max") == 0 ? L.f.maximize : L.f.minimize;
            LPARAM at = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
            SendMessageW(g.main, WM_MOUSEMOVE, 0, at);
            SendMessageW(g.main, WM_LBUTTONDOWN, MK_LBUTTON, at);
            SendMessageW(g.main, WM_LBUTTONUP, 0, at);
            pump(300);
        };
        pressTb(L"max");
        T(IsZoomed(g.main), L"title-bar maximize button did not maximise");
        {
            RECT wr;
            GetWindowRect(g.main, &wr);
            MONITORINFO mi{sizeof(mi)};
            GetMonitorInfoW(MonitorFromWindow(g.main, MONITOR_DEFAULTTONEAREST), &mi);
            T(EqualRect(&wr, &mi.rcWork), L"maximised window %ld,%ld-%ld,%ld is not the work area %ld,%ld-%ld,%ld", wr.left, wr.top, wr.right, wr.bottom,
              mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom);
        }
        pressTb(L"max");
        T(!IsZoomed(g.main) && !IsIconic(g.main), L"title-bar maximize button did not restore");
        SendMessageW(g.main, WM_NCLBUTTONDBLCLK, HTCAPTION, 0);
        pump(300);
        T(IsZoomed(g.main), L"double-clicking the title bar did not maximise");
        SendMessageW(g.main, WM_NCLBUTTONDBLCLK, HTCAPTION, 0);
        pump(300);
        T(!IsZoomed(g.main), L"double-clicking the title bar did not restore");
        pressTb(L"min");
        T(IsIconic(g.main), L"title-bar minimize button did not minimise");
        ShowWindow(g.main, SW_RESTORE);
        pump(250);
        {   // moving off a pressed button cancels it
            relayout(g.main);
            RECT r = L.f.maximize;
            SendMessageW(g.main, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2));
            SendMessageW(g.main, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(r.left - S(200), r.bottom + S(200)));
            SendMessageW(g.main, WM_LBUTTONUP, 0, MAKELPARAM(r.left - S(200), r.bottom + S(200)));
            pump(200);
            T(!IsZoomed(g.main), L"releasing away from the maximize button still maximised");
        }
        T(CrashRecoveredCount() == before, L"minimise/maximise/restore raised %d internal errors (see crash.log)", CrashRecoveredCount() - before);
        Note(L"window states: minimise, restore, maximise checked on %d backgrounds (minimised client %ldx%ld)", (int)pats.size(), rc.right, rc.bottom);
    }

    // ---- layout and paint at every UI scale and window size (including far below the minimum),
    // on every tab, with tiled, display, gradient and scene backgrounds
    {
        int before = CrashRecoveredCount(), paints = 0;
        std::vector<int> pats;
        for (int k : {K_TILE4, K_FULL, K_DSCENE, K_GRAD, K_DTILE})
            for (int i = 0; i < (int)P.size(); i++) if (P[i].kind == k) { pats.push_back(i); break; }
        static const int dpis[] = {72, 96, 120, 144, 168, 192, 240, 288};
        static const SIZE sizes[] = {{1, 1}, {160, 31}, {300, 200}, {480, 360}, {700, 500}, {960, 700}, {1100, 800}, {1366, 728}, {1920, 1032}, {2560, 1400}};
        int keepTab = g.tab, keepSel = g.sel;
        HDC dc = GetDC(g.main);
        for (int dpi : dpis)
            for (SIZE sz : sizes) {
                if ((double)sz.cx * sz.cy * dpi * dpi / (96.0 * 96.0) > 3.0e7) continue;   // keep the back buffer sane
                for (int t = 0; t < TAB_COUNT; t++)
                    for (int pat : pats) {
                        g_dpiOverride = dpi;
                        g.tab = t;
                        g.sel = pat;
                        g_testClient = SIZE{std::max<LONG>(1, sz.cx * dpi / 96), std::max<LONG>(1, sz.cy * dpi / 96)};
                        try {
                            paintMain(g.main, dc);
                            relayout(g.main);
                            for (int py = 0; py < g_testClient.cy; py += std::max<LONG>(1, g_testClient.cy / 12))
                                for (int px = 0; px < g_testClient.cx; px += std::max<LONG>(1, g_testClient.cx / 12)) hitKey(POINT{px, py});
                            paints++;
                        } catch (...) {
                            T(false, L"layout %ldx%ld at %d dpi, tab %d, %ls: %ls", g_testClient.cx, g_testClient.cy, dpi, t, P[pat].name.c_str(),
                              CrashRecovered(L"self test layout").c_str());
                        }
                    }
            }
        ReleaseDC(g.main, dc);
        g_dpiOverride = 0;
        g_testClient = SIZE{0, 0};
        g.tab = keepTab;
        g.sel = keepSel;
        for (int i = 0; i < 400 && PreviewPending(); i++) Sleep(25);   // let the render worker finish what the sweep asked for
        relayout(g.main);
        T(CrashRecoveredCount() == before, L"layout sweep raised %d internal errors (see crash.log)", CrashRecoveredCount() - before);
        Note(L"layout sweep: %d paints across %d UI scales", paints, (int)(sizeof(dpis) / sizeof(dpis[0])));
    }
    {
        wchar_t ev[32] = L"";
        int steps = GetEnvironmentVariableW(L"IRIXBG_MONKEY", ev, 32) ? _wtoi(ev) : 2500;
        uint32_t seed = GetEnvironmentVariableW(L"IRIXBG_MONKEY_SEED", ev, 32) ? (uint32_t)wcstoul(ev, nullptr, 10) : 20261015u;
        auto pump = [](int ms) {
            double t0 = nowMs();
            MSG m;
            do {
                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
                if (nowMs() - t0 < ms) Sleep(1);
            } while (nowMs() - t0 < ms);
        };
        g.status.clear();
        monkeyTest(std::max(0, steps), seed, pump);
    }
    T(CrashRecoveredCount() == 0, L"%d internal errors were recovered during the self test (see crash.log)", CrashRecoveredCount());

    for (int i = 0; i < 2; i++) { if (had[i]) WriteTextFile(files[i], saved[i]); else DeleteFileW(files[i].c_str()); }
    if (userHadApplied) ApplyWallpaper(userApplied, nullptr);   // put the user's own wallpaper back
    Note(L"RESULT: %d passed, %d failed", g_pass, g_fail);
    if (g_log) fclose(g_log);
    return g_fail;
}

// ---------------------------------------------------------------------------------------------
// --crash-test=fatal|recover|render|fault: deliberately fails, to check that crash.log works
static void crashTest(const std::wstring& args) {
    size_t p = args.find(L"--crash-test=");
    if (p == std::wstring::npos) return;
    std::wstring kind = args.substr(p + 13, args.find(L' ', p) == std::wstring::npos ? std::wstring::npos : args.find(L' ', p) - p - 13);
    CrashContext(L"crash test (%ls)", kind.c_str());
    if (kind == L"fatal") { std::vector<int> v; v.at(3) = 1; }
    if (kind == L"fault") { volatile int* z = nullptr; *z = 1; }
    if (kind == L"render") {   // an exception inside a render helper thread, caught by the caller
        try {
            ParallelFor(512, [](int y) { if (y == 300) throw std::runtime_error("crash test: render helper failed on row 300"); });
        } catch (...) {
            MessageBoxW(nullptr, (L"Recovered: " + CrashRecovered(L"crash test") + L"\n\nReport: " + CrashLogPath()).c_str(), L"Crash test", MB_OK);
        }
    }
    if (kind == L"recover") g_crashTestUi = true;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmd, int show) {
    g.inst = inst;
    std::wstring args = cmd ? cmd : L"";
    bool watch = args.find(L"--watch") != std::wstring::npos, apply = args.find(L"--apply") != std::wstring::npos;
    g_selftest = args.find(L"--selftest") != std::wstring::npos;
    CrashInit(watch ? L"watch" : apply ? L"apply" : g_selftest ? L"selftest" : L"panel", !watch && !apply && !g_selftest);
    CrashSetDescriber(describeState);
    if (watch) return RunWatcher();
    EngineInit();
    crashTest(args);
    if (apply) {
        Config c;
        if (LoadApplied(c)) return ApplyWallpaper(c, nullptr) ? 0 : 1;
        return 2;
    }
    if (!g_selftest) {
        if (HWND other = FindWindowW(kMainClass, nullptr)) {
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
            return 0;
        }
    }
    CrashContext(L"loading settings");
    try {
        LoadState();
    } catch (...) {
        // unreadable settings must never stop the program from starting: set them aside
        std::wstring sum = CrashRecovered(L"loading settings");
        std::wstring dir = AppDataDir(), aside = dir + L"\\state-unreadable.txt";
        MoveFileExW((dir + L"\\state.txt").c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING);
        g = App{};
        g.inst = inst;
        LoadState();
        g.status = L"Your saved settings could not be read and were set aside (state-unreadable.txt). " + sum;
    }
    CrashContext(L"");
    if (!g_selftest && g.watch) RefreshWatch();
    computeRows();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, 0);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);
    RegisterColorBrowser(inst);
    RegisterThemeOptions(inst);
    RegisterMotifDialogClass(inst);

    POINT origin{0, 0};
    HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96;
    typedef HRESULT(WINAPI * GDFM)(HMONITOR, int, UINT*, UINT*);
    if (HMODULE sh = LoadLibraryW(L"shcore.dll"))
        if (GDFM f = (GDFM)GetProcAddress(sh, "GetDpiForMonitor")) f(mon, 0, &dx, &dy);
    g_dpi = FitDpi(mon, (int)dx);
    int W = std::min<int>(S(1100), mi.rcWork.right - mi.rcWork.left), H = std::min<int>(S(800), mi.rcWork.bottom - mi.rcWork.top);
    int x = (mi.rcWork.left + mi.rcWork.right - W) / 2, y = (mi.rcWork.top + mi.rcWork.bottom - H) / 2;
    if (!g_selftest && g.winRect.right > g.winRect.left) {
        // where it was last time - if that is still on a connected display (fitToMonitor fixes the size)
        RECT r = g.winRect;
        if (HMONITOR m = MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {
            MONITORINFO mi2{sizeof(mi2)};
            RECT vis;
            if (GetMonitorInfoW(m, &mi2) && IntersectRect(&vis, &r, &mi2.rcWork) && vis.right - vis.left >= 120 && vis.bottom - vis.top >= 80) {
                x = r.left; y = r.top;
                W = r.right - r.left; H = r.bottom - r.top;
                mon = m;
            }
        }
    }
    HWND hw = CreateWindowExW(WS_EX_APPWINDOW, kMainClass, APP_NAME,
                              WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN, x, y, W, H, nullptr, nullptr, inst, nullptr);
    relayout(hw);
    g_pl.ensure(rowOfPattern(g.sel));
    if (g_selftest) {
        std::wstring logPath = L"selftest.log";
        size_t p = args.find(L"--selftest");
        // the log path is the next argument: "quoted with spaces" or a single word (not another --switch)
        std::wstring rest = args.substr(p + 10);
        size_t a = rest.find_first_not_of(L' ');
        if (a != std::wstring::npos && rest[a] == L'"') {
            size_t e = rest.find(L'"', a + 1);
            logPath = rest.substr(a + 1, e == std::wstring::npos ? std::wstring::npos : e - a - 1);
        } else if (a != std::wstring::npos && rest.compare(a, 2, L"--") != 0) {
            size_t e = rest.find(L' ', a);
            logPath = rest.substr(a, e == std::wstring::npos ? std::wstring::npos : e - a);
        }
        if (logPath.empty()) logPath = L"selftest.log";
        ShowWindow(hw, SW_SHOWNOACTIVATE);
        UpdateWindow(hw);
        int fails = args.find(L"--monkey") != std::wstring::npos ? runMonkeyOnly(logPath) : runSelfTest(logPath);
        DestroyWindow(hw);
        return fails;
    }
    fitToMonitor(hw);
    bool startMax = g.winMax && show != SW_SHOWMINIMIZED && show != SW_MINIMIZE && show != SW_SHOWMINNOACTIVE;
    ShowWindow(hw, startMax ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(hw);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsColorBrowserMessage(msg)) continue;
        if (msg.hwnd == g.search && msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_DOWN || msg.wParam == VK_UP || msg.wParam == VK_RETURN || msg.wParam == VK_PRIOR || msg.wParam == VK_NEXT) {
                SendMessageW(g.main, WM_KEYDOWN, msg.wParam, msg.lParam);
                continue;
            }
            if (msg.wParam == VK_ESCAPE) { SetWindowTextW(g.search, L""); SetFocus(g.main); continue; }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
