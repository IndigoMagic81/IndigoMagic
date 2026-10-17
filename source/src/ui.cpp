// Motif / 4Dwm style drawing helpers
#include "ui.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <memory>

int g_dpi = 96;
// The main panel needs kPanelMinW x kPanelMinH logical pixels. On a small or highly scaled screen
// (a 14" 1080p laptop at 150-175%, 1366x768 at 125%) that is more than the work area, which pushed
// Apply / Close and the status line off the bottom of the screen. The UI scale is then lowered just
// enough for the whole panel to fit (never below 75%); on screens with room it is the system scale.
int FitDpi(HMONITOR mon, int dpi) {
    MONITORINFO mi{sizeof(mi)};
    if (!mon || !GetMonitorInfoW(mon, &mi)) return dpi;
    int ww = mi.rcWork.right - mi.rcWork.left, wh = mi.rcWork.bottom - mi.rcWork.top;
    if (ww <= 0 || wh <= 0) return dpi;
    int fit = std::min(ww * 96 / kPanelMinW, wh * 96 / kPanelMinH);
    return std::max(std::min(dpi, fit), std::min(dpi, 72));
}
// Per-monitor DPI APIs exist from Windows 10 1607; they are looked up at run time so the program
// also starts on older Windows (which then uses the system DPI).
static UINT dpiForWindow(HWND h) {
    typedef UINT(WINAPI * Fn)(HWND);
    static Fn f = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (f) return f(h);
    HDC dc = GetDC(nullptr);
    UINT d = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return d;
}
int g_dpiOverride = 0;
void UseDpi(HWND h) {
    if (g_dpiOverride) { g_dpi = g_dpiOverride; return; }
    UINT d = h ? dpiForWindow(h) : 0;
    int dpi = d ? (int)d : 96;
    g_dpi = h ? FitDpi(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), dpi) : dpi;
}

static HFONT mkFont(int px, bool bold, bool italic, const wchar_t* face = L"Arial") {
    return CreateFontW(-S(px), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, italic, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
}
Fonts& F() {
    static std::map<int, Fonts> m;
    auto it = m.find(g_dpi);
    if (it != m.end()) return it->second;
    Fonts f;
    f.title = mkFont(15, true, true);
    f.bold = mkFont(15, true, false);
    f.body = mkFont(14, false, false);
    f.small = mkFont(12, false, false);
    f.list = mkFont(15, false, false);
    f.big = mkFont(30, true, false, L"Times New Roman");
    f.italic = mkFont(13, false, true);
    f.smallBold = mkFont(12, true, false);
    f.menu = mkFont(14, true, true);   // ObliqueLabelFont: IRIX menus are Helvetica bold oblique
    m[g_dpi] = f;
    return m[g_dpi];
}

// ---------------------------------------------------------------------------------------------
// Themes (IRIX desktop colour schemes)
// ---------------------------------------------------------------------------------------------
#include "schemes.inc"
int SchemeCount() { return kSchemeCount; }
const SchemeDef& Scheme(int i) { return kSchemes[std::clamp(i, 0, kSchemeCount - 1)]; }
int FindScheme(const std::wstring& name) {
    if (name.empty()) return -1;
    auto squash = [](std::wstring s) {   // "Key West", "keywest" and "KeyWest" are the same scheme
        std::wstring o;
        for (wchar_t ch : s) if (iswalnum(ch)) o += (wchar_t)towlower(ch);
        return o;
    };
    std::wstring want = squash(name);
    for (int i = 0; i < kSchemeCount; i++) {
        if (squash(kSchemes[i].name) == want) return i;
        std::wstring al = kSchemes[i].aliases;
        size_t a = 0;
        while (a <= al.size()) {
            size_t b = al.find(L'|', a);
            if (b == std::wstring::npos) b = al.size();
            if (squash(al.substr(a, b - a)) == want) return i;
            a = b + 1;
        }
    }
    return -1;
}

static int g_scheme = 0;
int CurrentScheme() { return g_scheme; }

COLORREF Mix(COLORREF a, COLORREF b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto m = [&](int x, int y) { return (int)std::lround(x + (y - x) * t); };
    return RGB(m(GetRValue(a), GetRValue(b)), m(GetGValue(a), GetGValue(b)), m(GetBValue(a), GetBValue(b)));
}
float Brightness(COLORREF c) {
    // Motif (Visual.c): 75% intensity + 25% luminosity
    float r = GetRValue(c) / 255.f, g = GetGValue(c) / 255.f, b = GetBValue(c) / 255.f;
    float intensity = (r + g + b) / 3, luminosity = 0.30f * r + 0.59f * g + 0.11f * b;
    return 0.75f * intensity + 0.25f * luminosity;
}
static float relLum(COLORREF c) {
    auto lin = [](int v) { float s = v / 255.f; return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(GetRValue(c)) + 0.7152f * lin(GetGValue(c)) + 0.0722f * lin(GetBValue(c));
}
static float contrast(COLORREF a, COLORREF b) {
    float x = relLum(a), y = relLum(b);
    if (x < y) std::swap(x, y);
    return (x + 0.05f) / (y + 0.05f);
}
LRESULT EditColors(HDC dc) {
    SetBkColor(dc, C::field);
    SetTextColor(dc, C::fieldText);
    SetDCBrushColor(dc, C::field);
    return (LRESULT)GetStockObject(DC_BRUSH);
}
COLORREF Readable(COLORREF want, COLORREF bg, float minRatio) {
    if (contrast(want, bg) >= minRatio) return want;
    return contrast(RGB(0, 0, 0), bg) >= contrast(RGB(255, 255, 255), bg) ? RGB(0, 0, 0) : RGB(255, 255, 255);
}

// The shadows of a widget come from its own background colour. Motif computes them from a
// brightness measure in three bands; SGI's "Indigo Magic" look draws lighter highlights and a
// softer bottom shadow than stock Motif. The factors below reproduce the shades measured on a
// real IRIX 6.5 screen (#bdbdbd button: #ededed top, #7e7e7e / #5c5c5c bottom; #d8d8d8 window:
// #f5f5f5 / #8f8f8f) and follow Motif's bands for very light and very dark colours.
Shade Shades(COLORREF c) {
    float br = Brightness(c);
    auto toward = [&](COLORREF to, float t) { return Mix(c, to, t); };
    auto scale = [&](float f) { return Mix(RGB(0, 0, 0), c, f); };
    Shade s;
    if (br > 0.93f) {          // near-white: Motif darkens both shadows
        s.light = scale(0.86f);
        s.dark = scale(0.56f);
        s.darker = scale(0.40f);
        s.armed = scale(0.85f);
    } else if (br < 0.12f) {   // near-black: both shadows are lighter than the colour
        s.light = toward(RGB(255, 255, 255), 0.55f);
        s.dark = toward(RGB(255, 255, 255), 0.30f);
        s.darker = toward(RGB(255, 255, 255), 0.18f);
        s.armed = toward(RGB(255, 255, 255), 0.15f);
    } else {
        s.light = toward(RGB(255, 255, 255), 0.36f + 0.5f * br);
        s.dark = scale(0.56f + 0.12f * br);
        s.darker = scale(0.40f + 0.10f * br);
        s.armed = scale(0.86f);
    }
    return s;
}
// 4Dwm shades its frame less steeply than Motif widgets (#c5c1aa frame: #e6e4da / #757264)
Shade WmShades(COLORREF c) {
    Shade s;
    if (Brightness(c) < 0.12f) {
        s.light = Mix(c, RGB(255, 255, 255), 0.56f);
        s.dark = Mix(c, RGB(255, 255, 255), 0.22f);
    } else {
        s.light = Mix(c, RGB(255, 255, 255), 0.56f);
        s.dark = Mix(RGB(0, 0, 0), c, 0.60f);
    }
    s.darker = Mix(RGB(0, 0, 0), c, 0.30f);
    s.armed = Mix(RGB(0, 0, 0), c, 0.86f);
    return s;
}

namespace C {
COLORREF body, light, dark, darker, face, faceLight, faceDark, faceDarker, faceArmed, list, listLight, listDark, listSelBg, listSelFg,
    trough, sb, sbLight, sbDark, sbDarker, sbArmed, text, insens, disabledText, field, fieldText, indicator, check, radio, drawing, fill,
    quickHelp, swatchBorder, wmFace, wmLight, wmDark, wmText, wmFaceI, wmLightI, wmDarkI, wmTextI;
bool darkScheme = false;
}
static HBRUSH g_menuBrush = nullptr;
void SetScheme(int i) {
    g_scheme = std::clamp(i, 0, kSchemeCount - 1);
    const SchemeDef& s = kSchemes[g_scheme];
    auto R = [&](int role) { return Ref(s.c[role]); };
    C::darkScheme = s.dark;
    C::body = R(SR_BASIC);
    Shade b = Shades(C::body);
    C::light = b.light; C::dark = b.dark; C::darker = b.darker;
    C::face = R(SR_BUTTON);
    Shade f = Shades(C::face);
    C::faceLight = f.light; C::faceDark = f.dark; C::faceDarker = f.darker; C::faceArmed = f.armed;
    C::list = R(SR_LIST);
    Shade l = Shades(C::list);
    C::listLight = l.light; C::listDark = l.dark;
    C::text = R(SR_TEXT);
    // Motif lists select in reverse video: foreground-coloured bar, list-coloured text
    C::listSelBg = C::text;
    C::listSelFg = C::list;
    C::trough = R(SR_TROUGH);
    C::sb = R(SR_SBCONTROL);
    Shade sb = Shades(C::sb);
    C::sbLight = sb.light; C::sbDark = sb.dark; C::sbDarker = sb.darker; C::sbArmed = sb.armed;
    C::insens = C::text;   // drawn through a 50% stipple, as Motif does
    C::disabledText = R(SR_DISABLEDTEXT);
    C::field = R(SR_TEXTFIELD);
    C::fieldText = Readable(C::text, C::field, 2.5f);
    C::indicator = R(SR_INDICATOR);
    C::check = R(SR_CHECK);
    C::radio = R(SR_RADIO);
    C::drawing = R(SR_DRAWINGAREA);
    C::fill = R(SR_ALT5);
    C::quickHelp = RGB(0xFE, 0xF9, 0xB5);   // QuickHelpBackground (the same in every scheme)
    C::swatchBorder = RGB(0x2F, 0x4F, 0x4F);
    C::wmFace = R(SR_WMACTIVEBG);
    Shade w = WmShades(C::wmFace);
    C::wmLight = w.light; C::wmDark = w.dark;
    C::wmText = R(SR_WMACTIVEFG);
    C::wmFaceI = R(SR_WMBG);
    Shade wi = WmShades(C::wmFaceI);
    C::wmLightI = wi.light; C::wmDarkI = wi.dark;
    C::wmTextI = R(SR_WMFG);
    if (g_menuBrush) DeleteObject(g_menuBrush);
    g_menuBrush = CreateSolidBrush(C::body);
}
static struct SchemeInit { SchemeInit() { SetScheme(0); } } g_schemeInit;

COLORREF StatusColor(int level) {
    // poor / fair / good / excellent; dark text on light schemes, light text on dark ones
    static const COLORREF onLight[4] = {RGB(0xA0, 0x10, 0x10), RGB(0x80, 0x55, 0x00), RGB(0x10, 0x60, 0x10), RGB(0x08, 0x50, 0x20)};
    static const COLORREF onDark[4] = {RGB(0xFF, 0x90, 0x90), RGB(0xFF, 0xD8, 0x70), RGB(0x9C, 0xF0, 0x9C), RGB(0x80, 0xF0, 0xB0)};
    level = std::clamp(level, 0, 3);
    COLORREF a = onLight[level], b = onDark[level];
    return contrast(a, C::body) >= contrast(b, C::body) ? Readable(a, C::body, 2.5f) : Readable(b, C::body, 2.5f);
}

// ---------------------------------------------------------------------------------------------
// Owner-drawn menus
// ---------------------------------------------------------------------------------------------
struct MenuData { std::wstring text, accel; bool sep = false, sub = false; };
static std::vector<std::unique_ptr<MenuData>> g_menuData;
static void themeMenu(HMENU m) {
    MENUINFO mi{sizeof(mi)};
    mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = g_menuBrush;
    SetMenuInfo(m, &mi);
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        wchar_t buf[256] = L"";
        MENUITEMINFOW it{sizeof(it)};
        it.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU | MIIM_DATA;
        it.dwTypeData = buf;
        it.cch = 255;
        if (!GetMenuItemInfoW(m, i, TRUE, &it)) continue;
        if (it.fType & MFT_OWNERDRAW) { if (it.hSubMenu) themeMenu(it.hSubMenu); continue; }
        auto d = std::make_unique<MenuData>();
        d->sep = (it.fType & MFT_SEPARATOR) != 0;
        d->sub = it.hSubMenu != nullptr;
        std::wstring t = buf;
        size_t tab = t.find(L'\t');
        if (tab != std::wstring::npos) { d->accel = t.substr(tab + 1); t = t.substr(0, tab); }
        std::wstring clean;   // "&&" is a literal ampersand; a single '&' marks a mnemonic
        for (size_t k = 0; k < t.size(); k++) {
            if (t[k] == L'&') { if (k + 1 < t.size() && t[k + 1] == L'&') { clean += L'&'; k++; } continue; }
            clean += t[k];
        }
        d->text = clean;
        MENUITEMINFOW set{sizeof(set)};
        set.fMask = MIIM_FTYPE | MIIM_DATA;
        set.fType = it.fType | MFT_OWNERDRAW;
        set.dwItemData = (ULONG_PTR)d.get();
        SetMenuItemInfoW(m, i, TRUE, &set);
        g_menuData.push_back(std::move(d));
        if (it.hSubMenu) themeMenu(it.hSubMenu);
    }
}
void ThemeMenu(HMENU m) {
    if (g_menuData.size() > 512) g_menuData.erase(g_menuData.begin(), g_menuData.begin() + 256);   // old menus are long gone
    themeMenu(m);
}
static bool isOurs(ULONG_PTR p) {
    for (auto& d : g_menuData) if ((ULONG_PTR)d.get() == p) return true;
    return false;
}
bool MenuMeasure(MEASUREITEMSTRUCT* mi) {
    if (!mi || mi->CtlType != ODT_MENU || !isOurs(mi->itemData)) return false;
    MenuData* d = (MenuData*)mi->itemData;
    if (d->sep) { mi->itemWidth = S(40); mi->itemHeight = S(9); return true; }
    HDC s = GetDC(nullptr);
    HGDIOBJ o = SelectObject(s, F().menu);
    SIZE a{}, b{};
    GetTextExtentPoint32W(s, d->text.c_str(), (int)d->text.size(), &a);
    if (!d->accel.empty()) GetTextExtentPoint32W(s, d->accel.c_str(), (int)d->accel.size(), &b);
    SelectObject(s, o);
    ReleaseDC(nullptr, s);
    mi->itemWidth = a.cx + (b.cx ? b.cx + S(24) : 0) + S(30) + S(22);
    mi->itemHeight = std::max<int>(S(24), a.cy + S(8));
    return true;
}
bool MenuDraw(DRAWITEMSTRUCT* di) {
    if (!di || di->CtlType != ODT_MENU || !isOurs(di->itemData)) return false;
    MenuData* d = (MenuData*)di->itemData;
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    Fill(dc, r, C::body);
    if (d->sep) {
        int y = (r.top + r.bottom) / 2 - 1;
        HLine(dc, r.left + S(2), r.right - S(2), y, C::dark);
        HLine(dc, r.left + S(2), r.right - S(2), y + 1, C::light);
        ExcludeClipRect(dc, r.left, r.top, r.right, r.bottom);
        return true;
    }
    bool grayed = (di->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
    bool armed = (di->itemState & ODS_SELECTED) && !grayed;
    if (armed) {   // Motif: the armed menu item rises out of the menu
        RECT a{r.left + 1, r.top, r.right - 1, r.bottom};
        Bevel(dc, a, std::max(1, S(2)), C::light, C::dark);
    }
    COLORREF tc = C::text;
    if (grayed) tc = contrast(C::disabledText, C::body) >= 1.6f ? C::disabledText : Mix(C::text, C::body, 0.55f);
    int ind = S(26);
    if (di->itemState & ODS_CHECKED) {   // a toggle's check mark, in the scheme's CheckColor
        int s = S(9), cx = r.left + S(13), cy = (r.top + r.bottom) / 2;
        POINT p[3] = {{cx - s / 2, cy}, {cx - s / 8, cy + s / 2}, {cx + s / 2 + 1, cy - s / 2}};
        HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2.5)), grayed ? tc : C::check);
        HGDIOBJ op = SelectObject(dc, pen);
        Polyline(dc, p, 3);
        SelectObject(dc, op);
        DeleteObject(pen);
    }
    RECT t{r.left + ind, r.top, r.right - S(20), r.bottom};
    SetBkMode(dc, TRANSPARENT);
    Text(dc, F().menu, d->text, t, tc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (!d->accel.empty()) Text(dc, F().menu, d->accel, t, tc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    if (d->sub) {   // cascade arrow (and keep Windows from drawing its own over it)
        int s = S(5), cx = r.right - S(12), cy = (r.top + r.bottom) / 2;
        POINT tri[3] = {{cx - s / 2, cy - s}, {cx - s / 2, cy + s}, {cx + s / 2 + 1, cy}};
        SelectObject(dc, GetStockObject(DC_PEN));
        SelectObject(dc, GetStockObject(DC_BRUSH));
        SetDCPenColor(dc, tc); SetDCBrushColor(dc, tc);
        Polygon(dc, tri, 3);
    }
    ExcludeClipRect(dc, r.left, r.top, r.right, r.bottom);
    return true;
}

void BackBuf::ensure(int W, int H) {
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (dc && W == w && H == h) return;
    release();
    HDC s = GetDC(nullptr);
    dc = CreateCompatibleDC(s);
    ReleaseDC(nullptr, s);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
    old = (HBITMAP)SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);
    w = W; h = H;
}
void BackBuf::release() {
    if (dc) { SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc); }
    dc = nullptr; bmp = nullptr; bits = nullptr; w = h = 0;
}

void Fill(HDC dc, RECT r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}
void HLine(HDC dc, int x1, int x2, int y, COLORREF c) { Fill(dc, RECT{x1, y, x2, y + 1}, c); }
void VLine(HDC dc, int x, int y1, int y2, COLORREF c) { Fill(dc, RECT{x, y1, x + 1, y2}, c); }
void Bevel(HDC dc, RECT r, int t, COLORREF tl, COLORREF br) {
    for (int i = 0; i < t; i++) {
        HLine(dc, r.left + i, r.right - i, r.top + i, tl);
        VLine(dc, r.left + i, r.top + i, r.bottom - i, tl);
        HLine(dc, r.left + i + 1, r.right - i, r.bottom - 1 - i, br);
        VLine(dc, r.right - 1 - i, r.top + i + 1, r.bottom - i, br);
    }
}
void Raised(HDC dc, RECT r, int t) { Bevel(dc, r, t, C::light, C::dark); }
void Sunken(HDC dc, RECT r, int t) { Bevel(dc, r, t, C::dark, C::light); }

void Text(HDC dc, HFONT f, const std::wstring& s, RECT r, COLORREF c, UINT fmt) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), -1, &r, fmt | DT_NOPREFIX);
}

// Motif draws insensitive labels through a 50% stipple: only every other pixel of the glyphs.
void StippleText(BackBuf& bb, HFONT f, const std::wstring& s, RECT r, COLORREF c, UINT fmt) {
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    static BackBuf tmp;
    tmp.ensure(std::max(w, tmp.w), std::max(h, tmp.h));
    Fill(tmp.dc, RECT{0, 0, w, h}, RGB(255, 255, 255));
    Text(tmp.dc, f, s, RECT{0, 0, w, h}, RGB(0, 0, 0), fmt);
    GdiFlush();
    uint32_t col = (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if ((tmp.bits[y * tmp.w + x] & 0xff) >= 140) continue;
            int X = r.left + x, Y = r.top + y;
            if (X < 0 || Y < 0 || X >= bb.w || Y >= bb.h || ((X + Y) & 1)) continue;
            bb.bits[Y * bb.w + X] = col;
        }
}
void Label(BackBuf& bb, HFONT f, const std::wstring& s, RECT r, bool enabled, UINT fmt) {
    if (enabled) Text(bb.dc, f, s, r, C::text, fmt);
    else { GdiFlush(); StippleText(bb, f, s, r, C::insens, fmt); }
}

void PushButton(BackBuf& bb, RECT r, const std::wstring& label, bool pressed, bool enabled) {
    HDC dc = bb.dc;
    Bevel(dc, r, 1, C::dark, C::dark);   // the thin ring around every IRIX push button
    RECT in{r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
    Fill(dc, in, pressed ? C::faceArmed : C::face);
    if (pressed) Bevel(dc, in, 2, C::faceDark, C::faceLight); else Bevel(dc, in, 2, C::faceLight, C::faceDark);
    RECT t = in;
    if (pressed) OffsetRect(&t, 1, 1);
    Label(bb, F().body, label, t, enabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void poly(HDC dc, const POINT* p, int n, COLORREF c) {
    SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, c);
    Polyline(dc, p, n);
}
void Diamond(HDC dc, int cx, int cy, int r, bool sel, bool enabled) {
    SelectObject(dc, GetStockObject(DC_PEN));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCPenColor(dc, C::indicator); SetDCBrushColor(dc, C::indicator);
    POINT d[4] = {{cx, cy - r}, {cx + r, cy}, {cx, cy + r}, {cx - r, cy}};
    Polygon(dc, d, 4);
    COLORREF tl = sel ? C::dark : C::light, br = sel ? C::light : C::dark;
    for (int i = 0; i < std::max(1, S(2)); i++) {
        int rr = r - i;
        POINT up[3] = {{cx - rr, cy}, {cx, cy - rr}, {cx + rr, cy}};
        POINT dn[3] = {{cx + rr, cy}, {cx, cy + rr}, {cx - rr, cy}};
        poly(dc, up, 3, tl);
        poly(dc, dn, 3, br);
    }
    if (sel) {
        COLORREF b = enabled ? C::radio : Mix(C::radio, C::indicator, 0.5f);
        SetDCPenColor(dc, b); SetDCBrushColor(dc, b);
        int a = (int)(r * 0.45), x0 = cx - (int)(r * 0.38);
        POINT t[3] = {{x0, cy - a}, {x0, cy + a}, {x0 + a, cy}};
        Polygon(dc, t, 3);
    }
}
void CheckBox(HDC dc, RECT r, bool on, bool enabled) {
    Fill(dc, r, C::indicator);
    if (on) Bevel(dc, r, std::max(1, S(2)), C::dark, C::light); else Bevel(dc, r, std::max(1, S(2)), C::light, C::dark);
    if (on) {
        COLORREF b = enabled ? C::check : Mix(C::check, C::indicator, 0.5f);   // the scheme's CheckColor (IRIX's red check)
        int w = r.right - r.left;
        POINT p[3] = {{r.left + w * 22 / 100, r.top + w * 50 / 100}, {r.left + w * 42 / 100, r.top + w * 72 / 100}, {r.left + w * 80 / 100, r.top + w * 24 / 100}};
        HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2.5)), b);
        HGDIOBJ o = SelectObject(dc, pen);
        Polyline(dc, p, 3);
        SelectObject(dc, o);
        DeleteObject(pen);
    }
}
void Arrow(HDC dc, RECT r, bool up, bool pressed) {
    Fill(dc, r, C::trough);
    int m = std::max(2, S(3));
    int l = r.left + m, rt = r.right - m - 1, t = r.top + m, b = r.bottom - m - 1, cx = (r.left + r.right - 1) / 2;
    POINT tri[3];
    if (up) { tri[0] = {cx, t}; tri[1] = {rt, b}; tri[2] = {l, b}; }
    else { tri[0] = {l, t}; tri[1] = {rt, t}; tri[2] = {cx, b}; }
    SelectObject(dc, GetStockObject(DC_PEN));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCPenColor(dc, C::sb); SetDCBrushColor(dc, pressed ? C::sbArmed : C::sb);
    Polygon(dc, tri, 3);
    COLORREF lt = pressed ? C::sbDark : C::sbLight, dk = pressed ? C::sbLight : C::sbDarker;
    if (up) {
        POINT a[2] = {tri[2], tri[0]}; poly(dc, a, 2, lt);
        POINT bb[3] = {tri[0], tri[1], tri[2]}; poly(dc, bb, 3, dk);
    } else {
        POINT a[3] = {tri[2], tri[0], tri[1]}; poly(dc, a, 3, lt);
        POINT bb[2] = {tri[1], tri[2]}; poly(dc, bb, 2, dk);
    }
}

// ---------------------------------------------------------------------------------------------
// 4Dwm frame
// ---------------------------------------------------------------------------------------------
FrameRects FrameLayout(int W, int H, bool minmax) {
    FrameRects f;
    f.border = S(5);
    f.titleH = S(25);
    int B = f.border, T = f.titleH;
    f.menu = RECT{B, B, B + T, B + T};
    f.maximize = minmax ? RECT{W - B - T, B, W - B, B + T} : RECT{0, 0, 0, 0};
    f.minimize = minmax ? RECT{W - B - 2 * T, B, W - B - T, B + T} : RECT{0, 0, 0, 0};
    f.title = RECT{B + T, B, minmax ? W - B - 2 * T : W - B, B + T};
    f.body = RECT{B, B + T, W - B, H - B};
    return f;
}

void DrawFrame(HDC dc, int W, int H, const std::wstring& title, bool active, bool minmax, int pressed) {
    FrameRects f = FrameLayout(W, H, minmax);
    COLORREF face = active ? C::wmFace : C::wmFaceI, lt = active ? C::wmLight : C::wmLightI, dk = active ? C::wmDark : C::wmDarkI;
    // border ring
    Fill(dc, RECT{0, 0, W, f.border}, face);
    Fill(dc, RECT{0, H - f.border, W, H}, face);
    Fill(dc, RECT{0, 0, f.border, H}, face);
    Fill(dc, RECT{W - f.border, 0, W, H}, face);
    Bevel(dc, RECT{0, 0, W, H}, 1, lt, RGB(0x30, 0x30, 0x30));
    Bevel(dc, RECT{1, 1, W - 1, H - 1}, 1, lt, dk);
    Bevel(dc, RECT{f.border - 1, f.border - 1, W - f.border + 1, H - f.border + 1}, 1, dk, lt);
    // corner notches (4Dwm resize handles)
    int n = f.border + f.titleH;
    auto notchV = [&](int x, int y0, int y1) { VLine(dc, x, y0, y1, dk); VLine(dc, x + 1, y0, y1, lt); };
    auto notchH = [&](int y, int x0, int x1) { HLine(dc, x0, x1, y, dk); HLine(dc, x0, x1, y + 1, lt); };
    notchV(n, 1, f.border - 1); notchV(W - n, 1, f.border - 1);
    notchV(n, H - f.border + 1, H - 1); notchV(W - n, H - f.border + 1, H - 1);
    notchH(n, 1, f.border - 1); notchH(H - n, 1, f.border - 1);
    notchH(n, W - f.border + 1, W - 1); notchH(H - n, W - f.border + 1, W - 1);

    auto section = [&](RECT r, bool down) {
        Fill(dc, r, face);
        Bevel(dc, r, 1, down ? dk : lt, down ? lt : dk);
    };
    section(f.title, false);
    section(f.menu, pressed == 1);
    {   // menu button: a raised horizontal bar
        RECT r = f.menu;
        int w = (r.right - r.left) * 6 / 10, hh = std::max(3, S(5));
        int x = (r.left + r.right - w) / 2, y = (r.top + r.bottom - hh) / 2;
        Fill(dc, RECT{x, y, x + w, y + hh}, face);
        Bevel(dc, RECT{x, y, x + w, y + hh}, 1, lt, RGB(0x30, 0x30, 0x30));
    }
    if (minmax) {
        section(f.minimize, pressed == 2);
        section(f.maximize, pressed == 3);
        RECT r = f.minimize;
        int s = std::max(3, S(5)), x = (r.left + r.right - s) / 2, y = (r.top + r.bottom - s) / 2;
        Bevel(dc, RECT{x, y, x + s, y + s}, 1, lt, RGB(0x30, 0x30, 0x30));
        r = f.maximize;
        s = (r.right - r.left) * 55 / 100; x = (r.left + r.right - s) / 2; y = (r.top + r.bottom - s) / 2;
        Bevel(dc, RECT{x, y, x + s, y + s}, 1, lt, RGB(0x30, 0x30, 0x30));
    }
    RECT t = f.title;
    t.left += S(14);
    Text(dc, F().title, title, t, active ? C::wmText : C::wmTextI, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

LRESULT FrameHitTest(HWND h, LPARAM lp, bool resizable, bool minmax) {
    POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(h, &p);
    RECT rc;
    GetClientRect(h, &rc);
    UseDpi(h);
    FrameRects f = FrameLayout(rc.right, rc.bottom, minmax);
    if (resizable && !IsZoomed(h)) {
        int b = f.border + S(2), c = f.border + f.titleH;
        bool L = p.x < b, R = p.x >= rc.right - b, T = p.y < b, B = p.y >= rc.bottom - b;
        bool Lc = p.x < c, Rc = p.x >= rc.right - c, Tc = p.y < c, Bc = p.y >= rc.bottom - c;
        if ((T && Lc) || (L && Tc)) return HTTOPLEFT;
        if ((T && Rc) || (R && Tc)) return HTTOPRIGHT;
        if ((B && Lc) || (L && Bc)) return HTBOTTOMLEFT;
        if ((B && Rc) || (R && Bc)) return HTBOTTOMRIGHT;
        if (L) return HTLEFT;
        if (R) return HTRIGHT;
        if (T) return HTTOP;
        if (B) return HTBOTTOM;
    }
    if (In(f.title, p)) return HTCAPTION;
    return HTCLIENT;
}

void SetupFrameWindow(HWND h) {
    DWORD pref = 1;   // DWMWCP_DONOTROUND: keep the square IRIX corners on Windows 11
    DwmSetWindowAttribute(h, 33, &pref, sizeof(pref));
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT FrameNcCalcSize(HWND h, WPARAM wp, LPARAM lp) {
    if (wp && IsZoomed(h)) {
        // maximised: keep the client inside the monitor's work area. WM_GETMINMAXINFO already
        // maximises to the work area; if Windows still overhangs the edges (as it does for framed
        // windows), the overhang becomes non-client instead of being drawn off-screen.
        NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*)lp;
        MONITORINFO mi{sizeof(mi)};
        RECT r;
        if (GetMonitorInfoW(MonitorFromRect(&p->rgrc[0], MONITOR_DEFAULTTONEAREST), &mi) && IntersectRect(&r, &p->rgrc[0], &mi.rcWork))
            p->rgrc[0] = r;
    }
    return 0;   // client area = whole window; we draw the 4Dwm frame ourselves
}

// ---------------------------------------------------------------------------------------------
// Motif message dialog
// ---------------------------------------------------------------------------------------------
struct MDlg {
    std::wstring title, text;
    std::vector<std::wstring> buttons;
    int icon = 0, result = -1, pressed = -1, hot = -1, width = 440;
    bool done = false, active = true, tbPressed = false;
    BackBuf bb;
};
static const wchar_t* kDlgClass = L"IrixMotifDialog";

struct DlgLayout { FrameRects f; RECT icon, text, sep; std::vector<RECT> btn; };
static DlgLayout dlgLayout(MDlg* d, int W, int H) {
    DlgLayout L;
    L.f = FrameLayout(W, H, false);
    RECT b = L.f.body;
    int bh = S(30), gap = S(12);
    int n = (int)d->buttons.size();
    // every button is at least the classic Motif width, wider when its label needs it
    std::vector<int> bw(n, S(92));
    HDC s = GetDC(nullptr);
    HGDIOBJ old = SelectObject(s, F().body);
    for (int i = 0; i < n; i++) {
        SIZE sz{};
        GetTextExtentPoint32W(s, d->buttons[i].c_str(), (int)d->buttons[i].size(), &sz);
        bw[i] = std::max<int>(bw[i], sz.cx + S(28));
    }
    SelectObject(s, old);
    ReleaseDC(nullptr, s);
    int total = (n - 1) * gap;
    for (int w : bw) total += w;
    int x = (b.left + b.right - total) / 2, y = b.bottom - S(12) - bh;
    for (int i = 0; i < n; i++) { L.btn.push_back(RECT{x, y, x + bw[i], y + bh}); x += bw[i] + gap; }
    L.sep = RECT{b.left + S(2), y - S(12) - 2, b.right - S(2), y - S(12)};
    L.icon = RECT{b.left + S(16), b.top + S(16), b.left + S(16) + S(44), b.top + S(16) + S(44)};
    L.text = RECT{L.icon.right + S(16), b.top + S(18), b.right - S(18), L.sep.top - S(10)};
    return L;
}
static int dlgTextHeight(const std::wstring& t, int w) {
    HDC s = GetDC(nullptr);
    SelectObject(s, F().body);
    RECT r{0, 0, w, 0};
    DrawTextW(s, t.c_str(), -1, &r, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    ReleaseDC(nullptr, s);
    return r.bottom;
}

static LRESULT CALLBACK DlgProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    MDlg* d = (MDlg*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_NCCREATE: {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        break;
    }
    case WM_NCCALCSIZE: return FrameNcCalcSize(h, wp, lp);
    case WM_NCHITTEST: return FrameHitTest(h, lp, false, false);
    case WM_NCACTIVATE: if (d) { d->active = wp != 0; InvalidateRect(h, nullptr, FALSE); } return DefWindowProcW(h, m, wp, -1);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        UseDpi(h);
        RECT rc; GetClientRect(h, &rc);
        d->bb.ensure(rc.right, rc.bottom);
        HDC dc = d->bb.dc;
        DlgLayout L = dlgLayout(d, rc.right, rc.bottom);
        Fill(dc, rc, C::body);
        DrawFrame(dc, rc.right, rc.bottom, d->title, d->active, false, d->tbPressed ? 1 : 0);
        // icon: Motif-style symbol in a sunken well
        Fill(dc, L.icon, C::list);
        Sunken(dc, L.icon, 2);
        const wchar_t* sym = d->icon == 1 ? L"?" : (d->icon == 2 ? L"!" : L"i");
        const SchemeDef& sc = Scheme(CurrentScheme());
        COLORREF ic = d->icon == 2 ? Readable(Mix(Ref(sc.c[SR_RED]), RGB(0, 0, 0), C::darkScheme ? 0.0f : 0.3f), C::list, 3.0f)
                                   : Readable(Ref(sc.c[SR_BLUE]), C::list, 3.0f);
        Text(dc, F().big, sym, L.icon, ic, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        Text(dc, F().body, d->text, L.text, C::text, DT_LEFT | DT_WORDBREAK);
        HLine(dc, L.sep.left, L.sep.right, L.sep.top, C::dark);
        HLine(dc, L.sep.left, L.sep.right, L.sep.top + 1, C::light);
        for (size_t i = 0; i < L.btn.size(); i++)
            PushButton(d->bb, L.btn[i], d->buttons[i], d->pressed == (int)i && d->hot == (int)i, true);
        GdiFlush();
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc; GetClientRect(h, &rc);
        UseDpi(h);
        DlgLayout L = dlgLayout(d, rc.right, rc.bottom);
        for (size_t i = 0; i < L.btn.size(); i++)
            if (In(L.btn[i], p)) { d->pressed = d->hot = (int)i; SetCapture(h); InvalidateRect(h, nullptr, FALSE); }
        if (In(L.f.menu, p)) { d->result = (int)d->buttons.size() - 1; d->done = true; }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (d->pressed >= 0) {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT rc; GetClientRect(h, &rc);
            UseDpi(h);
            DlgLayout L = dlgLayout(d, rc.right, rc.bottom);
            int hot = In(L.btn[d->pressed], p) ? d->pressed : -1;
            if (hot != d->hot) { d->hot = hot; InvalidateRect(h, nullptr, FALSE); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (d->pressed >= 0) {
            ReleaseCapture();
            if (d->hot == d->pressed) { d->result = d->pressed; d->done = true; }
            d->pressed = -1;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { d->result = (int)d->buttons.size() - 1; d->done = true; }
        if (wp == VK_RETURN) { d->result = 0; d->done = true; }
        return 0;
    case WM_CLOSE:
        d->result = (int)d->buttons.size() - 1;
        d->done = true;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static LRESULT CALLBACK PromptProc(HWND h, UINT m, WPARAM wp, LPARAM lp);
void RegisterMotifDialogClass(HINSTANCE inst) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = kDlgClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = PromptProc;
    wc.lpszClassName = L"IrixMotifPrompt";
    RegisterClassExW(&wc);
}

int MotifDialog(HWND owner, const std::wstring& title, const std::wstring& text, const std::vector<std::wstring>& buttons, int icon, int width) {
    UseDpi(owner);
    MDlg d;
    d.title = title; d.text = text; d.buttons = buttons; d.icon = icon; d.width = width;
    int W = S(width);
    FrameRects f = FrameLayout(W, 1000, false);
    int textW = W - 2 * f.border - S(16) - S(44) - S(16) - S(18);
    int th = std::max(S(44), dlgTextHeight(text, textW));
    int H = f.border + f.titleH + S(18) + th + S(10) + S(12) + 2 + S(12) + S(30) + S(12) + f.border;
    RECT orc;
    GetWindowRect(owner, &orc);
    int x = (orc.left + orc.right - W) / 2, y = (orc.top + orc.bottom - H) / 2;
    HWND h = CreateWindowExW(0, kDlgClass, title.c_str(), WS_POPUP | WS_SYSMENU, x, y, W, H, owner, nullptr,
                             (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE), &d);
    SetupFrameWindow(h);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetFocus(h);
    EnableWindow(owner, FALSE);
    MSG msg;
    while (!d.done) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) { PostQuitMessage((int)msg.wParam); break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    DestroyWindow(h);
    return d.result < 0 ? (int)buttons.size() - 1 : d.result;
}


// ---------------------------------------------------------------------------------------------
// Scales, option menus, tabs, groups, glyphs
// ---------------------------------------------------------------------------------------------
RECT ScaleThumb(RECT tr, float t) {
    int tw = S(12);
    t = std::clamp(t, 0.0f, 1.0f);
    int x = tr.left + 1 + (int)std::lround(t * (tr.right - tr.left - 2 - tw));
    return RECT{x, tr.top - S(3), x + tw, tr.bottom + S(3)};
}
void ScaleDraw(BackBuf& bb, RECT tr, float t, bool enabled, bool pressed) {
    HDC dc = bb.dc;
    Fill(dc, tr, enabled ? C::trough : Mix(C::trough, C::body, 0.5f));
    Bevel(dc, tr, 1, C::dark, C::light);
    if (enabled && t > 0) {   // filled portion
        RECT f{tr.left + 1, tr.top + 1, ScaleThumb(tr, t).left + S(6), tr.bottom - 1};
        Fill(dc, f, contrast(C::fill, C::trough) >= 1.25f ? C::fill : Mix(C::trough, C::text, 0.35f));
    }
    RECT th = ScaleThumb(tr, t);
    Fill(dc, th, pressed ? C::sbArmed : C::sb);
    Bevel(dc, th, 1, enabled ? C::sbLight : C::sb, C::sbDarker);
    int mx = (th.left + th.right) / 2;
    VLine(dc, mx - 1, th.top + 3, th.bottom - 3, enabled ? C::sbDark : Mix(C::sbDark, C::sb, 0.5f));
    VLine(dc, mx, th.top + 3, th.bottom - 3, C::sbLight);
}
void OptionButton(BackBuf& bb, RECT r, const std::wstring& text, bool pressed, bool enabled) {
    HDC dc = bb.dc;
    Fill(dc, r, pressed ? C::faceArmed : C::face);
    if (pressed) Bevel(dc, r, 2, C::faceDark, C::faceLight); else Bevel(dc, r, 2, C::faceLight, C::faceDark);
    // Motif cascade indicator: a small raised bar on the right
    int iw = S(12), ih = S(6);
    RECT ind{r.right - S(8) - iw, (r.top + r.bottom - ih) / 2, r.right - S(8), (r.top + r.bottom - ih) / 2 + ih};
    Fill(dc, ind, C::face);
    Bevel(dc, ind, 1, enabled ? C::faceLight : C::face, C::faceDarker);
    RECT t{r.left + S(8), r.top, ind.left - S(6), r.bottom};
    Label(bb, F().body, text, t, enabled, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
void TabButton(BackBuf& bb, RECT r, const std::wstring& text, bool active, bool hot) {
    HDC dc = bb.dc;
    RECT f = r;
    if (!active) f.top += S(3);
    Fill(dc, f, active ? C::body : Mix(C::body, C::face, hot ? 0.3f : 0.6f));
    HLine(dc, f.left + 1, f.right - 1, f.top, C::light);
    VLine(dc, f.left, f.top + 1, f.bottom, C::light);
    VLine(dc, f.right - 1, f.top + 1, f.bottom, C::darker);
    VLine(dc, f.right - 2, f.top + 2, f.bottom, C::dark);
    if (!active) HLine(dc, f.left, f.right, f.bottom - 1, C::light);
    Text(dc, active ? F().bold : F().body, text, f, C::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
void EtchedGroup(BackBuf& bb, RECT r, const std::wstring& title) {
    HDC dc = bb.dc;
    int ty = r.top + S(7);
    RECT box{r.left, ty, r.right, r.bottom};
    Bevel(dc, box, 1, C::dark, C::light);
    Bevel(dc, RECT{box.left + 1, box.top + 1, box.right - 1, box.bottom - 1}, 1, C::light, C::dark);
    if (!title.empty()) {
        SelectObject(dc, F().smallBold);
        SIZE sz;
        GetTextExtentPoint32W(dc, title.c_str(), (int)title.size(), &sz);
        RECT t{r.left + S(10), r.top, r.left + S(10) + sz.cx + S(8), r.top + S(15)};
        Fill(dc, t, C::body);
        Text(dc, F().smallBold, title, t, C::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}
void GlyphLock(HDC dc, RECT r, bool locked, bool enabled) {
    int w = r.right - r.left, h = r.bottom - r.top;
    int bw = w * 62 / 100, bh = h * 42 / 100;
    int bx = r.left + (w - bw) / 2, by = r.top + h * 50 / 100;
    COLORREF body = !enabled ? RGB(0xA8, 0xA8, 0xA8) : (locked ? RGB(0xC8, 0x9A, 0x2A) : RGB(0x9A, 0x9A, 0x9A));
    COLORREF line = enabled ? Readable(RGB(0x30, 0x30, 0x30), C::body, 2.2f) : Mix(C::text, C::body, 0.55f);
    // shackle
    int sw = bw * 70 / 100, sx = bx + (bw - sw) / 2 + (locked ? 0 : bw * 28 / 100), sy = r.top + h * 12 / 100;
    HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2)), line);
    HGDIOBJ op = SelectObject(dc, pen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Arc(dc, sx, sy, sx + sw, sy + sw, sx + sw, sy + sw / 2, sx, sy + sw / 2);
    MoveToEx(dc, sx, sy + sw / 2, nullptr); LineTo(dc, sx, by + (locked ? 1 : -S(3)));
    MoveToEx(dc, sx + sw - 1, sy + sw / 2, nullptr); LineTo(dc, sx + sw - 1, by + 1);
    SelectObject(dc, op);
    DeleteObject(pen);
    RECT b{bx, by, bx + bw, by + bh};
    Fill(dc, b, body);
    Bevel(dc, b, 1, enabled ? RGB(0xF0, 0xE0, 0xB0) : C::light, line);
    Fill(dc, RECT{bx + bw / 2 - 1, by + bh / 3, bx + bw / 2 + 1, by + bh * 2 / 3 + 1}, line);
}
void GlyphDice(HDC dc, RECT r, int pips, bool enabled) {
    int s = std::min(r.right - r.left, r.bottom - r.top) - S(2);
    int x = (r.left + r.right - s) / 2, y = (r.top + r.bottom - s) / 2;
    RECT d{x, y, x + s, y + s};
    Fill(dc, d, enabled ? RGB(0xFA, 0xFA, 0xFA) : RGB(0xD8, 0xD8, 0xD8));
    Bevel(dc, d, 1, RGB(0x60, 0x60, 0x60), RGB(0x30, 0x30, 0x30));
    COLORREF pc = enabled ? RGB(0xC0, 0x20, 0x40) : RGB(0x90, 0x90, 0x90);
    int pr = std::max(1, s / 9);
    auto pip = [&](float fx, float fy) {
        int cx = x + (int)(s * fx), cy = y + (int)(s * fy);
        Fill(dc, RECT{cx - pr, cy - pr, cx + pr + 1, cy + pr + 1}, pc);
    };
    static const float P[7][2] = {{0.5f, 0.5f}, {0.27f, 0.27f}, {0.73f, 0.73f}, {0.73f, 0.27f}, {0.27f, 0.73f}, {0.27f, 0.5f}, {0.73f, 0.5f}};
    static const int sets[7][7] = {{0}, {0}, {1, 2}, {0, 1, 2}, {1, 2, 3, 4}, {0, 1, 2, 3, 4}, {1, 2, 3, 4, 5, 6}};
    static const int cnt[7] = {0, 1, 2, 3, 4, 5, 6};
    pips = std::clamp(pips, 1, 6);
    for (int i = 0; i < cnt[pips]; i++) pip(P[sets[pips][i]][0], P[sets[pips][i]][1]);
}

// ---------------------------------------------------------------------------------------------
// Prompt dialog (Motif XmPromptDialog)
// ---------------------------------------------------------------------------------------------
struct PDlg {
    std::wstring title, prompt, value;
    HWND edit = nullptr;
    int pressed = -1, hot = -1;
    bool done = false, ok = false, active = true;
    BackBuf bb;
};
static const wchar_t* kPromptClass = L"IrixMotifPrompt";
struct PLayout { FrameRects f; RECT prompt, edit, sep, btn[2]; };
static PLayout promptLayout(int W, int H) {
    PLayout L;
    L.f = FrameLayout(W, H, false);
    RECT b = L.f.body;
    L.prompt = RECT{b.left + S(16), b.top + S(14), b.right - S(16), b.top + S(14) + S(20)};
    L.edit = RECT{b.left + S(16), L.prompt.bottom + S(6), b.right - S(16), L.prompt.bottom + S(6) + S(28)};
    int bh = S(30), bw = S(92), gap = S(12), y = b.bottom - S(12) - bh;
    int x = (b.left + b.right - (2 * bw + gap)) / 2;
    L.btn[0] = RECT{x, y, x + bw, y + bh};
    L.btn[1] = RECT{x + bw + gap, y, x + 2 * bw + gap, y + bh};
    L.sep = RECT{b.left + S(2), y - S(12) - 2, b.right - S(2), y - S(12)};
    return L;
}
static void promptFinish(PDlg* d, bool ok) {
    if (ok && d->edit) {
        int n = GetWindowTextLengthW(d->edit);
        std::wstring s(n, L'\0');
        if (n) GetWindowTextW(d->edit, &s[0], n + 1);
        d->value = s;
    }
    d->ok = ok;
    d->done = true;
}
static LRESULT CALLBACK PromptProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    PDlg* d = (PDlg*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_NCCREATE: SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams); break;
    case WM_CREATE: {
        d = (PDlg*)((CREATESTRUCTW*)lp)->lpCreateParams;
        UseDpi(h);
        d->edit = CreateWindowExW(0, L"EDIT", d->value.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)1,
                                  (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE), nullptr);
        SendMessageW(d->edit, EM_SETLIMITTEXT, 80, 0);
        return 0;
    }
    case WM_SIZE: {
        UseDpi(h);
        PLayout L = promptLayout(LOWORD(lp), HIWORD(lp));
        if (d && d->edit) {
            SendMessageW(d->edit, WM_SETFONT, (WPARAM)F().list, TRUE);
            MoveWindow(d->edit, L.edit.left + 3, L.edit.top + 4, L.edit.right - L.edit.left - 6, L.edit.bottom - L.edit.top - 7, TRUE);
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: return EditColors((HDC)wp);
    case WM_NCCALCSIZE: return FrameNcCalcSize(h, wp, lp);
    case WM_NCHITTEST: return FrameHitTest(h, lp, false, false);
    case WM_NCACTIVATE: if (d) { d->active = wp != 0; InvalidateRect(h, nullptr, FALSE); } return DefWindowProcW(h, m, wp, -1);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        UseDpi(h);
        RECT rc; GetClientRect(h, &rc);
        d->bb.ensure(rc.right, rc.bottom);
        HDC dc = d->bb.dc;
        PLayout L = promptLayout(rc.right, rc.bottom);
        Fill(dc, rc, C::body);
        DrawFrame(dc, rc.right, rc.bottom, d->title, d->active, false, 0);
        Text(dc, F().body, d->prompt, L.prompt, C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        Fill(dc, L.edit, C::field);
        Sunken(dc, L.edit, 2);
        HLine(dc, L.sep.left, L.sep.right, L.sep.top, C::dark);
        HLine(dc, L.sep.left, L.sep.right, L.sep.top + 1, C::light);
        PushButton(d->bb, L.btn[0], L"OK", d->pressed == 0 && d->hot == 0, true);
        PushButton(d->bb, L.btn[1], L"Cancel", d->pressed == 1 && d->hot == 1, true);
        GdiFlush();
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc; GetClientRect(h, &rc);
        UseDpi(h);
        PLayout L = promptLayout(rc.right, rc.bottom);
        for (int i = 0; i < 2; i++)
            if (In(L.btn[i], p)) { d->pressed = d->hot = i; SetCapture(h); InvalidateRect(h, nullptr, FALSE); }
        if (In(L.f.menu, p)) promptFinish(d, false);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (d->pressed >= 0) {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT rc; GetClientRect(h, &rc);
            UseDpi(h);
            PLayout L = promptLayout(rc.right, rc.bottom);
            int hot = In(L.btn[d->pressed], p) ? d->pressed : -1;
            if (hot != d->hot) { d->hot = hot; InvalidateRect(h, nullptr, FALSE); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (d->pressed >= 0) {
            ReleaseCapture();
            if (d->hot == d->pressed) promptFinish(d, d->pressed == 0);
            d->pressed = -1;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_CLOSE: promptFinish(d, false); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
bool MotifPrompt(HWND owner, const std::wstring& title, const std::wstring& prompt, std::wstring& value) {
    UseDpi(owner);
    PDlg d;
    d.title = title; d.prompt = prompt; d.value = value;
    int W = S(420);
    FrameRects f = FrameLayout(W, 1000, false);
    int H = f.border + f.titleH + S(14) + S(20) + S(6) + S(28) + S(14) + 2 + S(12) + S(30) + S(12) + f.border;
    RECT orc;
    GetWindowRect(owner, &orc);
    HWND h = CreateWindowExW(0, kPromptClass, title.c_str(), WS_POPUP | WS_SYSMENU | WS_CLIPCHILDREN, (orc.left + orc.right - W) / 2,
                             (orc.top + orc.bottom - H) / 2, W, H, owner, nullptr, (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE), &d);
    SetupFrameWindow(h);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    EnableWindow(owner, FALSE);
    SetFocus(d.edit);
    SendMessageW(d.edit, EM_SETSEL, 0, -1);
    MSG msg;
    while (!d.done) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) { PostQuitMessage((int)msg.wParam); break; }
        if (msg.message == WM_KEYDOWN && (msg.hwnd == d.edit || msg.hwnd == h)) {
            if (msg.wParam == VK_RETURN) { promptFinish(&d, true); continue; }
            if (msg.wParam == VK_ESCAPE) { promptFinish(&d, false); continue; }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    DestroyWindow(h);
    if (d.ok) value = d.value;
    return d.ok;
}
