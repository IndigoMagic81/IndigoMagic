// Motif / 4Dwm style drawing helpers shared by every window of the app.
#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "core.h"
#include <map>
#include <string>

extern int g_dpi;
inline int S(double v) { return (int)(v * g_dpi / 96.0 + (v >= 0 ? 0.5 : -0.5)); }
void UseDpi(HWND h);                       // g_dpi for this window (scaled down if the panel would not fit)
const int kPanelMinW = 960, kPanelMinH = 712;   // main panel minimum, logical pixels (incl. a little slack)
int FitDpi(HMONITOR mon, int dpi);
extern int g_dpiOverride;                  // self test: force a UI scale

struct Fonts { HFONT title, bold, body, small, list, big, italic, smallBold, menu; };
Fonts& F();

// ---------------------------------------------------------------------------------------------
// Themes: the IRIX 6.5 desktop colour schemes (Indigo Magic, Rio, Gotham, Inverness ...), taken
// from /usr/lib/X11/schemes/*/BaseColorPalette by tools/gen_schemes.py. Every colour the UI draws
// with comes from the current scheme's roles - window background, buttons, text fields, lists,
// scrollbars, toggle indicators, check / radio marks, the 4Dwm frame - and the 3-D shadows are
// derived from each widget's own background, as SGI's Motif did.
// ---------------------------------------------------------------------------------------------
struct SchemeDef {
    const wchar_t* key;        // IRIX folder name (IndigoMagic, Rio, KeyWest ...)
    const wchar_t* name;       // shown in Theme Options
    const wchar_t* desc;       // the scheme's own SchemeDescription
    const wchar_t* aliases;    // '|'-separated names it is also known by (IRIX 6.2 names)
    bool dark;                 // IsDarkScheme
    uint32_t c[26];            // roles, in the order of enum SchemeRole
};
enum SchemeRole {
    SR_BASIC, SR_TEXT, SR_BUTTON, SR_TEXTFIELD, SR_LIST, SR_TROUGH, SR_SBCONTROL, SR_INDICATOR, SR_RADIO, SR_CHECK,
    SR_SELECTFILL, SR_TEXTSELBG, SR_TEXTSELFG, SR_DISABLEDTEXT, SR_DRAWINGAREA, SR_WMBG, SR_WMFG, SR_WMACTIVEBG, SR_WMACTIVEFG,
    SR_ALT5, SR_READONLY, SR_TEXTBG, SR_RED, SR_GREEN, SR_BLUE, SR_ORANGE, SR_COUNT
};
int SchemeCount();
const SchemeDef& Scheme(int i);
int FindScheme(const std::wstring& keyOrAlias);   // -1 when unknown
int CurrentScheme();
void SetScheme(int i);                              // recomputes every colour in C::
inline COLORREF Ref(uint32_t rgb) { return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }

struct Shade { COLORREF light, dark, darker, armed; };
Shade Shades(COLORREF base);        // Motif / SGI 3-D shadows (and the pressed "armed" fill) of a widget colour
Shade WmShades(COLORREF base);      // the 4Dwm frame's own shading
COLORREF Mix(COLORREF a, COLORREF b, float t);
float Brightness(COLORREF c);       // Motif's brightness measure, 0..1
COLORREF Readable(COLORREF want, COLORREF bg, float minRatio = 3.0f);   // want, or black / white if it would not read
COLORREF StatusColor(int level);   // 0 poor .. 3 excellent, readable on C::body
LRESULT EditColors(HDC dc);         // WM_CTLCOLOREDIT for the scheme's TextFieldBackground

namespace C {
extern COLORREF body, light, dark, darker,                 // window background and its shadows
    face, faceLight, faceDark, faceDarker, faceArmed,       // push buttons, option menus
    list, listLight, listDark, listSelBg, listSelFg,        // scrolled lists
    trough, sb, sbLight, sbDark, sbDarker, sbArmed,         // scrollbars and scale sliders
    text, insens, disabledText, field, fieldText, indicator, check, radio, drawing, fill, quickHelp,
    swatchBorder,
    wmFace, wmLight, wmDark, wmText, wmFaceI, wmLightI, wmDarkI, wmTextI;   // 4Dwm frame (active / inactive)
extern bool darkScheme;
}

// Owner-drawn popup menus in the current scheme (Motif menus: window background, bold-oblique
// labels, the armed item raised). ThemeMenu() converts a menu (and its submenus) just before it is
// shown; the owner window passes WM_MEASUREITEM / WM_DRAWITEM to MenuMeasure / MenuDraw.
void ThemeMenu(HMENU m);
bool MenuMeasure(MEASUREITEMSTRUCT* mi);
bool MenuDraw(DRAWITEMSTRUCT* di);

struct BackBuf {
    HDC dc = nullptr; HBITMAP bmp = nullptr, old = nullptr; uint32_t* bits = nullptr; int w = 0, h = 0;
    void ensure(int W, int H);
    void release();
    ~BackBuf() { release(); }
};

void Fill(HDC dc, RECT r, COLORREF c);
void HLine(HDC dc, int x1, int x2, int y, COLORREF c);
void VLine(HDC dc, int x, int y1, int y2, COLORREF c);
void Bevel(HDC dc, RECT r, int t, COLORREF tl, COLORREF br);
void Raised(HDC dc, RECT r, int t = 2);
void Sunken(HDC dc, RECT r, int t = 2);
void Text(HDC dc, HFONT f, const std::wstring& s, RECT r, COLORREF c, UINT fmt);
void StippleText(BackBuf& bb, HFONT f, const std::wstring& s, RECT r, COLORREF c, UINT fmt);
void Label(BackBuf& bb, HFONT f, const std::wstring& s, RECT r, bool enabled, UINT fmt);
void PushButton(BackBuf& bb, RECT r, const std::wstring& label, bool pressed, bool enabled);
void Diamond(HDC dc, int cx, int cy, int r, bool selected, bool enabled);
void CheckBox(HDC dc, RECT r, bool on, bool enabled);
void Arrow(HDC dc, RECT r, bool up, bool pressed);
inline bool In(const RECT& r, POINT p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

// 4Dwm window frame (drawn inside our own borderless windows)
struct FrameRects { RECT menu, title, minimize, maximize, body; int border, titleH; };
FrameRects FrameLayout(int W, int H, bool minmax);
void DrawFrame(HDC dc, int W, int H, const std::wstring& title, bool active, bool minmax, int pressedBtn /*0 none,1 menu,2 min,3 max*/);
LRESULT FrameHitTest(HWND h, LPARAM lp, bool resizable, bool minmax);
void SetupFrameWindow(HWND h);
LRESULT FrameNcCalcSize(HWND h, WPARAM wp, LPARAM lp);

// Motif scale (horizontal): trough + raised thumb; t in [0,1]
void ScaleDraw(BackBuf& bb, RECT trough, float t, bool enabled, bool pressed);
RECT ScaleThumb(RECT trough, float t);
// Motif option menu button: current value + cascade indicator
void OptionButton(BackBuf& bb, RECT r, const std::wstring& text, bool pressed, bool enabled);
void TabButton(BackBuf& bb, RECT r, const std::wstring& text, bool active, bool hot);
void EtchedGroup(BackBuf& bb, RECT r, const std::wstring& title);
void GlyphLock(HDC dc, RECT r, bool locked, bool enabled);
void GlyphDice(HDC dc, RECT r, int pips, bool enabled);
// modal text prompt; returns false on Cancel
bool MotifPrompt(HWND owner, const std::wstring& title, const std::wstring& prompt, std::wstring& value);

// modal Motif-style message dialog; returns index of pressed button (or last button on close)
int MotifDialog(HWND owner, const std::wstring& title, const std::wstring& text, const std::vector<std::wstring>& buttons, int icon /*0 info,1 question,2 warning*/, int width = 440);
void RegisterMotifDialogClass(HINSTANCE inst);   // also registers the prompt dialog
