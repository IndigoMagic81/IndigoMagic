// The IRIX "Backgrounds: Color Browser": HSV wheel, H/S/V + R/G/B scales, hex entry,
// current/stored swatches. Edits the open slot of the current pattern live.
#include "app.h"
#include "crash.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>

static HWND g_cbWnd = nullptr;
static int g_cbSlot = -1;
static const wchar_t* kCBClass = L"IrixColorBrowser";
static void cbClose(bool revert);
static Config& cbTarget();   // the configuration whose colour is being edited
static std::wstring hexColor(uint32_t c) {
    wchar_t b[16];
    swprintf(b, 16, L"#%06X", c & 0xffffff);
    return b;
}
// Color Browser (Backgrounds: Color Browser)
// ---------------------------------------------------------------------------------------------
struct ColorBrowser {
    int slot = 0, pattern = -1;   // the colour being edited: pattern index + slot
    uint32_t stored = 0, cur = 0;
    double hh = 0, ss = 0, vv = 0;   // hue 0..360, sat 0..1, val 0..1
    int drag = -1, pressed = -1, hot = -1;
    bool active = true, updatingEdit = false;
    HWND edit = nullptr;
    BackBuf bb;
    std::vector<uint32_t> wheel;
    int wheelSize = 0;
    double wheelV = -1;
} g_cb;

enum { CB_WHEEL = 100, CB_SL0 = 110, CB_OK = 120, CB_APPLY, CB_RANDOM, CB_CANCEL, CB_STORED, CB_MENU };
struct CBLayout { FrameRects f; RECT wheel, sl[6], slLabel[6], slVal[6], curLbl, cur, stoLbl, sto, hexLbl, hex, btn[4]; };
static CBLayout cbLayout(int W, int H) {
    CBLayout L;
    L.f = FrameLayout(W, H, false);
    RECT b = L.f.body;
    int d = S(184);
    L.wheel = RECT{b.left + S(16), b.top + S(14), b.left + S(16) + d, b.top + S(14) + d};
    int x0 = L.wheel.right + S(24);
    for (int i = 0; i < 6; i++) {
        int y = b.top + S(16) + i * S(30);
        L.slLabel[i] = RECT{x0, y, x0 + S(80), y + S(18)};
        L.sl[i] = RECT{x0 + S(82), y + S(1), b.right - S(58), y + S(17)};
        L.slVal[i] = RECT{L.sl[i].right + S(8), y, b.right - S(10), y + S(18)};
    }
    int y = L.wheel.bottom + S(14);
    L.curLbl = RECT{b.left + S(16), y, b.left + S(16) + S(58), y + S(36)};
    L.cur = RECT{L.curLbl.right, y, L.curLbl.right + S(66), y + S(36)};
    L.stoLbl = RECT{L.cur.right + S(18), y, L.cur.right + S(18) + S(58), y + S(36)};
    L.sto = RECT{L.stoLbl.right, y, L.stoLbl.right + S(66), y + S(36)};
    L.hexLbl = RECT{L.sto.right + S(22), y, L.sto.right + S(22) + S(40), y + S(36)};
    L.hex = RECT{L.hexLbl.right, y + S(5), L.hexLbl.right + S(96), y + S(31)};
    int bh = S(30), bw = S(88), gap = S(10), by = b.bottom - S(12) - bh;
    for (int i = 0; i < 4; i++) {
        int r = b.right - S(12) - (3 - i) * (bw + gap);
        L.btn[i] = RECT{r - bw, by, r, by + bh};
    }
    return L;
}
static void rgb2hsv(uint32_t c, double& h, double& s, double& v) {
    double r = ((c >> 16) & 255) / 255.0, g = ((c >> 8) & 255) / 255.0, b = (c & 255) / 255.0;
    double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 1e-9) return;   // keep previous hue for greys
    if (mx == r) h = 60 * std::fmod((g - b) / d + 6, 6.0);
    else if (mx == g) h = 60 * ((b - r) / d + 2);
    else h = 60 * ((r - g) / d + 4);
}
static uint32_t hsv2rgb(double h, double s, double v) {
    h = std::fmod(h, 360.0); if (h < 0) h += 360;
    double c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.0, 2.0) - 1)), m = v - c, r = 0, g = 0, b = 0;
    int k = (int)(h / 60) % 6;
    switch (k) { case 0: r = c; g = x; break; case 1: r = x; g = c; break; case 2: g = c; b = x; break;
                 case 3: g = x; b = c; break; case 4: r = x; b = c; break; default: r = c; b = x; }
    auto q = [](double t) { return (uint32_t)std::clamp((int)std::lround(t * 255), 0, 255); };
    return q(r + m) << 16 | q(g + m) << 8 | q(b + m);
}
static void cbSync(bool fromRgb, bool updateEdit = true) {
    if (fromRgb) rgb2hsv(g_cb.cur, g_cb.hh, g_cb.ss, g_cb.vv);
    else g_cb.cur = hsv2rgb(g_cb.hh, g_cb.ss, g_cb.vv);
    if (g_cbSlot >= 0) cbTarget().colors[g_cbSlot] = g_cb.cur;
    if (updateEdit && g_cb.edit) {
        g_cb.updatingEdit = true;
        SetWindowTextW(g_cb.edit, hexColor(g_cb.cur).c_str());
        g_cb.updatingEdit = false;
    }
    if (g_cbWnd) InvalidateRect(g_cbWnd, nullptr, FALSE);
    if (g.main) Changed();
}
static double slValue(int i) {
    switch (i) {
    case 0: return g_cb.hh / 360.0;
    case 1: return g_cb.ss;
    case 2: return g_cb.vv;
    default: return ((g_cb.cur >> (16 - 8 * (i - 3))) & 255) / 255.0;
    }
}
static void slSet(int i, double t) {
    t = std::clamp(t, 0.0, 1.0);
    if (i < 3) {
        if (i == 0) g_cb.hh = t * 360.0; else if (i == 1) g_cb.ss = t; else g_cb.vv = t;
        cbSync(false);
    } else {
        int sh = 16 - 8 * (i - 3);
        g_cb.cur = (g_cb.cur & ~(0xffu << sh)) | ((uint32_t)std::lround(t * 255) << sh);
        cbSync(true);
    }
}
void CloseColorBrowser(bool revert) { cbClose(revert); }
static Config& cbTarget() { return g_cb.pattern >= 0 && g_cb.pattern < (int)g.cfg.size() ? g.cfg[g_cb.pattern] : Cur(); }
static void cbClose(bool revert) {
    if (!g_cbWnd) return;
    if (revert && g_cbSlot >= 0 && g_cb.pattern >= 0 && g_cb.pattern < (int)g.cfg.size()) g.cfg[g_cb.pattern].colors[g_cbSlot] = g_cb.stored;
    HWND w = g_cbWnd;
    g_cbWnd = nullptr;
    g_cbSlot = -1;
    DestroyWindow(w);
    if (g.main) Changed();
}

static void paintCB(HWND h, HDC hdc) {
    UseDpi(h);
    RECT rc;
    GetClientRect(h, &rc);
    g_cb.bb.ensure(rc.right, rc.bottom);
    HDC dc = g_cb.bb.dc;
    CBLayout L = cbLayout(rc.right, rc.bottom);
    Fill(dc, rc, C::body);
    DrawFrame(dc, rc.right, rc.bottom, L"Backgrounds: Color Browser", g_cb.active, false, g_cb.pressed == CB_MENU ? 1 : 0);

    // hue / saturation wheel at the current value
    int d = L.wheel.right - L.wheel.left;
    static COLORREF wheelBg = 0;
    if (g_cb.wheelSize != d || std::fabs(g_cb.wheelV - g_cb.vv) > 1e-6 || wheelBg != C::body) {
        wheelBg = C::body;
        g_cb.wheel.assign((size_t)d * d, 0);
        double R = d / 2.0;
        uint32_t bg = (GetRValue(C::body) << 16) | (GetGValue(C::body) << 8) | GetBValue(C::body);
        for (int y = 0; y < d; y++)
            for (int x = 0; x < d; x++) {
                double dx = x + 0.5 - R, dy = y + 0.5 - R, r = std::sqrt(dx * dx + dy * dy);
                uint32_t c = bg;
                if (r <= R - 1) {
                    double hue = std::atan2(-dy, dx) * 180 / 3.14159265358979;
                    c = hsv2rgb(hue, std::min(1.0, r / (R - 1)), g_cb.vv);
                } else if (r <= R) {
                    c = 0x303030;
                }
                g_cb.wheel[(size_t)y * d + x] = c;
            }
        g_cb.wheelSize = d;
        g_cb.wheelV = g_cb.vv;
    }
    // sliders
    static const wchar_t* names[6] = {L"Hue", L"Saturation", L"Value", L"Red", L"Green", L"Blue"};
    for (int i = 0; i < 6; i++) {
        Text(dc, F().body, names[i], L.slLabel[i], C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Sunken(dc, L.sl[i], 1);
        wchar_t v[16];
        double t = slValue(i);
        if (i == 0) swprintf(v, 16, L"%d", (int)std::lround(g_cb.hh));
        else if (i < 3) swprintf(v, 16, L"%d", (int)std::lround(t * 100));
        else swprintf(v, 16, L"%d", (int)std::lround(t * 255));
        Text(dc, F().body, v, L.slVal[i], C::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    Text(dc, F().body, L"Current\nColor:", L.curLbl, C::text, DT_LEFT);
    Text(dc, F().body, L"Stored\nColor:", L.stoLbl, C::text, DT_LEFT);
    Text(dc, F().body, L"Hex:", L.hexLbl, C::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Fill(dc, L.cur, RGB((g_cb.cur >> 16) & 255, (g_cb.cur >> 8) & 255, g_cb.cur & 255));
    Bevel(dc, L.cur, 1, C::swatchBorder, C::swatchBorder);
    Fill(dc, L.sto, RGB((g_cb.stored >> 16) & 255, (g_cb.stored >> 8) & 255, g_cb.stored & 255));
    Bevel(dc, L.sto, 1, C::swatchBorder, C::swatchBorder);
    Sunken(dc, RECT{L.hex.left - 2, L.hex.top - 2, L.hex.right + 2, L.hex.bottom + 2}, 2);
    static const wchar_t* bn[4] = {L"OK", L"Apply", L"Random", L"Cancel"};
    for (int i = 0; i < 4; i++) PushButton(g_cb.bb, L.btn[i], bn[i], g_cb.pressed == CB_OK + i && g_cb.hot == CB_OK + i, true);

    GdiFlush();
    uint32_t* bits = g_cb.bb.bits;
    for (int y = 0; y < d; y++)
        std::copy(&g_cb.wheel[(size_t)y * d], &g_cb.wheel[(size_t)y * d] + d, bits + (size_t)(L.wheel.top + y) * g_cb.bb.w + L.wheel.left);
    // WYSIWYG slider troughs: each shows the ramp for its channel
    for (int i = 0; i < 6; i++) {
        RECT r{L.sl[i].left + 1, L.sl[i].top + 1, L.sl[i].right - 1, L.sl[i].bottom - 1};
        int w = r.right - r.left;
        for (int x = 0; x < w; x++) {
            double t = (x + 0.5) / w;
            uint32_t c;
            if (i == 0) c = hsv2rgb(t * 360, std::max(0.35, g_cb.ss), std::max(0.5, g_cb.vv));
            else if (i == 1) c = hsv2rgb(g_cb.hh, t, g_cb.vv);
            else if (i == 2) c = hsv2rgb(g_cb.hh, g_cb.ss, t);
            else { int sh = 16 - 8 * (i - 3); c = (g_cb.cur & ~(0xffu << sh)) | ((uint32_t)(t * 255) << sh); }
            for (int y = r.top; y < r.bottom; y++) bits[(size_t)y * g_cb.bb.w + r.left + x] = c;
        }
    }
    // thumbs + wheel marker (GDI again)
    for (int i = 0; i < 6; i++) {
        RECT r = L.sl[i];
        int tw = S(12), x = r.left + 1 + (int)std::lround(slValue(i) * (r.right - r.left - 2 - tw));
        RECT t{x, r.top - S(3), x + tw, r.bottom + S(3)};
        Fill(dc, t, C::sb);
        Bevel(dc, t, 1, C::sbLight, C::sbDarker);
        VLine(dc, (t.left + t.right) / 2 - 1, t.top + 2, t.bottom - 2, C::sbDark);
        VLine(dc, (t.left + t.right) / 2, t.top + 2, t.bottom - 2, C::sbLight);
    }
    {
        double R = d / 2.0 - 1, a = g_cb.hh * 3.14159265358979 / 180;
        int mx = L.wheel.left + (int)std::lround(d / 2.0 + std::cos(a) * g_cb.ss * R);
        int my = L.wheel.top + (int)std::lround(d / 2.0 - std::sin(a) * g_cb.ss * R);
        int r = S(5);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, RGB(0, 0, 0));
        Ellipse(dc, mx - r, my - r, mx + r + 1, my + r + 1);
        SetDCPenColor(dc, RGB(255, 255, 255));
        Ellipse(dc, mx - r + 1, my - r + 1, mx + r, my + r);
    }
    GdiFlush();
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
}
static int hitCB(POINT p, const CBLayout& L) {
    if (In(L.f.menu, p)) return CB_MENU;
    int d = L.wheel.right - L.wheel.left;
    double dx = p.x - L.wheel.left - d / 2.0, dy = p.y - L.wheel.top - d / 2.0;
    if (dx * dx + dy * dy <= (d / 2.0) * (d / 2.0)) return CB_WHEEL;
    for (int i = 0; i < 6; i++) {
        RECT r = L.sl[i];
        InflateRect(&r, 0, S(4));
        if (In(r, p)) return CB_SL0 + i;
    }
    for (int i = 0; i < 4; i++) if (In(L.btn[i], p)) return CB_OK + i;
    if (In(L.sto, p)) return CB_STORED;
    return -1;
}
static void cbDrag(int what, POINT p, const CBLayout& L) {
    if (what == CB_WHEEL) {
        int d = L.wheel.right - L.wheel.left;
        double dx = p.x - L.wheel.left - d / 2.0, dy = -(p.y - L.wheel.top - d / 2.0);
        g_cb.hh = std::fmod(std::atan2(dy, dx) * 180 / 3.14159265358979 + 360, 360.0);
        g_cb.ss = std::min(1.0, std::sqrt(dx * dx + dy * dy) / (d / 2.0 - 1));
        if (g_cb.vv < 0.05) g_cb.vv = 1.0;   // picking from a black wheel would do nothing
        cbSync(false);
    } else if (what >= CB_SL0 && what < CB_SL0 + 6) {
        RECT r = L.sl[what - CB_SL0];
        slSet(what - CB_SL0, (double)(p.x - r.left - S(6)) / (r.right - r.left - S(12)));
    }
}

static LRESULT cbProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        UseDpi(h);
        g_cb.edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE, 0, 0, 10, 10, h, (HMENU)1,
                                    g.inst, nullptr);
        SendMessageW(g_cb.edit, EM_SETLIMITTEXT, 7, 0);
        SetupFrameWindow(h);
        return 0;
    }
    case WM_SIZE: {
        UseDpi(h);
        CBLayout L = cbLayout(LOWORD(lp), HIWORD(lp));
        if (g_cb.edit) {
            SendMessageW(g_cb.edit, WM_SETFONT, (WPARAM)F().body, TRUE);
            MoveWindow(g_cb.edit, L.hex.left, L.hex.top + S(4), L.hex.right - L.hex.left, L.hex.bottom - L.hex.top - S(6), TRUE);
        }
        return 0;
    }
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        g_cb.wheelSize = 0;
        return 0;
    }
    case WM_CTLCOLOREDIT: return EditColors((HDC)wp);
    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE && !g_cb.updatingEdit) {
            wchar_t b[16];
            GetWindowTextW(g_cb.edit, b, 16);
            std::wstring s = b;
            if (!s.empty() && s[0] == L'#') s.erase(0, 1);
            if (s.size() == 6 && s.find_first_not_of(L"0123456789ABCDEFabcdef") == std::wstring::npos) {
                g_cb.cur = (uint32_t)wcstoul(s.c_str(), nullptr, 16);
                cbSync(true, false);
            }
        }
        return 0;
    case WM_NCCALCSIZE: return FrameNcCalcSize(h, wp, lp);
    case WM_NCHITTEST: return FrameHitTest(h, lp, false, false);
    case WM_NCACTIVATE: g_cb.active = wp != 0; InvalidateRect(h, nullptr, FALSE); return DefWindowProcW(h, m, wp, -1);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        if (!IsIconic(h) && rc.right >= S(200) && rc.bottom >= S(200)) paintCB(h, hdc);   // never lay out a minimised window
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        UseDpi(h);
        RECT rc; GetClientRect(h, &rc);
        CBLayout L = cbLayout(rc.right, rc.bottom);
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int w = hitCB(p, L);
        if (w == CB_MENU) { cbClose(false); return 0; }
        if (w == CB_STORED) { g_cb.cur = g_cb.stored; cbSync(true); return 0; }
        if (w == CB_WHEEL || (w >= CB_SL0 && w < CB_SL0 + 6)) { g_cb.drag = w; SetCapture(h); cbDrag(w, p, L); return 0; }
        if (w >= CB_OK && w <= CB_CANCEL) { g_cb.pressed = g_cb.hot = w; SetCapture(h); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSEMOVE: {
        UseDpi(h);
        RECT rc; GetClientRect(h, &rc);
        CBLayout L = cbLayout(rc.right, rc.bottom);
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (g_cb.drag >= 0) cbDrag(g_cb.drag, p, L);
        else if (g_cb.pressed >= 0) {
            int hot = hitCB(p, L) == g_cb.pressed ? g_cb.pressed : -1;
            if (hot != g_cb.hot) { g_cb.hot = hot; InvalidateRect(h, nullptr, FALSE); }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        ReleaseCapture();
        int pr = g_cb.pressed;
        bool click = pr >= 0 && g_cb.hot == pr;
        g_cb.drag = g_cb.pressed = -1;
        InvalidateRect(h, nullptr, FALSE);
        if (click) {
            if (pr == CB_OK) cbClose(false);
            else if (pr == CB_APPLY) { g_cb.stored = g_cb.cur; InvalidateRect(h, nullptr, FALSE); }
            else if (pr == CB_RANDOM) {
                g_cb.cur = RandomSlot(g.sel, g.harmony, Cur().colors, g_cbSlot, NextRandom());
                cbSync(true);
            }
            else if (pr == CB_CANCEL) cbClose(true);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) cbClose(true);
        if (wp == VK_RETURN) cbClose(false);
        return 0;
    case WM_CLOSE: cbClose(false); return 0;
    case WM_DESTROY: g_cb.edit = nullptr; g_cb.bb.release(); g_cb.wheelSize = 0; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
static LRESULT CALLBACK CBProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    try {
        return cbProc(h, m, wp, lp);
    } catch (...) {
        std::wstring sum = CrashRecovered(L"color browser");
        if (m == WM_PAINT) ValidateRect(h, nullptr);
        if (GetCapture() == h) ReleaseCapture();
        g.status = L"Internal error in the Color Browser: " + sum + L" - details in crash.log";
        Changed();
        return m == WM_CREATE ? 0 : DefWindowProcW(h, m, wp, lp);
    }
}

void OpenColorBrowser(int slot) {
    if (slot < 0 || slot > 3) return;
    Edit();
    g_cbSlot = slot;
    g_cb.slot = slot;
    g_cb.pattern = g.sel;
    g_cb.stored = g_cb.cur = Cur().colors[slot];
    g_cb.hh = 0;
    rgb2hsv(g_cb.cur, g_cb.hh, g_cb.ss, g_cb.vv);
    if (!g_cbWnd) {
        UseDpi(g.main);
        int W = S(600), H = S(342);
        RECT mr;
        GetWindowRect(g.main, &mr);
        int x = mr.right + S(8), y = mr.top + S(60);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(g.main, MONITOR_DEFAULTTONEAREST), &mi);
        if (x + W > mi.rcWork.right) x = std::max<int>(mi.rcWork.left, mr.right - W - S(30));
        if (y + H > mi.rcWork.bottom) y = mi.rcWork.bottom - H;
        g_cbWnd = CreateWindowExW(0, kCBClass, L"Backgrounds: Color Browser", WS_POPUP | WS_SYSMENU | WS_CLIPCHILDREN, x, y, W, H, g.main,
                                  nullptr, g.inst, nullptr);
        ShowWindow(g_cbWnd, SW_SHOW);
    }
    cbSync(true);
    SetForegroundWindow(g_cbWnd);
}


int ColorBrowserSlot() { return g_cbWnd ? g_cbSlot : -1; }
HWND ColorBrowserWindow() { return g_cbWnd; }
void ColorBrowserSync() {
    if (!g_cbWnd || g_cbSlot < 0) return;
    g_cb.cur = cbTarget().colors[g_cbSlot];
    rgb2hsv(g_cb.cur, g_cb.hh, g_cb.ss, g_cb.vv);
    if (g_cb.edit) {
        g_cb.updatingEdit = true;
        SetWindowTextW(g_cb.edit, hexColor(g_cb.cur).c_str());
        g_cb.updatingEdit = false;
    }
    InvalidateRect(g_cbWnd, nullptr, FALSE);
}
bool IsColorBrowserMessage(MSG& msg) {
    if (g_cbWnd && g_cb.edit && msg.hwnd == g_cb.edit && msg.message == WM_KEYDOWN && (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE)) {
        SendMessageW(g_cbWnd, WM_KEYDOWN, msg.wParam, 0);
        return true;
    }
    return false;
}
void RegisterColorBrowser(HINSTANCE inst) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = CBProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, 0);
    wc.lpszClassName = kCBClass;
    RegisterClassExW(&wc);
}
