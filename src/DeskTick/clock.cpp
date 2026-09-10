// clock.cpp — transparent desktop clock widget.
// Raw Win32 + software Direct2D. No D3D device, no DComp, no WIC, no STL.
//
// Right-click for the menu: show seconds, customize, resize, face, always on
// top, start with Windows, tray icon, exit. Double-click cycles to the next
// built-in face.
// Per-face options, always-on-top and the tray icon live in DeskTick.ini beside
// the exe (settings.cpp); "start with Windows" lives in the registry, because
// Windows is what reads it. The second hand and resize mode stay volatile.

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <d2d1.h>
#include <string.h>
#include "faces.h"
#include "app.h"

#pragma comment(lib, "d2d1")
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "ole32")
#pragma comment(lib, "shell32")

// DIAL / CX / RADIUS live in faces.h (shared with the faces).
static const float GRIP   = 28.0f;           // corner hit-region size (DIPs)
static const float GRIP_R = 13.0f;           // the round grip button's radius

#define WM_ENDRESIZE (WM_APP + 1)            // posted by the outside-click hook
#define WM_TRAYICON  (WM_APP + 2)            // the notification icon's callback

// ---- menu command ids ----
enum { CMD_SECONDS = 1, CMD_RESIZE, CMD_CONFIG, CMD_TOPMOST, CMD_STARTUP,
       CMD_TRAY, CMD_ABOUT, CMD_CLOSE,
       CMD_FACE0 = 100 };                       // FACE0 = built-in face

// ---- globals (single window, single thread) ----
static HWND                 g_hwnd;
static HANDLE               g_timer;
static ID2D1Factory*        g_factory;
static ID2D1StrokeStyle*    g_roundStroke;  // round caps/joins (grip arrow)
static ID2D1StrokeStyle*    g_dashStroke;   // dashed (resize-mode marquee)
static ID2D1DCRenderTarget* g_rt;        // software target bound to the DIB's DC
static ID2D1Bitmap*         g_dial;      // cached dial (rim/ticks/numerals)
static ID2D1SolidColorBrush* g_brush;
static HDC                  g_memDC;
static HBITMAP              g_dib;
static int                  g_px;        // DIB / render target size in pixels
static int                  g_winPx;     // window size in pixels (== g_px except mid-drag)
static UINT                 g_dpi = 96;  // monitor DPI (for WM_DPICHANGED ratio)
static bool                 g_seconds;   // second hand on/off (persisted)
bool ShowSeconds() { return g_seconds; }   // faces.h: read by DrawDial, see there
static bool                 g_resize;    // resize mode: marquee + grip shown
static bool                 g_topmost;   // always on top (persisted)
static bool                 g_tray;      // notification icon shown (persisted)
static HHOOK                g_outsideHook;  // installed only while g_resize
static bool                 g_menuUp;    // our popup menu is tracking
static bool                 g_drag;      // grip drag in progress
static POINT                g_anchor;    // fixed top-left corner during drag
static int                  g_face;      // 0..NUM_BUILTIN-1 = built-in face, NUM_BUILTIN.. = asset image (assets.cpp)

// Face whose hands DrawFrame draws — set by BuildDial to what was actually
// drawn (an image load failure falls back to the default face, hands included).
// Starts null, never read before BuildDial runs: DrawFrame bails while g_dial
// is null, and g_dial only becomes non-null after BuildDial assigned this.
// (Can't seed from g_builtinFaces here — cross-TU dynamic-init order.)
static const IClockFace*    g_activeFace;

// ------------------------------------------------------------------
// Dial: drawn once per size into an offscreen D2D bitmap.
// Rationale: rim + 60 ticks + 12 DirectWrite numerals is by far the most
// expensive part of the scene. Caching it makes each tick a single
// DrawBitmap plus 2-3 line strokes — cheap enough that CPU cost per
// minute (or second) is unmeasurable, and nothing per-frame touches
// DirectWrite at all.
// ------------------------------------------------------------------
static ID2D1Bitmap* BuildDial()
{
    ID2D1BitmapRenderTarget* brt = nullptr;
    if (FAILED(g_rt->CreateCompatibleRenderTarget(D2D1::SizeF(DIAL, DIAL), &brt)))
        return nullptr;

    ID2D1SolidColorBrush* b = nullptr;
    if (FAILED(brt->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.92f, 0.95f), &b))) {
        brt->Release();
        return nullptr;
    }
    // Grayscale AA: ClearType needs an opaque background we don't have.
    brt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    brt->BeginDraw();
    brt->Clear(D2D1::ColorF(0, 0.0f));                       // fully transparent

    // Selected face draws the dial; on failure (image not loadable) fall
    // back to the default face. g_activeFace records what was actually
    // drawn so DrawFrame draws matching hands.
    const IClockFace* f = (g_face < NUM_BUILTIN) ? g_builtinFaces[g_face]
                                                 : g_faceImage;
    if (!f->DrawDial(brt, b)) {
        f = g_builtinFaces[0];
        f->DrawDial(brt, b);
    }
    g_activeFace = f;

    brt->EndDraw();
    b->Release();

    ID2D1Bitmap* bmp = nullptr;
    brt->GetBitmap(&bmp);  // bitmap outlives the compatible target
    brt->Release();
    return bmp;
}

// ------------------------------------------------------------------
// (Re)create everything size-dependent for a window of `px` physical
// pixels: DIB + memory DC + DC render target + cached dial. The D2D DPI
// is derived from px so the fixed 240-DIP scene fills the window exactly
// — this is what makes resize scale everything proportionally.
// Called at startup, on WM_DPICHANGED, and during a resize drag.
// ------------------------------------------------------------------
static bool CreateResources(int px)
{
    SafeRelease(g_dial);
    SafeRelease(g_brush);
    SafeRelease(g_rt);
    if (g_memDC) { DeleteDC(g_memDC); g_memDC = nullptr; }
    if (g_dib)   { DeleteObject(g_dib); g_dib = nullptr; }

    g_px = g_winPx = px;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = g_px;
    bi.bmiHeader.biHeight      = -g_px;          // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;             // BGRA premultiplied
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    g_dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!g_dib) return false;
    g_memDC = CreateCompatibleDC(nullptr);
    SelectObject(g_memDC, g_dib);

    // TYPE_SOFTWARE guarantees no hardware D3D device is ever created —
    // rendering stays on the CPU into our DIB.
    float dpi = px * 96.0f / DIAL;               // logical 240 DIPs -> px pixels
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpi, dpi);
    if (FAILED(g_factory->CreateDCRenderTarget(&props, &g_rt))) return false;

    RECT rc = { 0, 0, g_px, g_px };
    if (FAILED(g_rt->BindDC(g_memDC, &rc))) return false;

    g_rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &g_brush);
    g_dial = BuildDial();
    return g_dial && g_brush;
}

// ------------------------------------------------------------------
// One frame: blit cached dial, stroke the hands, resize-mode overlays,
// push to the screen.
// ------------------------------------------------------------------
static void DrawFrame()
{
    if (!g_rt || !g_dial || !g_brush) return;   // survive a failed CreateResources
    SYSTEMTIME st;
    GetLocalTime(&st);

    // Mid-drag the window (g_winPx) is smaller than the drag buffer (g_px):
    // draw the whole 240-DIP scene scaled into the top-left g_winPx region.
    // Steady state f == 1. This is what makes a resize drag allocation-free.
    float f = (float)g_winPx / g_px;
    D2D1_MATRIX_3X2_F base = D2D1::Matrix3x2F::Scale(f, f);

    g_rt->BeginDraw();
    g_rt->SetTransform(base);
    g_rt->Clear(D2D1::ColorF(0, 0.0f));
    g_rt->DrawBitmap(g_dial, D2D1::RectF(0, 0, DIAL, DIAL));

    g_activeFace->DrawHands(g_rt, g_brush, st, g_seconds, base);

    if (g_resize) {
        // Dotted marquee on the window's square bounds — the dial is a circle,
        // so without it there is nothing showing what the drag actually sizes.
        // Inset half a stroke width so neither edge is clipped.
        g_brush->SetColor(D2D1::ColorF(0.2f, 0.5f, 0.9f, 0.9f));
        g_rt->DrawRectangle(D2D1::RectF(0.75f, 0.75f, DIAL - 0.75f, DIAL - 0.75f),
                            g_brush, 1.5f, g_dashStroke);

        // Grip, bottom-right (FlipClock's button): light disc, hairline rim,
        // NWSE double arrow — the same axis as the IDC_SIZENWSE cursor over it.
        // Overlays need alpha > 0: fully transparent ULW pixels are click-through.
        D2D1_POINT_2F gc = D2D1::Point2F(DIAL - GRIP / 2, DIAL - GRIP / 2);
        D2D1_ELLIPSE  disc = D2D1::Ellipse(gc, GRIP_R, GRIP_R);
        g_brush->SetColor(D2D1::ColorF(0.96f, 0.97f, 0.99f, 0.97f));
        g_rt->FillEllipse(disc, g_brush);
        g_brush->SetColor(D2D1::ColorF(0, 0.18f));      // black hairline rim
        g_rt->DrawEllipse(disc, g_brush, 1.0f);
        // One shaft plus a two-stroke head at each end. Round caps so the
        // head strokes merge into the shaft without a notch.
        g_brush->SetColor(D2D1::ColorF(0.27f, 0.35f, 0.43f));
        constexpr float a = 4.6f, hd = 3.4f;
        constexpr float seg[5][4] = {
            { -a, -a,       a,       a },
            { -a, -a, -a + hd,      -a },
            { -a, -a,      -a, -a + hd },
            {  a,  a,  a - hd,       a },
            {  a,  a,       a,  a - hd },
        };
        for (int i = 0; i < 5; i++)
            g_rt->DrawLine(D2D1::Point2F(gc.x + seg[i][0], gc.y + seg[i][1]),
                           D2D1::Point2F(gc.x + seg[i][2], gc.y + seg[i][3]),
                           g_brush, 1.7f, g_roundStroke);
    }
    g_rt->EndDraw();

    // Push the premultiplied DIB to the screen. ULW blits just the top-left
    // g_winPx square, so a drag buffer larger than the window is fine.
    SIZE sz = { g_winPx, g_winPx };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_hwnd, nullptr, nullptr, &sz,
                        g_memDC, &src, 0, &bf, ULW_ALPHA);
}

// ------------------------------------------------------------------
// Apply a face or option change: the dial is the only thing cached across
// frames, so rebuilding it is all any of them needs. Three call sites —
// the menu, double-click, and the configure dialog.
// ------------------------------------------------------------------
void RefreshFace()          // not static: customize.cpp calls it
{
    SafeRelease(g_dial);
    g_dial = BuildDial();
    CustomizeRetarget(g_activeFace);    // BuildDial may have fallen back
    DrawFrame();
}

// Repaint at the current wall time, rebuilding nothing. Exists for a nested
// modal loop: ChooseColorW in the configure dialog runs one, and while it does,
// the loop at the bottom of this file — the only thing that waits on the tick
// timer — is not running. customize.cpp drives this off a UI timer for exactly
// as long as the picker is up, so the clock keeps time through it.
//
// The waitable timer is one-shot and stays signalled once it fires, so nothing
// here re-arms: the loop resumes, sees it signalled, draws and re-aims itself.
void ClockRepaint()         // not static: customize.cpp calls it
{
    DrawFrame();
}

// Persist face + geometry. Called after the three things that change them:
// a face switch, the end of a resize drag, the end of a move drag.
static void SaveWindow()
{
    RECT rc;
    if (g_hwnd && GetWindowRect(g_hwnd, &rc))
        ConfigSaveWindow(g_face, g_winPx, rc.left, rc.top);
}

// ------------------------------------------------------------------
// Re-arm math. The timer is always aimed at the next real wall-clock
// boundary, never at "now + period":
//
//   minute mode:  msLeft = 60000 - (sec*1000 + ms)   until next :00
//   second mode:  msLeft =  1000 - ms                 until next second
//
// Because we recompute from GetLocalTime after every fire, any latency
// in servicing the previous tick (or a system clock adjustment) is
// absorbed — the hands can never drift relative to the taskbar clock.
// Negative due time = relative, in 100 ns units, hence * -10000.
//
// MARGIN is why the aim is *past* the boundary and not at it. The timer
// is high resolution; GetLocalTime is not. The SYSTEMTIME it hands back
// only advances on the clock interrupt — 15.6 ms apart on an idle
// machine, ~1 ms while any other process holds the resolution down, and
// promised to be neither. Wake exactly on the boundary and the read can
// still be on the old side of it: DrawFrame paints the second we just
// left, and re-arming from that same stale read gives a few ms, which
// then has to be bumped by a whole period so we don't spin — landing on
// the *next* boundary. That is the visible bug: one second painted
// twice, the one after it never painted at all. Waking a margin late
// puts the read unambiguously past the boundary in either regime, and
// leaves no too-small remainder to special-case. 20 ms of lateness in
// the hands is invisible; a skipped second is not.
//
// The tolerance (last argument) must stay 0. A coalescable timer and a
// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION one are mutually exclusive: pass
// both and SetWaitableTimerEx returns FALSE with ERROR_INVALID_PARAMETER
// and arms nothing at all. Minute mode used to pass 250 ms here to let the
// kernel coalesce the wakeup, which meant it never armed the timer and the
// clock only redrew when some unrelated message happened to repaint it.
// One wake a minute is not what costs a battery — this is not worth
// buying back with a second, non-high-resolution timer.
// ------------------------------------------------------------------
static void ArmTimer()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    const DWORD MARGIN = 20;                // > the 15.6 ms clock-interrupt period
    DWORD msLeft = (g_seconds ? 1000 - st.wMilliseconds
                              : 60000 - (st.wSecond * 1000 + st.wMilliseconds)) + MARGIN;
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)msLeft * 10000;
    SetWaitableTimerEx(g_timer, &due, 0, nullptr, nullptr, nullptr, 0);
}

// Grip hit region in physical pixels (GRIP DIPs scaled to window size). It is
// the disc's bounding square, not the disc — the square's corners are fully
// transparent, so ULW makes them click-through before the hit test is asked.
static int GripPx() { return MulDiv(g_winPx, (int)GRIP, (int)DIAL); }
// True for a client-relative point inside that square.
static bool InGrip(int x, int y) { int s = GripPx(); return x > g_winPx - s && y > g_winPx - s; }

// ------------------------------------------------------------------
// Resize mode is transient: turn it on, size the clock, click away. But this
// window is WS_EX_NOACTIVATE, so there is no focus to lose — no WM_KILLFOCUS,
// no WM_ACTIVATEAPP — and a click landing anywhere else never reaches us. A
// low-level mouse hook is the only thing that sees that click, which is what
// lets the mode end without an accept button on the clock.
//
// Installed ONLY while resize mode is on: the ordinary state pays nothing.
// The proc stays trivial for the reason it has to — Windows silently drops a
// low-level hook whose proc overruns LowLevelHooksTimeout — and only posts,
// leaving the state change to the normal message path.
// ------------------------------------------------------------------
static LRESULT CALLBACK OutsideClickProc(int code, WPARAM wp, LPARAM lp)
{
    // g_menuUp: picking an item in our own menu lands outside the window, and
    // would otherwise cancel resize mode as a side effect of using the menu.
    if (code == HC_ACTION && !g_menuUp &&
        (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN)) {
        POINT pt = ((MSLLHOOKSTRUCT*)lp)->pt;
        RECT rc; GetWindowRect(g_hwnd, &rc);
        if (!PtInRect(&rc, pt))
            PostMessageW(g_hwnd, WM_ENDRESIZE, 0, 0);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

// Enter or leave resize mode: the marquee and grip appear, and the
// outside-click hook lives exactly as long as the mode does.
static void SetResizeMode(bool on)
{
    if (on == g_resize) return;
    g_resize = on;
    if (on) {
        g_outsideHook = SetWindowsHookExW(WH_MOUSE_LL, OutsideClickProc,
                                          GetModuleHandleW(nullptr), 0);
    } else if (g_outsideHook) {
        UnhookWindowsHookEx(g_outsideHook);
        g_outsideHook = nullptr;
    }
    DrawFrame();
}

// ------------------------------------------------------------------
// Own the desktop's DefView, which is what exempts us from Win+D.
//
// Not a one-time startup step: an Explorer restart destroys the whole
// Progman/WorkerW/DefView tree and builds a new one. Measured, the clock
// itself survives that — window and process both — but it is left owned by
// a handle that is no longer a window, so the exemption is quietly gone
// with nothing on screen to say so.
//
// Explorer broadcasts "TaskbarCreated" when it comes back, which is how
// tray icons re-add themselves; we get it because the window is WS_POPUP
// and owned rather than WS_CHILD, so it is still top-level for a broadcast
// (which is also why FindWindowW can't find it — that walks the desktop's
// own children, and ours now hang off DefView).
//
// The desktop listview is not guaranteed to exist yet at that instant, so
// a failed grab sets g_needOwner and the tick retries it — no new timer,
// and the loop already wakes at least once a minute.
// ------------------------------------------------------------------
static bool g_needOwner;                    // grab failed; retry on the next tick

static void PinToDesktop()
{
    HWND dv = FindDefView();
    if (dv) SetWindowLongPtrW(g_hwnd, GWLP_HWNDPARENT, (LONG_PTR)dv);
    g_needOwner = !dv;                      // no shell running: try again later
}

// ------------------------------------------------------------------
// Always on top. The desktop ownership above is kept either way — the two are
// not in conflict: SetWindowPos drags a window's *owned* windows topmost with
// it and explicitly leaves its owner alone, so the desktop stays at the
// bottom where it belongs. Off is HWND_NOTOPMOST rather than HWND_BOTTOM,
// which would shove the clock under everything instead of merely out of the
// topmost band.
// ------------------------------------------------------------------
static void ApplyTopmost()
{
    SetWindowPos(g_hwnd, g_topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// ------------------------------------------------------------------
// The notification icon. Off by default: the clock is its own UI, and this
// exists for when it isn't reachable — parked under another window, or
// dragged somewhere unhelpful. It carries the same menu, so it is also the
// way back to Exit when the clock itself can't be right-clicked.
//
// Re-added on "TaskbarCreated" for the same reason PinToDesktop is re-run
// there: an Explorer restart takes the notification area with it.
// ------------------------------------------------------------------
static const UINT TRAY_UID = 1;

// uID rather than NIF_GUID: a GUID-identified icon is bound to the exe's path
// on disk, so moving or renaming the exe loses the icon's settings silently.
static NOTIFYICONDATAW TrayData()
{
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID  = TRAY_UID;
    return nid;
}

static void TrayDelete() { NOTIFYICONDATAW nid = TrayData(); Shell_NotifyIconW(NIM_DELETE, &nid); }

static void TraySync()
{
    if (!g_tray) { TrayDelete(); return; }
    NOTIFYICONDATAW nid = TrayData();
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    // LR_SHARED at exactly the small-icon metric: the system caches and owns
    // the handle, so there is nothing to DestroyIcon and nothing to leak on a
    // re-add. LoadIconW would hand back the 32 px frame for the shell to
    // squash, which is the blurry tray icon everyone recognises.
    nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(ICON_APP),
                                  IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    lstrcpyW(nid.szTip, L"DeskTick");       // fixed literal into a 128-char field
    Shell_NotifyIconW(NIM_ADD, &nid);
    // Version 3 callbacks: lParam is the mouse message, plainly. Version 4
    // reports the click as WM_CONTEXTMENU with packed screen coordinates,
    // which is a second code path to buy nothing for one menu.
    nid.uVersion = NOTIFYICON_VERSION;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

// The clock window's only message handler: hit-testing and cursors, the
// resize drag, the right-click menu, and the system changes (DPI, displays,
// theme, wall-clock time, the shell restarting) the widget has to survive.
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // Registered messages have no compile-time id, so this can't be a case
    // label. Registered once on first use; 0 means the OS refused, and no
    // real message id is 0, so the comparison is still safe.
    static const UINT s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == s_taskbarCreated && s_taskbarCreated) {
        PinToDesktop();                     // the shell restarted: re-own the new DefView
        TraySync();                         // and re-add the icon it took with it
        return 0;
    }

    switch (msg) {
    case WM_NCHITTEST: {
        if (g_resize) {
            RECT rc; GetWindowRect(hwnd, &rc);
            int x = GET_X_LPARAM(lp) - rc.left, y = GET_Y_LPARAM(lp) - rc.top;
            if (InGrip(x, y))
                return HTCLIENT;                // let the grip take clicks
        }
        return HTCAPTION;                       // everything else drags the window
    }
    case WM_SETCURSOR:
        // Diagonal-resize cursor over the grip (only the grip hit-tests as
        // HTCLIENT, and only in resize mode). Everything else falls through
        // to the class arrow cursor.
        if (g_resize && LOWORD(lp) == HTCLIENT) {
            POINT p; GetCursorPos(&p);
            RECT rc; GetWindowRect(hwnd, &rc);
            if (InGrip(p.x - rc.left, p.y - rc.top)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENWSE));
                return TRUE;
            }
        }
        break;                                  // DefWindowProc: class cursor
    case WM_ENTERSIZEMOVE:
        // Caption drag started (modal move loop). WM_SETCURSOR isn't sent
        // inside the loop, so set the move cursor here — it sticks until we
        // restore it on exit.
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return 0;
    case WM_EXITSIZEMOVE:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        SaveWindow();                           // the clock was dragged
        return 0;
    case WM_NCLBUTTONDBLCLK:
        // Everything outside the grip hit-tests as HTCAPTION, so a double-click
        // on the clock arrives here (needs CS_DBLCLKS on the class). Cycles the
        // built-in faces; an asset image face wraps back to the first built-in.
        // Built-ins only: the assets folder is listed lazily on right-click,
        // and a double-click must not pay for a directory scan.
        g_face = (g_face + 1 < NUM_BUILTIN) ? g_face + 1 : 0;
        RefreshFace();
        SaveWindow();
        return 0;
    case WM_LBUTTONDOWN: {                      // only reachable in resize mode
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (InGrip(x, y)) {                     // grip: start manual resize drag
            RECT rc; GetWindowRect(hwnd, &rc);
            g_anchor.x = rc.left;               // top-left corner stays fixed
            g_anchor.y = rc.top;
            g_drag = true;
            SetCapture(hwnd);
            // One-time realloc to the max drag size (dial rendered crisp at
            // the buffer size, downscaled while dragging). Every mousemove
            // after this is allocation-free, so a back-and-forth drag never
            // grows private bytes. The buffer is never smaller than the
            // current window (very high DPI can start above 1024) so the
            // scale factor stays <= 1.
            int keep = g_winPx;
            if (!CreateResources(max(1024, keep))) {
                g_drag = false;                 // couldn't allocate: abort drag
                ReleaseCapture();
                return 0;
            }
            g_winPx = keep;
            DrawFrame();
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_drag) {
            POINT p; GetCursorPos(&p);
            int w = p.x - g_anchor.x, h = p.y - g_anchor.y;
            int px = max(w, h);                 // keep the window square
            if (px < 96)   px = 96;
            if (px > g_px) px = g_px;           // can't outgrow the drag buffer
            if (px != g_winPx) {
                g_winPx = px;                   // no allocations: scene is just
                DrawFrame();                    // redrawn scaled into the buffer
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_drag) {
            g_drag = false;
            ReleaseCapture();
            CreateResources(g_winPx);           // shrink buffer to final size, crisp dial
            DrawFrame();
            SaveWindow();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (g_drag) {                           // capture stolen mid-drag:
            g_drag = false;                     // finalize at current size
            CreateResources(g_winPx);
            DrawFrame();
        }
        return 0;
    case WM_NCRBUTTONUP: {
        // Re-listed every time the menu opens: dropping a new image in shows
        // up on the next right-click, no restart needed (assets.cpp).
        int nAssets = AssetsRefresh();
        HMENU faces = CreatePopupMenu();
        for (int i = 0; i < NUM_BUILTIN; i++)
            // GetName() is the face's identity — the INI section it saves under
            // (settings.cpp) — so it stays English and only the label is
            // translated. Word clock is named in every language and still draws
            // its English letter grid; the grid is not translatable text.
            AppendMenuW(faces, MF_STRING | (g_face == i ? MF_CHECKED : 0),
                        CMD_FACE0 + i, T(g_builtinFaces[i]->GetName()));
        // Separator only if there is something after it: with no assets folder
        // the built-in list would otherwise end on a rule with nothing under it.
        if (nAssets) AppendMenuW(faces, MF_SEPARATOR, 0, nullptr);
        for (int i = 0; i < nAssets; i++) {
            // Display only — assets.cpp still holds the filename the image
            // face needs. A leading dot marks a dotfile rather than an
            // extension, so that name is menued whole; stripping it would
            // leave an empty label.
            WCHAR label[MAX_PATH];
            lstrcpyW(label, AssetsName(i));
            WCHAR* dot = wcsrchr(label, L'.');
            if (dot && dot != label) *dot = 0;
            AppendMenuW(faces, MF_STRING | (g_face == i + NUM_BUILTIN ? MF_CHECKED : 0),
                        CMD_FACE0 + NUM_BUILTIN + i, label);
        }
        // Grouped: what the clock shows, then what it lives on, then leaving.
        HMENU m = CreatePopupMenu();
        // "Show seconds" covers the six text faces too, which have no hand.
        AppendMenuW(m, MF_STRING | (g_seconds ? MF_CHECKED : 0), CMD_SECONDS, T(L"Show seconds"));
        // The ellipsis is a universal character name because this .cpp has no
        // BOM: MSVC reads the file in the system codepage, so a pasted U+2026
        // would reach the menu as mojibake. \u2026 is resolved by the compiler
        // whatever the encoding. It is also part of the T() key, so lang\*.ini
        // spells it as a real U+2026 \u2014 those files are UTF-16 and can.
        AppendMenuW(m, MF_STRING, CMD_CONFIG, T(L"Customize Face\u2026"));
        AppendMenuW(m, MF_STRING | (g_resize  ? MF_CHECKED : 0), CMD_RESIZE,  T(L"Resize"));
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)faces, T(L"Face"));
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        // Where the clock lives rather than what it shows: above other windows,
        // present at logon, reachable from the notification area. The first and
        // third are ours to remember (DeskTick.ini); the second is a registry value
        // Windows itself reads, so it is asked for fresh every time the menu
        // opens: the user may well have unticked it in Task Manager since.
        AppendMenuW(m, MF_STRING | (g_topmost ? MF_CHECKED : 0), CMD_TOPMOST, T(L"Always on top"));
        AppendMenuW(m, MF_STRING | (StartupEnabled() ? MF_CHECKED : 0), CMD_STARTUP, T(L"Start with Windows"));
        AppendMenuW(m, MF_STRING | (g_tray ? MF_CHECKED : 0), CMD_TRAY, T(L"Show in system tray"));
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, CMD_ABOUT, T(L"About DeskTick\u2026"));
        // "Exit" quits the app outright: with no taskbar button, and a tray
        // icon only if it was asked for, there may be nothing left behind to
        // reopen it from.
        AppendMenuW(m, MF_STRING, CMD_CLOSE, T(L"Exit"));
        SetForegroundWindow(hwnd);              // required for menu dismissal on NOACTIVATE windows
        POINT pt; GetCursorPos(&pt);
        g_menuUp = true;                        // the outside-click hook must ignore menu clicks
        // TPM_LAYOUTRTL mirrors the menu for Arabic and Hebrew — item text
        // right-aligned, submenus opening leftward. The clock itself is a dial
        // and needs nothing.
        UINT tpm = TPM_RETURNCMD | TPM_RIGHTBUTTON | (LocIsRTL() ? TPM_LAYOUTRTL : 0);
        int cmd = TrackPopupMenu(m, tpm, pt.x, pt.y, 0, hwnd, nullptr);
        g_menuUp = false;
        DestroyMenu(m);                         // destroys the submenu too
        if (cmd >= CMD_FACE0) {
            g_face = cmd - CMD_FACE0;
            if (g_face >= NUM_BUILTIN)
                AssetsSelect(g_face - NUM_BUILTIN);
            RefreshFace();
            SaveWindow();
            return 0;
        }
        switch (cmd) {
        case CMD_SECONDS:
            g_seconds = !g_seconds;
            RefreshFace();                      // faces size their panel around the slot
            ArmTimer();                         // switch between 1 s and 1 min cadence
            ConfigSetFlag(L"Seconds", g_seconds);
            break;
        case CMD_RESIZE:
            SetResizeMode(!g_resize);
            break;
        case CMD_CONFIG:
            CustomizeShow(hwnd, g_activeFace);
            break;
        case CMD_TOPMOST:
            g_topmost = !g_topmost;
            ApplyTopmost();
            ConfigSetFlag(L"Topmost", g_topmost);
            break;
        case CMD_STARTUP:
            // No cached copy to flip: the registry value is the state, and it
            // was read a few lines above to draw the checkmark.
            SetStartupEnabled(!StartupEnabled());
            break;
        case CMD_TRAY:
            g_tray = !g_tray;
            TraySync();
            ConfigSetFlag(L"Tray", g_tray);
            break;
        case CMD_ABOUT:
            AboutShow(hwnd);
            break;
        case CMD_CLOSE:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    }
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        UINT dpi = HIWORD(wp);
        if (g_drag) { g_drag = false; ReleaseCapture(); }  // don't fight a live drag
        // Rescale from the window size (g_winPx), never the drag buffer (g_px).
        CreateResources(MulDiv(g_winPx, dpi, g_dpi));
        g_dpi = dpi;
        SetWindowPos(hwnd, nullptr, r->left, r->top, g_winPx, g_winPx,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        DrawFrame();
        return 0;
    }
    case WM_ENDRESIZE:                          // click landed outside: done resizing
        SetResizeMode(false);
        return 0;
    case WM_TRAYICON:
        // Version 3 callback: lParam is the mouse message, plainly. Either
        // button opens the clock's own menu — the icon exists to be a way into
        // it, and there is no window to restore on a left click. Sent rather
        // than duplicated: that handler already tracks the menu at the cursor,
        // which is where the tray click was.
        if (lp == WM_LBUTTONUP || lp == WM_RBUTTONUP)
            SendMessageW(hwnd, WM_NCRBUTTONUP, HTCAPTION, 0);
        return 0;
    case WM_DISPLAYCHANGE:
        // A monitor was unplugged, or one changed resolution under us. Either
        // can leave the clock in coordinates that no longer exist.
        ClampToMonitor(hwnd);
        return 0;
    case WM_SETTINGCHANGE:
        if (wp == SPI_SETWORKAREA) {            // taskbar moved, resized or un-hidden
            ClampToMonitor(hwnd);
            break;
        }
        // Light/dark switched while we're running: re-flush so the next
        // right-click opens in the new colours.
        // The config window is top-level and gets this broadcast too, but the
        // order between us is undefined and its answer would be stale — so it
        // stays out of it and we drive it, after the cache refresh.
        if (lp && !lstrcmpW((PCWSTR)lp, L"ImmersiveColorSet")) {
            ReflushMenuTheme();
            CustomizeThemeChanged();
            AboutThemeChanged();
        }
        break;                                      // let DefWindowProc see it too
    case WM_TIMECHANGE:
        // System time / time zone changed (Settings, NTP sync). Our timer
        // uses a relative due time so it won't fire early — redraw with the
        // new wall time now and re-aim at the new minute/second boundary.
        DrawFrame();
        ArmTimer();
        return 0;
    case WM_DESTROY:
        TrayDelete();                           // unconditional: a leftover icon
                                                // sits there until someone hovers it
        if (g_outsideHook) { UnhookWindowsHookEx(g_outsideHook); g_outsideHook = nullptr; }
        CustomizeClose();                       // both are ownerless, so neither
        AboutClose();                           // closes itself when we go
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Startup, then the message loop that is the whole app: create the one
// window, restore what was saved, and block until either the tick timer
// fires or a message arrives.
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // for WIC (image faces)
    InitDarkMode();

    // Both before anything builds a menu, a dialog or a dial: the first two
    // read English literals that must already have translations behind them,
    // and the third indexes date tables that are null until this call.
    LocInit();
    LocInitDateNames();

    AssetsInit();       // where the image faces come from, if there are any
    ConfigLoad();       // before the first BuildDial, so it draws configured

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_factory)))
        return 1;
    // Device-independent, created once; null on failure just means flat caps
    // for the grip arrow and a solid marquee instead of a dotted one.
    g_factory->CreateStrokeStyle(
        D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                    D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
        nullptr, 0, &g_roundStroke);
    g_factory->CreateStrokeStyle(
        D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                    D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER,
                                    10.0f, D2D1_DASH_STYLE_DASH),
        nullptr, 0, &g_dashStroke);

    WNDCLASSW wc = {};
    wc.style         = CS_DBLCLKS;              // double-click cycles the face
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ClockWidget";
    RegisterClassW(&wc);

    // Restore face, size and position. Each keeps the default below when its
    // key is missing, so a fresh install starts in a sane place.
    int wantPx = 0, wx = 120, wy = 120;
    ConfigLoadWindow(&g_face, &wantPx, &wx, &wy);

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"DeskTick", WS_POPUP,
        wx, wy, 1, 1, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    // Always pinned: owned by the desktop so Win+D skips us. Harmless if the
    // desktop can't be found (shell not running) — the retry picks it up.
    PinToDesktop();

    // The persisted window toggles. Topmost and Tray are no-ops in their off
    // state, so they are applied unconditionally rather than branched around.
    // Seconds needs no apply call, but must be read before CreateResources
    // below: BuildDial sizes the Digital faces' panel from ShowSeconds().
    g_topmost = ConfigGetFlag(L"Topmost", false);
    g_tray    = ConfigGetFlag(L"Tray", false);
    g_seconds = ConfigGetFlag(L"Seconds", false);
    ApplyTopmost();
    TraySync();

    g_dpi = GetDpiForWindow(g_hwnd);
    if (!CreateResources(wantPx ? wantPx : MulDiv((int)DIAL, g_dpi, 96))) return 1;
    SetWindowPos(g_hwnd, nullptr, 0, 0, g_px, g_px,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ClampToMonitor(g_hwnd);                 // the saved spot may be on a monitor
                                            // that is no longer there — only now
                                            // is the size known to check against
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    DrawFrame();

    g_timer = CreateWaitableTimerExW(nullptr, nullptr,
                                     CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                     TIMER_ALL_ACCESS);
    if (!g_timer) return 1;
    ArmTimer();

    // The zero-idle loop: this thread is blocked in the kernel — zero CPU —
    // until either the timer fires or a window message arrives. No polling,
    // no WM_TIMER, no Sleep.
    for (;;) {
        DWORD r = MsgWaitForMultipleObjectsEx(1, &g_timer, INFINITE,
                                              QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (r == WAIT_OBJECT_0) {
            DrawFrame();
            ArmTimer();
            if (g_needOwner) PinToDesktop();   // shell wasn't up yet; keep trying
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                CloseHandle(g_timer);
                SafeRelease(g_dial); SafeRelease(g_brush); SafeRelease(g_rt);
                SafeRelease(g_roundStroke); SafeRelease(g_dashStroke);
                SafeRelease(g_factory);
                DeleteDC(g_memDC); DeleteObject(g_dib);
                return (int)msg.wParam;
            }
            // Both are plain windows, so Tab/Escape/mnemonics only work if
            // their messages go through IsDialogMessage first.
            if (CustomizeIsDialogMessage(&msg) || AboutIsDialogMessage(&msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}
