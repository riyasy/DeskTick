// about.cpp — the About box.
//
// The frame, the message font, the DPI scaling and the dark-mode helpers it
// shares with the configure dialog are in dlgchrome.cpp. Nothing in this file
// touches a setting or a face — the only thing it reads is version.h.
//
// Modeless, like the configure dialog, and for a harder reason: a modal
// DialogBox runs its own message loop, and the engine's loop is what waits on
// the clock's timer — going modal would freeze the clock for as long as the
// box was open.

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <strsafe.h>
#include "app.h"
#include "version.h"                     // shared with DeskTick.rc — see there

#pragma comment(lib, "comctl32")
#pragma comment(lib, "shell32")

static const int ICON_FLYPHOTOS = 101, ICON_LETITRAIN = 102;   // DeskTick.rc

static const int A_MARGIN = 16, A_WIDTH = 372, A_ICON = 40, A_GAP = 14;

static const int ID_LINK = 901;         // the mailto link
static const int ID_APP0 = 910;         // + app index; its icon and name share one

static HWND  s_about;
static HFONT s_aFont, s_aTitle, s_aHead;    // body, "DeskTick", section/app names

struct OtherApp {
    int icon; const WCHAR* name; const WCHAR* blurb;
    const WCHAR* store;                 // ms-windows-store: opens the Store app
    const WCHAR* web;                   // https: used only if that scheme is dead
};

// Two URLs each, and the order matters. ms-windows-store://pdp goes straight
// to the Store app; an https://apps.microsoft.com link is a web page, so the
// browser wins it and the Store only opens if the page decides to hand off.
// The scheme is not registered on every Windows (LTSC, Server, a stripped
// image), so the web page is the fallback for a click that would otherwise do
// nothing — never the first choice.
//
// cid is the custom campaign id Partner Center reports on, under app page views
// and conversions by campaign id, attributing installs within 24 hours of the
// click. Same value on both URLs so either route attributes the same, and named
// for the surface (as the READMEs' cid=GitHubRelease is) so a second place that
// links to the Store can be told apart from this one.
static const OtherApp OTHER_APPS[] = {
    { ICON_FLYPHOTOS, L"FlyPhotos",
      L"Fast, lightweight, and minimalist photo viewer designed for the modern Windows",
      L"ms-windows-store://pdp/?productid=9PMSK128V1QT&cid=DeskTickAbout",
      L"https://apps.microsoft.com/detail/9pmsk128v1qt?cid=DeskTickAbout&mode=full" },
    { ICON_LETITRAIN, L"Let It Rain",
      L"Desktop Rain and Snow Simulator for Windows",
      L"ms-windows-store://pdp/?productid=9P1H1VCJHJZP&cid=DeskTickAbout",
      L"https://apps.microsoft.com/detail/9p1h1vcjhjzp?cid=DeskTickAbout&mode=full" },
};

// One handle per app above, sized from the table rather than counted by hand.
// The fill loop already runs to _countof(OTHER_APPS), so a third entry there
// would have written past a literal [2] — and the two cleanup loops, counting
// by hand as well, would have gone on freeing only the first two. Declared
// after the table so there is nothing left to keep in sync.
static HICON s_aIcons[_countof(OTHER_APPS)];

// What `s` actually measures in font `f`: pass w to wrap into it and read
// r.bottom, or 0 for one line and read r.right. DT_CALCRECT is the only way to
// know — a STATIC will not tell you, and guessing leaves either a blank line
// under a short blurb or a clickable strip of nothing beside a short name.
static RECT TextExtent(HWND p, HFONT f, const WCHAR* s, int w)
{
    HDC dc = GetDC(p);
    HGDIOBJ old = SelectObject(dc, f);
    RECT r = { 0, 0, w, 0 };
    DrawTextW(dc, s, -1, &r,
              DT_CALCRECT | DT_LEFT | (w ? DT_WORDBREAK : DT_SINGLELINE));
    SelectObject(dc, old);
    ReleaseDC(p, dc);
    return r;
}

// One text row. Returns the y below it, so the layout below reads as a column
// of rows and no coordinate is written twice.
static int AboutText(HWND p, HINSTANCE inst, const WCHAR* s, HFONT f,
                     int x, int y, int w, int h, int id = 0, DWORD extra = 0)
{
    HWND t = CreateWindowExW(0, L"STATIC", s, WS_CHILD | WS_VISIBLE | SS_LEFT | extra,
                             x, y, w, h, p, (HMENU)(INT_PTR)id, inst, nullptr);
    SendMessageW(t, WM_SETFONT, (WPARAM)f, TRUE);
    return y + h;
}

// Lay the box out top to bottom at the current DPI: name and version, the
// feedback link, then one row per app in OTHER_APPS. Every font and icon is
// rebuilt here, so this is also the DPI-change and theme-change path.
static void AboutBuild()
{
    HWND c;
    while ((c = GetWindow(s_about, GW_CHILD)) != nullptr) DestroyWindow(c);
    if (s_aFont)  DeleteObject(s_aFont);
    if (s_aTitle) DeleteObject(s_aTitle);
    if (s_aHead)  DeleteObject(s_aHead);
    for (int i = 0; i < (int)_countof(s_aIcons); i++)
        if (s_aIcons[i]) DestroyIcon(s_aIcons[i]);

    UINT dpi = GetDpiForWindow(s_about);
    #define A(x) MulDiv(x, (int)dpi, 96)

    LOGFONTW lf;
    DlgMessageLogFont(dpi, &lf);
    s_aFont = CreateFontIndirectW(&lf);
    // Derived from the message font rather than naming a family, so the box
    // follows whatever Windows is set to — including a user's larger text.
    LOGFONTW big = lf; big.lfHeight = lf.lfHeight * 9 / 5; big.lfWeight = FW_SEMIBOLD;
    s_aTitle = CreateFontIndirectW(&big);
    LOGFONTW mid = lf; mid.lfWeight = FW_SEMIBOLD;
    s_aHead = CreateFontIndirectW(&mid);

    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(s_about, GWLP_HINSTANCE);
    const int x = A(A_MARGIN), w = A(A_WIDTH) - 2 * A(A_MARGIN);
    int y = A(A_MARGIN);

    // From version.h, so this box and the exe's own properties cannot disagree.
    // _CRT_WIDE makes a wide literal of the narrow macro.
    y = AboutText(s_about, inst, _CRT_WIDE(VER_PRODUCT), s_aTitle, x, y, w, A(30));
    y = AboutText(s_about, inst, _CRT_WIDE(VER_DISPLAY), s_aFont, x, y + A(2), w, A(18));

    // Assembled from three pieces rather than drawn from VER_COPYRIGHT whole,
    // because only the last piece is prose. The sign and the holder are
    // identity: hand a translator "© RYF Tools. All rights reserved." as
    // one key and every one of them has to retype the holder inside their
    // value, where a typo is a wrong copyright notice — and rebranding would
    // silently drop all of the translations at once, the key having changed.
    // So the holder comes from VER_COMPANY untranslated and only the sentence
    // goes through T().
    //
    // ©, not the \xA9 version.h uses and not a pasted sign: this is a wide
    // literal in a BOM-less .cpp, so a universal character name is the only
    // escape the compiler resolves whatever the file's encoding happens to be.
    // VER_COPYRIGHT keeps its narrow \xA9 form for DeskTick.rc, now its only
    // consumer.
    WCHAR copyright[160];
    StringCchPrintfW(copyright, _countof(copyright), L"\u00A9 %s. %s",
                     _CRT_WIDE(VER_COMPANY), T(L"All rights reserved."));
    y = AboutText(s_about, inst, copyright, s_aFont, x, y, w, A(18));

    // Height measured rather than fixed at one line, for the same reason the
    // blurbs below are: this sentence is short in English and not in every
    // language, and a fixed A(18) would clip the second line off.
    const WCHAR* feedback = T(L"Report issues or send feedback to");
    y = AboutText(s_about, inst, feedback, s_aFont, x, y + A(14), w,
                  max(A(18), (int)TextExtent(s_about, s_aFont, feedback, w).bottom));
    // A SysLink rather than blue static text: it gets the hand cursor, keyboard
    // focus and the theme's own link colour for free, in both light and dark.
    HWND link = CreateWindowExW(0, WC_LINK,
                                L"<a href=\"mailto:ryftools@outlook.com\">ryftools@outlook.com</a>",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                x, y, w, A(20), s_about,
                                (HMENU)(INT_PTR)ID_LINK, inst, nullptr);
    SendMessageW(link, WM_SETFONT, (WPARAM)s_aFont, TRUE);
    y += A(20);

    y = AboutText(s_about, inst, T(L"Other apps"), s_aHead, x, y + A(18), w, A(20));
    y += A(6);

    for (int i = 0; i < (int)_countof(OTHER_APPS); i++) {
        s_aIcons[i] = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(OTHER_APPS[i].icon),
                                        IMAGE_ICON, A(A_ICON), A(A_ICON), 0);
        // SS_NOTIFY on the icon and the name, sharing one id: both open the same
        // Store page, and nothing here looks either control up by id.
        HWND ic = CreateWindowExW(0, L"STATIC", nullptr,
                                  WS_CHILD | WS_VISIBLE | SS_ICON | SS_REALSIZECONTROL | SS_NOTIFY,
                                  x, y, A(A_ICON), A(A_ICON), s_about,
                                  (HMENU)(INT_PTR)(ID_APP0 + i), inst, nullptr);
        SendMessageW(ic, STM_SETICON, (WPARAM)s_aIcons[i], 0);

        const int tx = x + A(A_ICON + A_GAP), tw = w - A(A_ICON + A_GAP);
        // The name is only as wide as the name: it is clickable, and a static
        // stretched to the margin would put the hand cursor over empty space.
        int nw = TextExtent(s_about, s_aHead, OTHER_APPS[i].name, 0).right;
        int ty = AboutText(s_about, inst, OTHER_APPS[i].name, s_aHead, tx, y,
                           min(nw, tw), A(18), ID_APP0 + i, SS_NOTIFY);
        // Height comes from DT_CALCRECT: one blurb wraps to two lines and the
        // other doesn't, and a fixed two-line box would leave the short one
        // trailing an empty line — visible as slack under the last row. Which
        // is also why the translated blurb needs nothing done to it: the row
        // was already sized to whatever the text measures.
        // The app *names* stay untranslated above — they are products, not copy.
        const WCHAR* blurb = T(OTHER_APPS[i].blurb);
        ty = AboutText(s_about, inst, blurb, s_aFont, tx, ty, tw,
                       TextExtent(s_about, s_aFont, blurb, tw).bottom);
        // Keep the row at least as tall as its icon, so the next one clears it.
        y = max(ty, y + A(A_ICON)) + A(12);
    }

    RECT rc = { 0, 0, A(A_WIDTH), y + A(A_MARGIN) - A(12) };
    AdjustWindowRectExForDpi(&rc, DLG_STYLE, FALSE, DlgExStyle(), dpi);
    SetWindowPos(s_about, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ClampToMonitor(s_about);
    InvalidateRect(s_about, nullptr, TRUE);
    #undef A
}

// The About box's handler: the two kinds of click it can take (an app row,
// the mailto link) plus the same theme and DPI painting as the config dialog.
static LRESULT CALLBACK AboutProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND: {
        if (LOWORD(wp) == IDCANCEL || LOWORD(wp) == IDOK) { DestroyWindow(hwnd); return 0; }
        int app = LOWORD(wp) - ID_APP0;          // STN_CLICKED on an icon or a name
        if (app >= 0 && app < (int)_countof(OTHER_APPS)) {
            // ShellExecute returns <= 32 when nothing claims the scheme, which
            // is the only way to find out — so try the Store, then the web page.
            if ((INT_PTR)ShellExecuteW(hwnd, L"open", OTHER_APPS[app].store,
                                       nullptr, nullptr, SW_SHOWNORMAL) <= 32)
                ShellExecuteW(hwnd, L"open", OTHER_APPS[app].web,
                              nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }
    case WM_SETCURSOR: {
        // A child's WM_SETCURSOR reaches us through its DefWindowProc, so the
        // hand for both app rows is one handler rather than a subclass each.
        int id = GetDlgCtrlID((HWND)wp);
        if (id >= ID_APP0 && id < ID_APP0 + (int)_countof(OTHER_APPS)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }
    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)lp;
        if (n->idFrom == ID_LINK && (n->code == NM_CLICK || n->code == NM_RETURN)) {
            // The href is in the control, so nothing here has to know the address.
            ShellExecuteW(hwnd, L"open", ((NMLINK*)lp)->item.szUrl,
                          nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, ThemeBrush());
        return 1;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, IsDarkMode() ? DARK_FG : GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)ThemeBrush();
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        AboutBuild();
        return 0;
    }
    case WM_DESTROY:
        if (s_aFont)  { DeleteObject(s_aFont);  s_aFont  = nullptr; }
        if (s_aTitle) { DeleteObject(s_aTitle); s_aTitle = nullptr; }
        if (s_aHead)  { DeleteObject(s_aHead);  s_aHead  = nullptr; }
        for (int i = 0; i < (int)_countof(s_aIcons); i++)
            if (s_aIcons[i]) { DestroyIcon(s_aIcons[i]); s_aIcons[i] = nullptr; }
        s_about = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Open the About box, or raise it if it is already up. `owner` only places
// the window beside the clock; like the dialog, it is deliberately ownerless.
void AboutShow(HWND owner)
{
    if (s_about) { SetForegroundWindow(s_about); return; }

    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        // The only comctl32 class this app asks for by name — the standard
        // controls elsewhere are user32's, redirected to v6 by the manifest.
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LINK_CLASS };
        InitCommonControlsEx(&icc);
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = AboutProc;
        wc.hInstance     = inst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;                 // WM_ERASEBKGND: theme-dependent
        wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(ICON_APP));
        wc.lpszClassName = L"ClockAbout";
        RegisterClassW(&wc);
        registered = true;
    }

    RECT rc; GetWindowRect(owner, &rc);
    s_about = CreateWindowExW(DlgExStyle(), L"ClockAbout",
                              T(L"About DeskTick"), DLG_STYLE,
                              rc.right + 12, rc.top, 100, 100,
                              nullptr, nullptr, inst, nullptr);
    if (!s_about) return;
    ThemeCaption(s_about);
    AboutBuild();
    ShowWindow(s_about, SW_SHOW);
    SetForegroundWindow(s_about);
}

// Close the About box if it is open — ownerless, so shutdown must say so.
void AboutClose()
{
    if (s_about) DestroyWindow(s_about);
}

// Light/dark switched. Driven from clock.cpp rather than our own
// WM_SETTINGCHANGE so the uxtheme colour cache is refreshed first — see there.
void AboutThemeChanged()
{
    if (s_about) {
        ThemeCaption(s_about);
        AboutBuild();
    }
}

// Tab / Escape / mnemonics — a plain window gets none of that for free.
bool AboutIsDialogMessage(MSG* m)
{
    return s_about && IsDialogMessageW(s_about, m);
}
