// winutil.cpp — the Win32 odds and ends that carry no clock state.
//
// The jobs here share this file for one reason: none of them reads or writes a
// single one of the engine's globals, and each is wanted by more than one
// caller — which is what keeps them out of clock.cpp.
//
// If that ever stops holding — the dark-mode block picks up state of its own —
// the split point is the horizontal rule above it.

#include <windows.h>
#include <strsafe.h>
#include "app.h"

// ------------------------------------------------------------------
// Keeping a window reachable.
// ------------------------------------------------------------------

// Nothing recovers an off-screen clock. There is no taskbar button, no tray
// icon, and no keyboard move — the window never takes focus. So every position
// that can have gone stale gets pulled back onto a monitor: one restored from
// the INI, and one left behind by a display that vanished under us.
//
// Bottom-right is clamped before top-left, so a clock bigger than the screen
// keeps its top-left corner on it rather than hiding the one corner you can
// grab. The dialogs use it too — each opens beside the clock, so a clock near
// an edge would otherwise put one past it.
void ClampToMonitor(HWND h)
{
    RECT r;
    if (!h || !GetWindowRect(h, &r)) return;
    MONITORINFO mi = { sizeof(mi) };
    // MONITOR_DEFAULTTONEAREST: once the monitor the clock was on is gone,
    // there is no correct answer left, only a reachable one.
    if (!GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi)) return;
    // Clamp against the work area: the clock is owned by the desktop and draws
    // behind the taskbar, so a spot under it is as good as off-screen.
    int x = r.left, y = r.top, w = r.right - r.left, ht = r.bottom - r.top;
    if (x + w  > mi.rcWork.right)  x = mi.rcWork.right - w;
    if (y + ht > mi.rcWork.bottom) y = mi.rcWork.bottom - ht;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top)  y = mi.rcWork.top;
    if (x != r.left || y != r.top)
        SetWindowPos(h, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// ------------------------------------------------------------------
// The desktop's SHELLDLL_DefView window: normally a child of Progman, but
// lives under a WorkerW when the wallpaper is in slideshow mode. Making our
// window OWNED by it (GWLP_HWNDPARENT) exempts it from Show Desktop / Win+D,
// which only minimizes app-owned windows.
// (Same technique as DateLine's WindowHelper.SetAsDesktopChild.)
// ------------------------------------------------------------------
HWND FindDefView()
{
    HWND dv = FindWindowExW(FindWindowW(L"Progman", nullptr), nullptr,
                            L"SHELLDLL_DefView", nullptr);
    HWND w = nullptr;
    while (!dv && (w = FindWindowExW(nullptr, w, L"WorkerW", nullptr)) != nullptr)
        dv = FindWindowExW(w, nullptr, L"SHELLDLL_DefView", nullptr);
    return dv;
}

// ------------------------------------------------------------------
// A folder that ships beside the exe.
// ------------------------------------------------------------------

// Look for `name` next to the exe, then walking up a few levels — which covers
// running out of DeskTick\x64\Release with the folder in DeskTick\. Two callers want
// exactly this: assets.cpp for `assets`, loc.cpp for `lang`.
//
// StringCchPrintfW rather than wsprintfW: this joins a MAX_PATH directory and a
// name into a MAX_PATH buffer, which is the one case wsprintfW's missing size
// argument gets wrong.
bool FindNearExe(const WCHAR* name, WCHAR* out, size_t cch)
{
    WCHAR dir[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, dir, MAX_PATH)) return false;
    for (int up = 0; up < 4; up++) {
        WCHAR* slash = wcsrchr(dir, L'\\');
        if (!slash) break;
        *slash = 0;                                  // strip file / last dir
        WCHAR probe[MAX_PATH];
        if (FAILED(StringCchPrintfW(probe, _countof(probe), L"%s\\%s", dir, name)))
            continue;
        DWORD a = GetFileAttributesW(probe);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY))
            return SUCCEEDED(StringCchCopyW(out, cch, probe));
    }
    return false;
}

// ------------------------------------------------------------------
// Dark mode. There is no public API for menu theming — uxtheme ordinals 135
// (SetPreferredAppMode) and 136 (FlushMenuThemes) are what Explorer itself
// uses. AllowDark means "follow the system setting", so nothing here reads the
// registry or picks colours.
//
// The build check is load-bearing, not politeness: before 17763 those ordinals
// are unrelated private functions with different signatures.
//
// This covers the menus only; the dialogs theme their own controls
// (dlgchrome.cpp), because common controls need a per-control opt-in.
// ------------------------------------------------------------------
static void (WINAPI* g_refreshColorPolicy)();       // ordinal 104
static bool (WINAPI* g_shouldAppsUseDark)();        // ordinal 132
static void (WINAPI* g_flushMenuThemes)();          // ordinal 136

// Answers false on anything too old to have the ordinals, which is what we
// want — no dark mode there to follow.
bool IsDarkMode()
{
    return g_shouldAppsUseDark && g_shouldAppsUseDark();
}

// Order matters, and it is the whole reason 104 exists here: uxtheme caches
// the light/dark policy, so flushing the menu theme on its own re-resolves to
// the value cached at startup and the menu never changes.
void ReflushMenuTheme()
{
    if (g_refreshColorPolicy) g_refreshColorPolicy();
    if (g_flushMenuThemes)    g_flushMenuThemes();
}

// Opt the process into dark menus and resolve the three ordinals used above.
// Called once at startup; leaves everything null on a build without them.
void InitDarkMode()
{
    OSVERSIONINFOW vi = { sizeof(vi) };             // RTL_OSVERSIONINFOW is the same layout
    LONG (WINAPI* getVer)(OSVERSIONINFOW*) = (LONG (WINAPI*)(OSVERSIONINFOW*))
        GetProcAddress(GetModuleHandleW(L"ntdll"), "RtlGetVersion");
    if (!getVer || getVer(&vi) != 0 || vi.dwBuildNumber < 17763)
        return;                                     // pre-1809: no dark mode, leave it alone
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");      // never freed: process lifetime
    if (!ux) return;
    int (WINAPI* setAppMode)(int) = (int (WINAPI*)(int))GetProcAddress(ux, MAKEINTRESOURCEA(135));
    g_refreshColorPolicy = (void (WINAPI*)())GetProcAddress(ux, MAKEINTRESOURCEA(104));
    g_shouldAppsUseDark  = (bool (WINAPI*)())GetProcAddress(ux, MAKEINTRESOURCEA(132));
    g_flushMenuThemes    = (void (WINAPI*)())GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (setAppMode) setAppMode(1);                  // 1 = AllowDark
    ReflushMenuTheme();
}
