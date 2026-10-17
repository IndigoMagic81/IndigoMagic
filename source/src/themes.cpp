// Theme Options: the IRIX desktop colour schemes as themes for this program, in a window modelled on
// IRIX's Scheme Browser - the schemes on the left, a sample window drawn in the highlighted scheme
// on the right, Apply to recolour every window of the program.
#include "app.h"
#include "crash.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>

static HWND g_thWnd = nullptr;
static const wchar_t* kThClass = L"IrixThemeOptions";
static int g_thStart = 0;   // theme in use when the window opened (Revert)

struct ThemeWin {
    int sel = 0, top = 0, pressed = -1, hot = -1;
    bool active = true, dragThumb = false;
    int dragOff = 0;
    BackBuf bb;
} g_th;

enum { TH_LIST = 100, TH_UP, TH_DN, TH_THUMB, TH_PGUP, TH_PGDN, TH_APPLY = 120, TH_REVERT, TH_CLOSE, TH_MENU };
struct THLayout {
    FrameRects f;
    RECT lbl, list, items, sb, up, dn, track, thumb, pvLbl, preview, desc, btn[3];
    int rowH = 26, vis = 1;
};
static THLayout thLayout(int W, int H) {
    THLayout L;
    L.f = FrameLayout(W, H, false);
    RECT b = L.f.body;
    int bh = S(30), bw = S(96), gap = S(10), by = b.bottom - S(12) - bh;
    for (int i = 0; i < 3; i++) {
        int r = b.right - S(12) - (2 - i) * (bw + gap);
        L.btn[i] = RECT{r - bw, by, r, by + bh};
    }
    int lw = S(236);
    L.lbl = RECT{b.left + S(14), b.top + S(10), b.left + S(14) + lw, b.top + S(30)};
    L.list = RECT{b.left + S(14), L.lbl.bottom + S(4), b.left + S(14) + lw, by - S(14)};
    L.rowH = S(26);
    RECT in{L.list.left + 2, L.list.top + 2, L.list.right - 2, L.list.bottom - 2};
    int sbw = S(17);
    L.sb = RECT{in.right - sbw, in.top, in.right, in.bottom};
    L.items = RECT{in.left, in.top + 1, L.sb.left - 1, in.bottom - 1};
    L.vis = std::max(1, (int)((L.items.bottom - L.items.top) / L.rowH));
    L.up = RECT{L.sb.left + 1, L.sb.top + 1, L.sb.right - 1, L.sb.top + sbw - 1};
    L.dn = RECT{L.sb.left + 1, L.sb.bottom - sbw + 1, L.sb.right - 1, L.sb.bottom - 1};
    L.track = RECT{L.sb.left + 1, L.up.bottom, L.sb.right - 1, L.dn.top};
    int n = SchemeCount(), maxTop = std::max(0, n - L.vis);
    g_th.top = std::clamp(g_th.top, 0, maxTop);
    int th = L.track.bottom - L.track.top;
    int tH = n <= L.vis ? th : std::max(S(18), th * L.vis / std::max(1, n));
    int ty = L.track.top + (maxTop ? (th - tH) * g_th.top / maxTop : 0);
    L.thumb = RECT{L.track.left, ty, L.track.right, ty + tH};
    int px = L.list.right + S(18);
    L.pvLbl = RECT{px, L.lbl.top, b.right - S(14), L.lbl.bottom};
    L.desc = RECT{px, L.list.bottom - S(44), b.right - S(14), L.list.bottom};
    L.preview = RECT{px, L.list.top, b.right - S(14), L.desc.top - S(8)};
    return L;
}
static int thMaxTop(const THLayout& L) { return std::max(0, SchemeCount() - L.vis); }
static void thEnsure(const THLayout& L) {
    if (g_th.sel < g_th.top) g_th.top = g_th.sel;
    if (g_th.sel >= g_th.top + L.vis) g_th.top = g_th.sel - L.vis + 1;
    g_th.top = std::clamp(g_th.top, 0, thMaxTop(L));
}

// A small window drawn entirely in scheme i: the 4Dwm frame, a label, a text field, a list, a
// toggle, a radio button, an option menu, a scale and push buttons.
static void drawSample(BackBuf& bb, RECT area, int scheme) {
    int keep = CurrentScheme();
    SetScheme(scheme);
    HDC dc = bb.dc;
    int W = area.right - area.left, H = area.bottom - area.top;
    // draw at the origin of a scratch buffer, then copy in (DrawFrame works in window coordinates)
    static BackBuf tmp;
    tmp.ensure(std::max(W, tmp.w), std::max(H, tmp.h));
    HDC t = tmp.dc;
    Fill(t, RECT{0, 0, W, H}, C::body);
    DrawFrame(t, W, H, Scheme(scheme).name, true, true, 0);
    FrameRects f = FrameLayout(W, H, true);
    RECT b = f.body;
    int x = b.left + S(12), y = b.top + S(10), r = b.right - S(12);
    Text(t, F().bold, L"Background Setting", RECT{x, y, r, y + S(20)}, C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    y += S(26);
    // list (left) + controls (right)
    int lw = std::min(S(150), (r - x) / 2);
    RECT lst{x, y, x + lw, b.bottom - S(52)};
    Sunken(t, lst, 2);
    Fill(t, RECT{lst.left + 2, lst.top + 2, lst.right - 2, lst.bottom - 2}, C::list);
    static const wchar_t* items[] = {L"Bulge", L"Granite", L"Moorish", L"Rhodo", L"Scallop", L"Tide Pool", L"Wicker"};
    int rh = S(20);
    for (int i = 0; i < 7; i++) {
        RECT rr{lst.left + S(3), lst.top + S(3) + i * rh, lst.right - S(3), lst.top + S(3) + (i + 1) * rh};
        if (rr.bottom > lst.bottom - S(2)) break;
        bool s = i == 1;
        if (s) Fill(t, rr, C::listSelBg);
        Text(t, F().body, items[i], RECT{rr.left + S(5), rr.top, rr.right, rr.bottom}, s ? C::listSelFg : C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    int cx = lst.right + S(14);
    int cy = y;
    // text field
    Text(t, F().body, L"Name:", RECT{cx, cy, cx + S(48), cy + S(26)}, C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT tf{cx + S(50), cy, r, cy + S(26)};
    Fill(t, tf, C::field);
    Sunken(t, tf, 2);
    Text(t, F().body, L"Indigo Magic", RECT{tf.left + S(6), tf.top, tf.right - S(4), tf.bottom}, C::fieldText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    cy += S(34);
    // toggles
    int s14 = S(14);
    CheckBox(t, RECT{cx + S(2), cy + S(4), cx + S(2) + s14, cy + S(4) + s14}, true, true);
    Text(t, F().body, L"Show Icons", RECT{cx + S(24), cy, r, cy + S(22)}, C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    cy += S(26);
    Diamond(t, cx + S(10), cy + S(11), S(8), true, true);
    Text(t, F().body, L"Tile Background", RECT{cx + S(23), cy, r, cy + S(22)}, C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    cy += S(30);
    // option menu
    OptionButton(tmp, RECT{cx, cy, r, cy + S(26)}, L"90s Neon", false, true);
    cy += S(36);
    // scale
    if (cy + S(16) < b.bottom - S(52)) ScaleDraw(tmp, RECT{cx, cy, r, cy + S(12)}, 0.62f, true, false);
    // buttons
    int by = b.bottom - S(40), bw = std::min(S(80), (r - x - S(16)) / 3);
    static const wchar_t* bn[3] = {L"Apply", L"Reset", L"Help"};
    for (int i = 0; i < 3; i++) {
        int rx = r - (2 - i) * (bw + S(8));
        PushButton(tmp, RECT{rx - bw, by, rx, by + S(28)}, bn[i], false, true);
    }
    GdiFlush();
    BitBlt(dc, area.left, area.top, W, H, t, 0, 0, SRCCOPY);
    SetScheme(keep);
}

static void paintTH(HWND h, HDC hdc) {
    UseDpi(h);
    RECT rc;
    GetClientRect(h, &rc);
    g_th.bb.ensure(rc.right, rc.bottom);
    HDC dc = g_th.bb.dc;
    THLayout L = thLayout(rc.right, rc.bottom);
    Fill(dc, rc, C::body);
    DrawFrame(dc, rc.right, rc.bottom, L"Theme Options", g_th.active, false, g_th.pressed == TH_MENU ? 1 : 0);
    Text(dc, F().bold, L"IRIX Schemes", L.lbl, C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    // the list
    Sunken(dc, L.list, 2);
    Fill(dc, RECT{L.list.left + 2, L.list.top + 2, L.list.right - 2, L.list.bottom - 2}, C::list);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, L.items.left, L.items.top, L.items.right, L.items.bottom);
    for (int i = g_th.top; i < SchemeCount() && i <= g_th.top + L.vis; i++) {
        RECT r{L.items.left, L.items.top + (i - g_th.top) * L.rowH, L.items.right, L.items.top + (i - g_th.top + 1) * L.rowH};
        bool s = i == g_th.sel;
        if (s) Fill(dc, RECT{r.left + S(2), r.top + 1, r.right - S(2), r.bottom - 1}, C::listSelBg);
        // four chips: window, buttons, text fields, active 4Dwm frame
        const SchemeDef& sd = Scheme(i);
        int ch = S(12), x0 = r.left + S(7), cy = (r.top + r.bottom - ch) / 2;
        static const int roles[4] = {SR_BASIC, SR_BUTTON, SR_TEXTFIELD, SR_WMACTIVEBG};
        for (int k = 0; k < 4; k++) {
            RECT c{x0 + k * (ch + S(2)), cy, x0 + k * (ch + S(2)) + ch, cy + ch};
            Fill(dc, c, Ref(sd.c[roles[k]]));
            Bevel(dc, c, 1, RGB(0x30, 0x30, 0x30), RGB(0x30, 0x30, 0x30));
        }
        RECT tr{x0 + 4 * (ch + S(2)) + S(6), r.top, r.right - S(24), r.bottom};
        bool cur = i == CurrentScheme();
        COLORREF tc = s ? C::listSelFg : C::text;
        Text(dc, cur ? F().bold : F().body, sd.name, tr, tc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (cur) {   // the theme in use gets a check mark
            int cxm = r.right - S(14), cym = (r.top + r.bottom) / 2, k = S(5);
            POINT pm[3] = {{cxm - k, cym}, {cxm - k / 3, cym + k * 2 / 3}, {cxm + k, cym - k * 2 / 3}};
            HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2)), tc);
            HGDIOBJ op = SelectObject(dc, pen);
            Polyline(dc, pm, 3);
            SelectObject(dc, op);
            DeleteObject(pen);
        }
    }
    RestoreDC(dc, saved);
    // scrollbar
    Fill(dc, L.sb, C::trough);
    Bevel(dc, L.sb, 1, C::dark, C::light);
    Arrow(dc, L.up, true, g_th.pressed == TH_UP && g_th.hot == TH_UP);
    Arrow(dc, L.dn, false, g_th.pressed == TH_DN && g_th.hot == TH_DN);
    Fill(dc, L.thumb, C::sb);
    Bevel(dc, L.thumb, 2, C::sbLight, C::sbDark);
    // preview + description
    const SchemeDef& sd = Scheme(g_th.sel);
    Text(dc, F().bold, std::wstring(L"Preview: ") + sd.name, L.pvLbl, C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    drawSample(g_th.bb, L.preview, g_th.sel);
    std::wstring d = sd.desc;
    d += sd.dark ? L".  A dark scheme." : L".";
    if (g_th.sel == CurrentScheme()) d += L"  In use.";
    Text(dc, F().italic, d, L.desc, C::text, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    static const wchar_t* bn[3] = {L"Apply", L"Revert", L"Close"};
    for (int i = 0; i < 3; i++) {
        bool en = i == 0 ? g_th.sel != CurrentScheme() : (i == 1 ? CurrentScheme() != g_thStart : true);
        PushButton(g_th.bb, L.btn[i], bn[i], g_th.pressed == TH_APPLY + i && g_th.hot == TH_APPLY + i, en);
    }
    GdiFlush();
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
}

static int hitTH(POINT p, const THLayout& L) {
    if (In(L.f.menu, p)) return TH_MENU;
    for (int i = 0; i < 3; i++) if (In(L.btn[i], p)) return TH_APPLY + i;
    if (In(L.up, p)) return TH_UP;
    if (In(L.dn, p)) return TH_DN;
    if (In(L.thumb, p)) return TH_THUMB;
    if (In(L.track, p)) return p.y < L.thumb.top ? TH_PGUP : TH_PGDN;
    if (In(L.items, p)) return TH_LIST;
    return -1;
}

// Recolour the whole program
void ApplyTheme(int i) {
    i = std::clamp(i, 0, SchemeCount() - 1);
    if (i == CurrentScheme()) return;
    CrashTrail(L"theme %ls", Scheme(i).key);
    SetScheme(i);
    if (g.main) {
        g.status = std::wstring(L"Theme: ") + Scheme(i).name + L" - " + Scheme(i).desc + L".";
        Changed();
        RedrawWindow(g.main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
        if (g.search) InvalidateRect(g.search, nullptr, TRUE);
        SaveState();
    }
    if (HWND cb = ColorBrowserWindow()) RedrawWindow(cb, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    if (g_thWnd) InvalidateRect(g_thWnd, nullptr, FALSE);
}

static void thSelect(int i, bool scroll = true) {
    g_th.sel = std::clamp(i, 0, SchemeCount() - 1);
    if (scroll && g_thWnd) {
        RECT rc;
        GetClientRect(g_thWnd, &rc);
        UseDpi(g_thWnd);
        thEnsure(thLayout(rc.right, rc.bottom));
    }
    if (g_thWnd) InvalidateRect(g_thWnd, nullptr, FALSE);
}
void CloseThemeOptions() {
    if (!g_thWnd) return;
    HWND w = g_thWnd;
    g_thWnd = nullptr;
    DestroyWindow(w);
    if (g.main) SetActiveWindow(g.main);
}
static void thScroll(int what, const THLayout& L) {
    int t = g_th.top;
    if (what == TH_UP) t--;
    else if (what == TH_DN) t++;
    else if (what == TH_PGUP) t -= L.vis;
    else if (what == TH_PGDN) t += L.vis;
    g_th.top = std::clamp(t, 0, thMaxTop(L));
}

static LRESULT thProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: SetupFrameWindow(h); return 0;
    case WM_NCCALCSIZE: return FrameNcCalcSize(h, wp, lp);
    case WM_NCHITTEST: return FrameHitTest(h, lp, false, false);
    case WM_NCACTIVATE: g_th.active = wp != 0; InvalidateRect(h, nullptr, FALSE); return DefWindowProcW(h, m, wp, -1);
    case WM_ERASEBKGND: return 1;
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        if (!IsIconic(h) && rc.right >= S(300) && rc.bottom >= S(240)) paintTH(h, hdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        UseDpi(h);
        RECT rc; GetClientRect(h, &rc);
        THLayout L = thLayout(rc.right, rc.bottom);
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int w = hitTH(p, L);
        if (w == TH_MENU) { CloseThemeOptions(); return 0; }
        if (w == TH_LIST) {
            int row = g_th.top + (p.y - L.items.top) / L.rowH;
            if (row >= 0 && row < SchemeCount()) {
                thSelect(row, false);
                if (m == WM_LBUTTONDBLCLK) ApplyTheme(row);
            }
            return 0;
        }
        if (w == TH_THUMB) { g_th.dragThumb = true; g_th.dragOff = p.y - L.thumb.top; SetCapture(h); return 0; }
        if (w == TH_UP || w == TH_DN || w == TH_PGUP || w == TH_PGDN) {
            g_th.pressed = g_th.hot = w;
            thScroll(w, L);
            SetCapture(h);
            SetTimer(h, 1, 350, nullptr);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (w >= TH_APPLY && w <= TH_CLOSE) { g_th.pressed = g_th.hot = w; SetCapture(h); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) {
            if (g_th.pressed >= TH_UP && g_th.pressed <= TH_PGDN && g_th.hot == g_th.pressed) {
                UseDpi(h);
                RECT rc; GetClientRect(h, &rc);
                thScroll(g_th.pressed, thLayout(rc.right, rc.bottom));
                InvalidateRect(h, nullptr, FALSE);
                SetTimer(h, 1, 55, nullptr);
            } else if (g_th.pressed < 0) KillTimer(h, 1);
        }
        return 0;
    case WM_MOUSEMOVE: {
        UseDpi(h);
        RECT rc; GetClientRect(h, &rc);
        THLayout L = thLayout(rc.right, rc.bottom);
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (g_th.dragThumb) {
            int th = L.track.bottom - L.track.top, tH = L.thumb.bottom - L.thumb.top;
            if (th > tH && thMaxTop(L) > 0) {
                int nt = (int)std::lround((double)(p.y - g_th.dragOff - L.track.top) * thMaxTop(L) / (th - tH));
                nt = std::clamp(nt, 0, thMaxTop(L));
                if (nt != g_th.top) { g_th.top = nt; InvalidateRect(h, nullptr, FALSE); }
            }
        } else if (g_th.pressed >= 0) {
            int hot = hitTH(p, L) == g_th.pressed ? g_th.pressed : -1;
            if (hot != g_th.hot) { g_th.hot = hot; InvalidateRect(h, nullptr, FALSE); }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        // read the press first: ReleaseCapture sends WM_CAPTURECHANGED, which cancels a press
        // (that is what made Apply / Revert / Close do nothing in 2.3.0)
        int pr = g_th.pressed;
        bool click = pr >= TH_APPLY && pr <= TH_CLOSE && g_th.hot == pr;
        g_th.pressed = g_th.hot = -1;
        g_th.dragThumb = false;
        ReleaseCapture();
        KillTimer(h, 1);
        InvalidateRect(h, nullptr, FALSE);
        if (click) {
            if (pr == TH_APPLY) ApplyTheme(g_th.sel);
            else if (pr == TH_REVERT) { ApplyTheme(g_thStart); thSelect(g_thStart); }
            else CloseThemeOptions();
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if ((HWND)lp != h && (g_th.pressed >= 0 || g_th.dragThumb)) { g_th.pressed = g_th.hot = -1; g_th.dragThumb = false; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_MOUSEWHEEL: {
        UseDpi(h);
        RECT rc; GetClientRect(h, &rc);
        THLayout L = thLayout(rc.right, rc.bottom);
        int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        g_th.top = std::clamp(g_th.top - steps * 3, 0, thMaxTop(L));
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case VK_UP: thSelect(g_th.sel - 1); break;
        case VK_DOWN: thSelect(g_th.sel + 1); break;
        case VK_HOME: thSelect(0); break;
        case VK_END: thSelect(SchemeCount() - 1); break;
        case VK_PRIOR: thSelect(g_th.sel - 6); break;
        case VK_NEXT: thSelect(g_th.sel + 6); break;
        case VK_RETURN: ApplyTheme(g_th.sel); break;
        case VK_ESCAPE: CloseThemeOptions(); break;
        }
        return 0;
    case WM_CLOSE: CloseThemeOptions(); return 0;
    case WM_DESTROY: g_th.bb.release(); if (g_thWnd == h) g_thWnd = nullptr; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
static LRESULT CALLBACK THProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    try {
        return thProc(h, m, wp, lp);
    } catch (...) {
        int keep = CurrentScheme();
        std::wstring sum = CrashRecovered(L"theme options");
        SetScheme(keep);
        if (m == WM_PAINT) ValidateRect(h, nullptr);
        if (GetCapture() == h) ReleaseCapture();
        g.status = L"Internal error in Theme Options: " + sum + L" - details in crash.log";
        Changed();
        return m == WM_CREATE ? 0 : DefWindowProcW(h, m, wp, lp);
    }
}

void OpenThemeOptions() {
    if (!g.main) return;
    if (!g_thWnd) {
        CrashTrail(L"theme options");
        g_thStart = CurrentScheme();
        g_th.sel = CurrentScheme();
        g_th.top = 0;
        g_th.pressed = g_th.hot = -1;
        UseDpi(g.main);
        int W = S(680), H = S(470);
        RECT mr;
        GetWindowRect(g.main, &mr);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(g.main, MONITOR_DEFAULTTONEAREST), &mi);
        int x = (mr.left + mr.right - W) / 2, y = (mr.top + mr.bottom - H) / 2;
        x = std::clamp<int>(x, mi.rcWork.left, std::max<int>(mi.rcWork.left, mi.rcWork.right - W));
        y = std::clamp<int>(y, mi.rcWork.top, std::max<int>(mi.rcWork.top, mi.rcWork.bottom - H));
        g_thWnd = CreateWindowExW(0, kThClass, L"Theme Options", WS_POPUP | WS_SYSMENU, x, y, W, H, g.main, nullptr, g.inst, nullptr);
        if (!g_thWnd) return;
        ShowWindow(g_thWnd, SW_SHOW);
        thSelect(g_th.sel);
    }
    SetForegroundWindow(g_thWnd);
}
HWND ThemeOptionsWindow() { return g_thWnd; }
RECT ThemeOptionsButton(int i) {   // self test: client rectangle of Apply (0), Revert (1), Close (2)
    if (!g_thWnd) return RECT{};
    RECT rc;
    GetClientRect(g_thWnd, &rc);
    UseDpi(g_thWnd);
    return thLayout(rc.right, rc.bottom).btn[std::clamp(i, 0, 2)];
}
RECT ThemeOptionsRow(int i) {   // self test: client rectangle of list row i (scrolled into view)
    if (!g_thWnd) return RECT{};
    RECT rc;
    GetClientRect(g_thWnd, &rc);
    UseDpi(g_thWnd);
    THLayout L = thLayout(rc.right, rc.bottom);
    g_th.top = std::clamp(i, 0, thMaxTop(L));
    return RECT{L.items.left, L.items.top + (i - g_th.top) * L.rowH, L.items.right, L.items.top + (i - g_th.top + 1) * L.rowH};
}
bool ThemeOptionsCapture(Image& out) {   // self test: paint the window and hand back its pixels
    if (!g_thWnd) return false;
    HDC dc = GetDC(g_thWnd);
    paintTH(g_thWnd, dc);
    ReleaseDC(g_thWnd, dc);
    GdiFlush();
    out.w = g_th.bb.w; out.h = g_th.bb.h;
    out.px.assign(g_th.bb.bits, g_th.bb.bits + (size_t)out.w * out.h);
    for (auto& v : out.px) v &= 0xffffff;
    return out.w > 0;
}
int ThemeOptionsSelection() { return g_thWnd ? g_th.sel : -1; }
void RegisterThemeOptions(HINSTANCE inst) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = THProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, 0);
    wc.lpszClassName = kThClass;
    RegisterClassExW(&wc);
}
