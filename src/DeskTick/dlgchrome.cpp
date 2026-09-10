// dlgchrome.cpp — the chrome both windows wear: dark-mode theming, the title
// bar, and the message font.
//
// The configure dialog and the About box want the same theming, the same frame
// and the same DPI-scaled message font, so it lives once, here. The frame style
// itself is in app.h rather than this file, since CreateWindowExW and
// AdjustWindowRectExForDpi must be handed the same one and both windows call
// both.
//
// Windows themes the *glyphs* — the checkbox tick, the combo arrow, the button
// frame — only if the control is told which dark theme class to use; it never
// paints the surface behind them, which is what each window's WM_CTLCOLOR*
// handlers are for. Two names cover every control in this app: combo boxes and
// edits want CFD, everything else Explorer.
//
// Light mode passes nullptr, which is "no override" — so turning dark mode off
// in Windows really does restore the stock dialog, not an imitation.

#include <windows.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include "app.h"

#pragma comment(lib, "uxtheme")
#pragma comment(lib, "dwmapi")

// The frame's extended style. DLG_STYLE is still a constant in app.h, but this
// half has a runtime bit: WS_EX_LAYOUTRTL mirrors a window's whole child layout
// for Arabic and Hebrew, so the two dialogs — which lay their rows out in code,
// left to right — come out right-to-left with no coordinate touched.
//
// It must be set at creation to mirror children, and the same value has to
// reach AdjustWindowRectExForDpi, which is the reason this is one function
// rather than an OR at four call sites.
DWORD DlgExStyle()
{
    return WS_EX_DLGMODALFRAME | WS_EX_TOPMOST | (LocIsRTL() ? WS_EX_LAYOUTRTL : 0);
}

static HBRUSH s_darkBrush;

// The background brush for the current theme. The dark one is created on
// first use and kept for the process lifetime; the light one is the system's.
HBRUSH ThemeBrush()
{
    if (!IsDarkMode()) return GetSysColorBrush(COLOR_3DFACE);
    if (!s_darkBrush) s_darkBrush = CreateSolidBrush(DARK_BG);
    return s_darkBrush;
}

// Point one control at its dark theme class, or clear the override in light
// mode. `darkClass` is "DarkMode_CFD" for combos and edits, "DarkMode_Explorer"
// for everything else here.
void ThemeControl(HWND c, const WCHAR* darkClass)
{
    SetWindowTheme(c, IsDarkMode() ? darkClass : nullptr, nullptr);
}

// Darken a window's title bar, which DWM owns and the theme classes above
// cannot reach.
void ThemeCaption(HWND h)
{
    BOOL dark = IsDarkMode();
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE; it was 19 before 19H1, and an
    // unsupported attribute just fails, so try the modern one first.
    if (FAILED(DwmSetWindowAttribute(h, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(h, 19, &dark, sizeof(dark));
}

// The system message font at this DPI, as a LOGFONTW rather than an HFONT:
// the About box derives two more faces from it — a larger semibold title and a
// semibold heading — and needs the fields to do that.
//
// Neither window names a font family anywhere, so both follow whatever Windows
// is set to, including a user's larger text. SystemParametersInfoForDpi is the
// accurate call; the plain one is the fallback where it is unavailable.
void DlgMessageLogFont(UINT dpi, LOGFONTW* out)
{
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi))
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    *out = ncm.lfMessageFont;
}
