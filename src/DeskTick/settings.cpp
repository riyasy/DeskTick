// settings.cpp — the face options as data: their defaults, their clamping,
// and the INI they persist to.
//
// Nothing in this file creates a window or takes an HWND. The configure dialog
// is the only caller of most of it, and the only reason any of it is not static.
//
// The INI uses the Win32 profile APIs, so there is no parser here. Colours are
// stored as RRGGBB hex — what anyone hand-editing the file expects — and bools
// and choice indices as plain decimal.

#include <windows.h>
#include <stdlib.h>                     // wcstol
#include <limits.h>
#include <strsafe.h>
#include <appmodel.h>                   // GetCurrentPackageFullName
#include <winrt/Windows.Foundation.h>    // IAsyncOperation::get, used just below
#include <winrt/Windows.ApplicationModel.h>   // StartupTask, packaged only
#include "faces.h"
#include "app.h"

#pragma comment(lib, "advapi32")         // the Run key, below
#pragma comment(lib, "windowsapp")       // the StartupTask API, below

// ------------------------------------------------------------------
// Packaged or plain exe
// ------------------------------------------------------------------
// The two places this app cannot behave the same way in an MSIX package as it
// does as a loose exe: where the INI lives, and how "Start with Windows" is
// asked for. Both are below; this is the test they share.
//
// GetCurrentPackageFullName answers APPMODEL_ERROR_NO_PACKAGE with no package
// and asks for a bigger buffer when there is one, so a length probe with no
// buffer is the whole check. Cheap enough to leave uncached — it is called
// twice per right-click at most.
static bool Packaged()
{
    UINT32 n = 0;
    return GetCurrentPackageFullName(&n, nullptr) == ERROR_INSUFFICIENT_BUFFER;
}

// ------------------------------------------------------------------
// INI, beside the exe.
// ------------------------------------------------------------------
static WCHAR s_ini[MAX_PATH];

// The exe's own path with its extension swapped for .ini, worked out once.
static const WCHAR* IniPath()
{
    if (!s_ini[0]) {
        // Packaged: the exe sits under WindowsApps, which is read-only to the
        // app itself, so an INI beside it would be read at startup and never
        // written — every setting lost on exit, and silently, because a failed
        // WritePrivateProfileString is a return value nobody checks. The MSIX
        // container redirects %LOCALAPPDATA% into the package's own LocalCache,
        // so this stays per-user and goes with the package on uninstall.
        WCHAR local[MAX_PATH];
        if (Packaged() &&
            GetEnvironmentVariableW(L"LOCALAPPDATA", local, _countof(local)) &&
            SUCCEEDED(StringCchPrintfW(s_ini, _countof(s_ini), L"%s\\DeskTick.ini", local)))
            return s_ini;

        GetModuleFileNameW(nullptr, s_ini, MAX_PATH);
        WCHAR* slash = wcsrchr(s_ini, L'\\');
        WCHAR* dot   = wcsrchr(slash ? slash : s_ini, L'.');
        // Bounded to what is actually left of the buffer: an exe path near the
        // MAX_PATH limit has no room for the four characters of ".ini", and a
        // name with no dot at all has none whatsoever. lstrcpyW takes no size.
        WCHAR* end = dot ? dot : s_ini + lstrlenW(s_ini);
        StringCchCopyW(end, _countof(s_ini) - (size_t)(end - s_ini), L".ini");
    }
    return s_ini;
}

// The image face's GetName() is the image's full path, which would make a
// new INI section per file. It gets one fixed section instead.
const WCHAR* Section(const IClockFace* f)
{
    return f == g_faceImage ? L"Image" : f->GetName();
}

// How many entries an OPT_CHOICE has; its `choices` array is null-terminated.
static int ChoiceCount(const FaceOpt& o)
{
    int n = 0;
    while (o.choices && o.choices[n]) n++;
    return n;
}

// Anything read from the INI is user-editable text: clamp it before it
// reaches a face. An out-of-range choice index would index past the
// null-terminated `choices` array on the very next repaint.
void ClampOpt(const FaceOpt& o)
{
    int v = *o.value;
    switch (o.kind) {
    case OPT_BOOL:   *o.value = v ? 1 : 0; break;
    case OPT_COLOR:  *o.value = v & 0x00FFFFFF; break;
    case OPT_CHOICE: {
        int n = ChoiceCount(o);
        *o.value = (v < 0 || v >= n) ? 0 : v;
        break;
    }
    }
}

// Read one face's section into its option values; anything absent keeps the
// face's own default.
static void LoadFace(const IClockFace* f)
{
    const FaceOpt* o;
    int n = f->GetOptions(&o);
    for (int i = 0; i < n; i++) {
        WCHAR buf[32];
        GetPrivateProfileStringW(Section(f), o[i].label, L"", buf, 32, IniPath());
        if (!buf[0]) continue;                          // absent: keep the default
        long v = wcstol(buf, nullptr, o[i].kind == OPT_COLOR ? 16 : 10);
        // Stored RRGGBB, but COLORREF is 0x00BBGGRR — swap on the way in.
        *o[i].value = o[i].kind == OPT_COLOR
                    ? (int)RGB((v >> 16) & 255, (v >> 8) & 255, v & 255)
                    : (int)v;
        ClampOpt(o[i]);
    }
}

// Write one face's option values back to its section.
static void SaveFace(const IClockFace* f)
{
    const FaceOpt* o;
    int n = f->GetOptions(&o);
    for (int i = 0; i < n; i++) {
        WCHAR buf[32];
        int v = *o[i].value;
        if (o[i].kind == OPT_COLOR)
            wsprintfW(buf, L"%02X%02X%02X", GetRValue(v), GetGValue(v), GetBValue(v));
        else
            wsprintfW(buf, L"%d", v);
        WritePrivateProfileStringW(Section(f), o[i].label, buf, IniPath());
    }
}

// ---- built-in defaults ----
// An option's default is its file-static's initialiser, so the value is
// already in the program; a `def` field on FaceOpt would be a second copy to
// keep in sync. These are snapshots taken at the one moment the values are
// known untouched — before ConfigLoad reads a line of INI. Keyed by the value
// pointer, so a face's table can be reordered without invalidating anything.
//
// One entry per option across every face — about 85 of them today, so the
// bound below is headroom rather than a fit. It is a bound and not an
// assertion because nothing here can count the options at compile time: each
// face's table is a static in its own TU and only GetOptions can say how long
// it is.
struct Default { int* p; int def; };
static Default s_def[512];
static int     s_nDef;

// Record one face's current option values as that face's defaults.
static void SnapshotFace(const IClockFace* f)
{
    const FaceOpt* o;
    int n = f->GetOptions(&o);
    for (int i = 0; i < n; i++) {
        if (s_nDef >= _countof(s_def)) {
            // Overrunning here is not a crash, it is a silence: every option
            // past this point has no default recorded, so "Reset this face"
            // quietly does nothing for whichever faces snapshotted last. Say
            // so where a developer will actually see it — the debugger is the
            // only output channel this app has, and reaching this at all means
            // the tables grew several times over.
            OutputDebugStringW(L"DeskTick: s_def too small - Reset this face will no-op\n");
            return;
        }
        s_def[s_nDef].p   = o[i].value;
        s_def[s_nDef].def = *o[i].value;
        s_nDef++;
    }
}

// Every face, not just the active one — the user may have edited the file by
// hand, and a face's options must be right the first time it is selected.
void ConfigLoad()
{
    for (int i = 0; i < NUM_BUILTIN; i++) SnapshotFace(g_builtinFaces[i]);
    SnapshotFace(g_faceImage);              // must precede every LoadFace

    for (int i = 0; i < NUM_BUILTIN; i++) LoadFace(g_builtinFaces[i]);
    LoadFace(g_faceImage);
}

// Every face again, not just the active one: the whole file is rewritten so a
// section can never be left describing a value that is no longer set.
void ConfigSave()
{
    for (int i = 0; i < NUM_BUILTIN; i++) SaveFace(g_builtinFaces[i]);
    SaveFace(g_faceImage);
}

// Put one face's options back to the values it was built with. The dialog owns
// the button, but the defaults table is here, so the restore is too — the
// dialog is left with only the part that touches controls.
//
// One face, not all of them: the dialog shows one at a time and the reset
// follows it. Other faces keep whatever they were set to.
void ResetFaceDefaults(const IClockFace* f)
{
    const FaceOpt* o;
    int n = f->GetOptions(&o);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < s_nDef; k++)
            if (s_def[k].p == o[i].value) { *o[i].value = s_def[k].def; break; }
}

// ---- [Window]: face, size and position ----
// The file exists anyway, and re-picking a face and re-dragging the clock
// back into its corner on every launch is the thing that actually annoys.

// Store the face index, the window's pixel size and its top-left corner.
//
// An image face is stored by name as well as by index. The index alone is a
// position in the assets listing, which is directory order and settled by
// whatever else is in the folder — so one file dropped in ahead of it would
// bring back a different picture under the same number, and a file deleted
// would bring back whatever slid into its place.
void ConfigSaveWindow(int face, int px, int x, int y)
{
    WCHAR buf[32];
    wsprintfW(buf, L"%d", face); WritePrivateProfileStringW(L"Window", L"Face", buf, IniPath());
    if (face >= NUM_BUILTIN)
        WritePrivateProfileStringW(L"Window", L"Image", AssetsName(face - NUM_BUILTIN), IniPath());
    wsprintfW(buf, L"%d", px);   WritePrivateProfileStringW(L"Window", L"Size", buf, IniPath());
    wsprintfW(buf, L"%d", x);    WritePrivateProfileStringW(L"Window", L"X",    buf, IniPath());
    wsprintfW(buf, L"%d", y);    WritePrivateProfileStringW(L"Window", L"Y",    buf, IniPath());
}

// Each out-param is left alone when the key is absent, so the caller's
// startup defaults survive a missing or partial file.
void ConfigLoadWindow(int* face, int* px, int* x, int* y)
{
    *face = GetPrivateProfileIntW(L"Window", L"Face", *face, IniPath());
    if (*face >= NUM_BUILTIN) {
        // An image face. The saved index means nothing until the folder has
        // been listed and means the wrong thing if the folder changed since,
        // so the saved *name* is what decides: list once and look it up. One
        // directory scan, and only on a launch whose last face was an image —
        // which is why this is not the double-click path's problem (assets.cpp).
        //
        // Selecting it here rather than handing the name back to the caller
        // keeps the [Window] loader's four out-params as they are, and keeps
        // the engine free of any notion that a face index needs resolving.
        WCHAR name[MAX_PATH];
        GetPrivateProfileStringW(L"Window", L"Image", L"", name, _countof(name), IniPath());
        AssetsRefresh();
        int i = name[0] ? AssetsFind(name) : -1;
        // Deleted, renamed, or an INI from a machine with a different folder:
        // fall back to the default face rather than to an arbitrary image.
        if (i < 0) { *face = 0; }
        else       { *face = NUM_BUILTIN + i; AssetsSelect(i); }
    }
    if (*face < 0) *face = 0;
    *px = GetPrivateProfileIntW(L"Window", L"Size", *px, IniPath());
    if (*px < 96 || *px > 4096) *px = 0;                // 0 = caller keeps its own

    // Restored unchecked: clock.cpp clamps the finished window onto a monitor
    // (ClampToMonitor), which is the same check with the size known — and it
    // slides the clock back to the nearest edge instead of throwing the corner
    // it was parked in away.
    int sx = GetPrivateProfileIntW(L"Window", L"X", INT_MIN, IniPath());
    int sy = GetPrivateProfileIntW(L"Window", L"Y", INT_MIN, IniPath());
    if (sx != INT_MIN && sy != INT_MIN) { *x = sx; *y = sy; }
}

// The window-level toggles that are not face options — always on top, the tray
// icon — as one pair of calls rather than a named pair each. Same section as
// the geometry: it is the same window they describe, and anything in [Window]
// is already the thing that survives a face change.
bool ConfigGetFlag(const WCHAR* key, bool def)
{
    return GetPrivateProfileIntW(L"Window", key, def, IniPath()) != 0;
}

void ConfigSetFlag(const WCHAR* key, bool on)
{
    WritePrivateProfileStringW(L"Window", key, on ? L"1" : L"0", IniPath());
}

// ---- Start with Windows ----
// HKCU\...\Run, not the INI, because we are not the one reading it. It is also
// where the user expects to find it: Task Manager's Startup tab lists this key
// and can disable an entry behind our back, which is exactly why the menu asks
// the registry every time it opens instead of caching an answer.
//
// Per-user and non-elevated by design — no scheduled task, no service, nothing
// that needs a UAC prompt to tick a menu item.
//
// The packaged build cannot use that key at all: the value would have to name
// the exe under WindowsApps, which is not launchable that way and would start
// the app without its package identity even if it were. A package declares a
// windows.startupTask extension in its manifest instead and asks Windows to
// enable it — same menu item, same "nothing is cached" rule, different API.
static const WCHAR RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR RUN_VAL[] = L"DeskTick";

// Must match TaskId in Package.appxmanifest's windows.startupTask extension.
// A mismatch is not a build error — GetAsync just throws at runtime and the
// menu item goes quietly dead.
static const WCHAR TASK_ID[] = L"DeskTickStartup";

// The packaged pair. Both swallow everything: this API throws, and a menu
// checkmark is not worth taking the app down for. C++/WinRT's get() asserts in
// a Debug build when it blocks an STA thread (which ours is) — the wait is
// safe here, its completion handler being agile, but a Debug packaged build
// will pop that assertion.
static bool StartupEnabledPkg()
{
    try {
        using namespace winrt::Windows::ApplicationModel;
        return StartupTask::GetAsync(TASK_ID).get().State() == StartupTaskState::Enabled;
    } catch (...) { return false; }
}

static void SetStartupEnabledPkg(bool on)
{
    try {
        using namespace winrt::Windows::ApplicationModel;
        auto t = StartupTask::GetAsync(TASK_ID).get();
        // RequestEnableAsync is a silent no-op once the user has switched the
        // app off in Task Manager — the state sticks at DisabledByUser and
        // only Task Manager can lift it. Nothing here caches, so the next menu
        // simply comes up unticked, which is the truth.
        if (on) t.RequestEnableAsync().get(); else t.Disable();
    } catch (...) {}
}

bool StartupEnabled()
{
    if (Packaged()) return StartupEnabledPkg();

    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return false;
    bool on = RegQueryValueExW(k, RUN_VAL, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(k);
    return on;
}

void SetStartupEnabled(bool on)
{
    if (Packaged()) { SetStartupEnabledPkg(on); return; }

    HKEY k;
    // The key exists on every Windows install, but RegCreateKeyEx opens an
    // existing key rather than failing, so it covers the one that doesn't.
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    if (on) {
        WCHAR exe[MAX_PATH], val[MAX_PATH + 4];
        DWORD n = GetModuleFileNameW(nullptr, exe, _countof(exe));
        // Quoted: an unquoted path with a space in it is read by CreateProcess
        // as a program name plus arguments, and "C:\Program" does not exist.
        if (n && n < _countof(exe) &&
            SUCCEEDED(StringCchPrintfW(val, _countof(val), L"\"%s\"", exe)))
            RegSetValueExW(k, RUN_VAL, 0, REG_SZ, (const BYTE*)val,
                           (DWORD)((lstrlenW(val) + 1) * sizeof(WCHAR)));
    } else {
        RegDeleteValueW(k, RUN_VAL);
    }
    RegCloseKey(k);
}
