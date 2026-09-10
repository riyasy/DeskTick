// customize.cpp — the configure dialog.
//
// Built in code, not from a resource template, because its contents are not
// knowable at build time: it reads whatever FaceOpt table the *active* face
// exposes and lays out one row per option. Switching face re-runs that layout
// in place (CustomizeRetarget), which is the whole point.
//
// Modeless with no OK/Cancel: every control writes its option through
// FaceOpt::value the moment it changes, then calls RefreshFace() so the clock
// repaints under the dialog. Nothing to apply, nothing to revert.
//
// It is a plain window rather than a real dialog, so IsDialogMessageW has to
// be pumped by the engine's loop (CustomizeIsDialogMessage) or Tab and Escape
// would do nothing.
//
// The options themselves — their defaults, their clamping and the INI — are in
// settings.cpp; the theming and the message font are in dlgchrome.cpp. What is
// left here is only what touches a control.

#include <windows.h>
#include <commdlg.h>
#include <strsafe.h>
#include "faces.h"
#include "app.h"

#pragma comment(lib, "comdlg32")

// Layout in 96-DPI units; S() scales to the dialog's monitor. LABEL_W, CTL_W
// and RESET_W are minimums rather than the whole answer: they are what English
// needs, and BuildControls widens each to whatever the loaded language actually
// measures. The maximums stop one long string from growing the dialog wider
// than a monitor — ClampToMonitor can only move a window, not shrink it.
static const int MARGIN = 12, ROW = 28, LABEL_W = 132, CTL_W = 148, RESET_W = 120;
static const int LABEL_MAX = 280, CTL_MAX = 260;
// Chrome a label sits next to and must not be measured into: a checkbox's box
// and gap, a combo's drop arrow and inner padding, a button's side padding.
static const int CHECK_W = 22, COMBO_W = 32, PAD = 12;
static const int ID_RESET = 900;
static const int ID_FIRST = 1000;       // control id = ID_FIRST + option index
// Runs only while the colour picker is up — see OnCommand.
static const UINT_PTR TIMER_TICK = 1;

static HWND              s_dlg;
static const IClockFace* s_face;
static HFONT             s_font;
static COLORREF          s_custom[16];  // ChooseColorW's custom slots

// The option table the dialog is currently showing; 0 rows before a face has
// been handed to it.
static int Opts(const FaceOpt** out)
{
    return s_face ? s_face->GetOptions(out) : 0;
}

// Push the option values back into the existing controls. Reset uses this
// rather than BuildControls so it never destroys the button whose WM_COMMAND
// is still on the stack.
static void SyncControls()
{
    const FaceOpt* o;
    int n = Opts(&o);
    for (int i = 0; i < n; i++) {
        HWND c = GetDlgItem(s_dlg, ID_FIRST + i);
        if (!c) continue;
        switch (o[i].kind) {
        case OPT_BOOL:
            SendMessageW(c, BM_SETCHECK, *o[i].value ? BST_CHECKED : BST_UNCHECKED, 0);
            break;
        case OPT_CHOICE:
            SendMessageW(c, CB_SETCURSEL, (WPARAM)*o[i].value, 0);
            break;
        case OPT_COLOR:
            InvalidateRect(c, nullptr, TRUE);       // swatch repaints from the value
            break;
        }
    }
}

// Active face only — it is the one the dialog is showing. The values are put
// back by settings.cpp, which owns the defaults; this end only has to make the
// controls agree with them again and apply the result.
static void ResetFace()
{
    ResetFaceDefaults(s_face);
    SyncControls();
    RefreshFace();
    ConfigSave();
}

// Rebuild every control from the active face's table and resize to fit.
// One string's width, back in 96-DPI units. The font handed in is already at
// the dialog's DPI, so the extent comes back scaled and is divided out again —
// which lets the caller keep working in the same units as the constants above
// and leave the S() macro to do the scaling exactly once.
static int TextW(HDC dc, const WCHAR* s, UINT dpi)
{
    SIZE sz;
    if (!s || !GetTextExtentPoint32W(dc, s, lstrlenW(s), &sz)) return 0;
    return MulDiv((int)sz.cx, 96, (int)dpi);
}

// Called on open and on every face change — this is what makes the dialog
// "dynamic": there is no per-face layout anywhere, only this loop.
static void BuildControls()
{
    HWND c;
    while ((c = GetWindow(s_dlg, GW_CHILD)) != nullptr) DestroyWindow(c);
    if (s_font) { DeleteObject(s_font); s_font = nullptr; }

    UINT dpi = GetDpiForWindow(s_dlg);
    #define S(x) MulDiv(x, (int)dpi, 96)

    LOGFONTW lf;
    DlgMessageLogFont(dpi, &lf);
    s_font = CreateFontIndirectW(&lf);

    const FaceOpt* o;
    int n = Opts(&o);
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(s_dlg, GWLP_HINSTANCE);
    int y = MARGIN;

    // Size the two columns to the translated strings rather than to English.
    // "Show seconds" is 12 characters; its German is 22, its Hungarian longer
    // still, and a clipped option label is an unusable one. Measuring also
    // means the dialog stays right for any string added later, in any language,
    // with nothing to re-tune.
    int labW = LABEL_W, ctlW = CTL_W, resetW = RESET_W;
    {
        HDC dc = GetDC(s_dlg);
        HGDIOBJ prev = SelectObject(dc, s_font);
        int checkW = 0;                 // widest checkbox: spans both columns
        for (int i = 0; i < n; i++) {
            int w = TextW(dc, T(o[i].label), dpi);
            if (o[i].kind == OPT_BOOL) {
                if (w + CHECK_W > checkW) checkW = w + CHECK_W;
            } else if (w + PAD > labW) {
                labW = w + PAD;
            }
            if (o[i].kind == OPT_CHOICE)
                for (int k = 0; o[i].choices[k]; k++) {
                    int cw = TextW(dc, T(o[i].choices[k]), dpi) + COMBO_W;
                    if (cw > ctlW) ctlW = cw;
                }
        }
        int rw = TextW(dc, T(L"Reset this face"), dpi) + PAD * 2;
        if (rw > resetW) resetW = rw;
        SelectObject(dc, prev);
        ReleaseDC(s_dlg, dc);
        if (labW > LABEL_MAX) labW = LABEL_MAX;
        if (ctlW > CTL_MAX)   ctlW = CTL_MAX;
        // A checkbox carries its own label across both columns, so it widens
        // the dialog rather than the label column — and the Reset button has to
        // fit inside that total too.
        if (checkW > labW + ctlW) ctlW = checkW - labW;
        if (resetW > labW + ctlW) ctlW = resetW - labW;
    }

    for (int i = 0; i < n; i++) {
        HWND ctl = nullptr;
        if (o[i].kind == OPT_BOOL) {
            // Checkbox carries its own label, so it spans both columns.
            ctl = CreateWindowExW(0, L"BUTTON", T(o[i].label),
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                  S(MARGIN), S(y), S(labW + ctlW), S(20),
                                  s_dlg, (HMENU)(INT_PTR)(ID_FIRST + i), inst, nullptr);
            ThemeControl(ctl, L"DarkMode_Explorer");
            SendMessageW(ctl, BM_SETCHECK, *o[i].value ? BST_CHECKED : BST_UNCHECKED, 0);
        } else {
            HWND lbl = CreateWindowExW(0, L"STATIC", T(o[i].label),
                                       WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       S(MARGIN), S(y + 4), S(labW), S(18),
                                       s_dlg, nullptr, inst, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)s_font, TRUE);
            int x = S(MARGIN + labW);
            if (o[i].kind == OPT_CHOICE) {
                // The height argument sizes the *dropped* list, not the box.
                ctl = CreateWindowExW(0, L"COMBOBOX", nullptr,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                      CBS_DROPDOWNLIST,
                                      x, S(y), S(ctlW), S(140),
                                      s_dlg, (HMENU)(INT_PTR)(ID_FIRST + i), inst, nullptr);
                ThemeControl(ctl, L"DarkMode_CFD");
                // Only the selected *index* is ever persisted (settings.cpp),
                // so translating the visible text changes nothing on disk.
                for (int k = 0; o[i].choices[k]; k++)
                    SendMessageW(ctl, CB_ADDSTRING, 0, (LPARAM)T(o[i].choices[k]));
                SendMessageW(ctl, CB_SETCURSEL, (WPARAM)*o[i].value, 0);
            } else {
                // Owner-draw so the button *is* the swatch (see WM_DRAWITEM).
                ctl = CreateWindowExW(0, L"BUTTON", nullptr,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      x, S(y), S(ctlW), S(21),
                                      s_dlg, (HMENU)(INT_PTR)(ID_FIRST + i), inst, nullptr);
            }
        }
        if (ctl) SendMessageW(ctl, WM_SETFONT, (WPARAM)s_font, TRUE);
        y += ROW;
    }

    // Section() names the face here: the image face's GetName() is a full
    // image path, which reads wrong in a title bar and is twice the length of
    // this buffer — and wsprintfW would not stop at the end of it.
    // The app's name goes in the title because this window is where the app is
    // named: it takes a taskbar button and an Alt+Tab slot (the clock itself is
    // a tool window and takes neither), so without it there is nothing on screen
    // that says what DeskTick is.
    //
    // The pieces are translated, the punctuation between them is ours. A
    // translatable string holding a "%s" would be a format string coming out of
    // an editable file: one that gained a second specifier would send
    // wsprintfW after an argument nobody passed. Assembling from parts costs a
    // little word order in a title bar and removes that entirely — and
    // StringCchPrintfW truncates where wsprintfW would run off the end, which
    // now matters because a translated face name has no length we control.
    WCHAR title[128];
    if (n) StringCchPrintfW(title, _countof(title), L"%s: %s - DeskTick",
                            T(L"Customize"), T(Section(s_face)));
    else   StringCchCopyW(title, _countof(title), T(L"Customize - DeskTick"));
    if (!n) {
        HWND lbl = CreateWindowExW(0, L"STATIC", T(L"This face has nothing to customize."),
                                   WS_CHILD | WS_VISIBLE | SS_LEFT,
                                   S(MARGIN), S(MARGIN), S(labW + ctlW), S(18),
                                   s_dlg, nullptr, inst, nullptr);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)s_font, TRUE);
        y += ROW;
    }
    SetWindowTextW(s_dlg, title);

    // Reset, bottom-right. Only worth showing when there is something to reset.
    if (n) {
        y += 6;
        // The label says "this face" because that is the scope: the dialog
        // shows one face at a time and the reset follows it.
        HWND rst = CreateWindowExW(0, L"BUTTON", T(L"Reset this face"),
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   S(MARGIN + labW + ctlW - resetW), S(y),
                                   S(resetW), S(24),
                                   s_dlg, (HMENU)(INT_PTR)ID_RESET, inst, nullptr);
        ThemeControl(rst, L"DarkMode_Explorer");
        SendMessageW(rst, WM_SETFONT, (WPARAM)s_font, TRUE);
        y += ROW;
    }

    // Grow the frame around the client area we just filled.
    RECT rc = { 0, 0, S(MARGIN * 2 + labW + ctlW), S(y - ROW + 24 + MARGIN) };
    AdjustWindowRectExForDpi(&rc, DLG_STYLE, FALSE, DlgExStyle(), dpi);
    SetWindowPos(s_dlg, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    // We open beside the clock, so a clock near the right or bottom edge puts
    // us past it — and a face with more options grows us further out.
    ClampToMonitor(s_dlg);
    InvalidateRect(s_dlg, nullptr, TRUE);
    #undef S
}

// One control changed: write its option through FaceOpt::value, then apply
// and persist. There is no OK button, so this is the whole commit path.
static void OnCommand(WPARAM wp)
{
    const FaceOpt* o;
    int n = Opts(&o);
    int i = LOWORD(wp) - ID_FIRST;
    if (i < 0 || i >= n) return;
    HWND ctl = GetDlgItem(s_dlg, LOWORD(wp));
    UINT code = HIWORD(wp);

    switch (o[i].kind) {
    case OPT_BOOL:
        if (code != BN_CLICKED) return;
        *o[i].value = SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED;
        break;
    case OPT_CHOICE: {
        if (code != CBN_SELCHANGE) return;
        LRESULT sel = SendMessageW(ctl, CB_GETCURSEL, 0, 0);
        if (sel == CB_ERR) return;
        *o[i].value = (int)sel;
        break;
    }
    case OPT_COLOR: {
        if (code != BN_CLICKED) return;
        CHOOSECOLORW cc = { sizeof(cc) };
        cc.hwndOwner    = s_dlg;
        cc.rgbResult    = (COLORREF)*o[i].value;
        cc.lpCustColors = s_custom;
        cc.Flags        = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
        // ChooseColorW is modal and pumps its own message loop. Both of this
        // app's own windows are modeless precisely to avoid that (see
        // about.cpp), because the engine's loop is the only thing waiting on
        // the clock's tick timer — and a common dialog is the one modal loop
        // there is no way around. Left alone, the clock freezes on whatever
        // minute the picker was opened at, which is the single bug a clock is
        // not allowed to have.
        //
        // A UI timer on this window *is* dispatched by that nested loop, so it
        // is what keeps the hands moving; it lives exactly as long as the modal
        // call does, the same shape as the resize hook in clock.cpp, so the
        // zero-idle design is untouched everywhere else. It is a poll, and the
        // ceiling it buys is 200 ms of lateness on the second hand while a
        // colour is being picked. Timekeeping itself is not affected: the
        // waitable timer is still the clock, and re-aims from the wall clock
        // the moment the engine's loop resumes.
        SetTimer(s_dlg, TIMER_TICK, 200, nullptr);
        BOOL picked = ChooseColorW(&cc);
        KillTimer(s_dlg, TIMER_TICK);
        if (!picked) return;                            // cancelled
        *o[i].value = (int)cc.rgbResult;
        InvalidateRect(ctl, nullptr, TRUE);             // repaint the swatch
        break;
    }
    }
    ClampOpt(o[i]);
    RefreshFace();
    ConfigSave();
}

// The configure window's handler: control changes, the owner-drawn colour
// swatches, and the theme/DPI painting a plain window has to do for itself.
static LRESULT CALLBACK CustomizeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }  // Escape
        if (LOWORD(wp) == ID_RESET) {
            if (HIWORD(wp) == BN_CLICKED) ResetFace();
            return 0;
        }
        OnCommand(wp);
        return 0;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        const FaceOpt* o;
        int n = Opts(&o);
        int i = (int)d->CtlID - ID_FIRST;
        if (i < 0 || i >= n || o[i].kind != OPT_COLOR) break;
        HBRUSH fill = CreateSolidBrush((COLORREF)*o[i].value);
        FillRect(d->hDC, &d->rcItem, fill);
        DeleteObject(fill);
        FrameRect(d->hDC, &d->rcItem, (HBRUSH)GetStockObject(GRAY_BRUSH));
        if (d->itemState & ODS_FOCUS) DrawFocusRect(d->hDC, &d->rcItem);
        return TRUE;
    }
    case WM_TIMER:
        // Only ever armed inside ChooseColorW's modal loop (OnCommand): the
        // engine's loop is blocked for the duration, so this is what repaints
        // the clock. Nothing else in this app uses a WM_TIMER.
        if (wp == TIMER_TICK) ClockRepaint();
        return 0;
    case WM_ERASEBKGND: {
        // The class brush can't do this: it is fixed at registration and the
        // theme can change while we're open.
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, ThemeBrush());
        return 1;
    }
    case WM_CTLCOLORSTATIC:                         // labels, and checkbox text
    case WM_CTLCOLORBTN:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, IsDarkMode() ? DARK_FG
                                           : GetSysColor(COLOR_WINDOWTEXT));  // honour high contrast
        return (LRESULT)ThemeBrush();
    case WM_CTLCOLORLISTBOX:                        // the combo's dropped list
        if (!IsDarkMode()) break;
        SetBkMode((HDC)wp, OPAQUE);
        SetBkColor((HDC)wp, DARK_BG);
        SetTextColor((HDC)wp, DARK_FG);
        return (LRESULT)ThemeBrush();
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        BuildControls();                                // re-scales everything
        return 0;
    }
    case WM_DESTROY:
        if (s_font) { DeleteObject(s_font); s_font = nullptr; }
        s_dlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Open, or raise if already open. `owner` is only used to place the dialog
// beside the clock — the window is deliberately ownerless, because the clock
// is owned by the desktop and an owned dialog would inherit that z-order.
void CustomizeShow(HWND owner, const IClockFace* face)
{
    s_face = face;
    if (s_dlg) {
        BuildControls();
        SetForegroundWindow(s_dlg);
        return;
    }

    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = CustomizeProc;
        wc.hInstance     = inst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;                 // WM_ERASEBKGND: theme-dependent
        wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(ICON_APP));
        wc.lpszClassName = L"ClockConfig";
        RegisterClassW(&wc);
        registered = true;
    }

    RECT rc; GetWindowRect(owner, &rc);
    s_dlg = CreateWindowExW(DlgExStyle(), L"ClockConfig",
                            T(L"Customize - DeskTick"), DLG_STYLE,
                            rc.right + 12, rc.top, 100, 100,
                            nullptr, nullptr, inst, nullptr);
    if (!s_dlg) return;
    ThemeCaption(s_dlg);
    BuildControls();            // ends in ClampToMonitor, which is what keeps a
                                // dialog opened beside a right-edge clock on screen
    ShowWindow(s_dlg, SW_SHOW);
    SetForegroundWindow(s_dlg);
}

// Face changed under an open dialog: swap in the new table. No-op if closed.
//
// The same-face early-out is load-bearing, not an optimisation: RefreshFace()
// calls this on every option change too, and rebuilding would destroy the very
// control whose WM_COMMAND is still on the stack.
void CustomizeRetarget(const IClockFace* face)
{
    if (face == s_face) return;
    s_face = face;
    if (s_dlg) BuildControls();
}

// Close the dialog if it is open. Called on shutdown: the window is
// ownerless, so it does not go when the clock does.
void CustomizeClose()
{
    if (s_dlg) DestroyWindow(s_dlg);
}

// Light/dark switched. Driven from clock.cpp rather than our own
// WM_SETTINGCHANGE so the uxtheme colour cache is refreshed first — see there.
// A rebuild is safe here: unlike an option change, nothing of ours is on the
// stack, so no control is destroyed out from under its own WM_COMMAND.
void CustomizeThemeChanged()
{
    if (s_dlg) {
        ThemeCaption(s_dlg);
        BuildControls();
    }
}

// Tab / Escape / mnemonics — a plain window gets none of that for free.
bool CustomizeIsDialogMessage(MSG* m)
{
    return s_dlg && IsDialogMessageW(s_dlg, m);
}
