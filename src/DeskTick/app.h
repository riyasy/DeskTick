// app.h — every declaration that crosses a .cpp boundary in this app.
//
// One header rather than one per source file. At this size a header per TU
// would carry more include lines than declarations, and faces.h already sets
// the precedent of one header covering a whole subsystem: anything a face
// needs is there, everything else is here.
//
// Nothing declared here may be re-declared in a .cpp. Two declarations of one
// function is one too many: change a signature on one side and nothing points at
// the other, and the linker only catches it if the mangling happens to differ.

#pragma once
#include <windows.h>

struct IClockFace;
struct FaceOpt;

// ------------------------------------------------------------------
// clock.cpp — the engine
// ------------------------------------------------------------------

// Apply a face or option change. The dial is the only thing cached across
// frames, so rebuilding it is all any change needs; this also retargets the
// configure dialog, because a failed image face falls back to another face
// and the dialog must follow whatever was actually drawn.
void RefreshFace();

// Repaint at the current wall time, rebuilding nothing. For a nested modal
// loop only: a common dialog pumps its own messages, and the engine's loop —
// the one thing that waits on the tick timer — is not running while it does.
// See the colour picker in customize.cpp, the only caller.
void ClockRepaint();

// ShowSeconds() is declared in faces.h, not here: a face reads it from
// DrawDial, so it belongs with the rest of the face-facing surface.

// ------------------------------------------------------------------
// winutil.cpp — Win32 odds and ends with no clock state of their own
// ------------------------------------------------------------------

// Pull a window back onto its monitor's work area. Not clock-specific — the
// dialogs use it to stay reachable when they open beside a clock parked at a
// screen edge, and a face with many options grows one further out still.
void ClampToMonitor(HWND h);

// The desktop's SHELLDLL_DefView. Owning our window to it is what exempts the
// clock from Show Desktop / Win+D. Null if the shell is not running.
HWND FindDefView();

// Opt the process into dark menus and resolve the uxtheme ordinals the two
// below need. Once, at startup; a no-op on anything older than 1809.
void InitDarkMode();

// Whether Windows is in dark mode, via uxtheme ordinal 132. False on any
// build too old to carry that ordinal, which is the right answer there —
// there is no dark mode to follow.
bool IsDarkMode();

// Re-resolve the cached light/dark policy and flush the menu theme, in that
// order — the order is the whole point, see the definition.
void ReflushMenuTheme();

// Locate a folder shipped beside the exe, walking up a few levels so a dev run
// out of DeskTick\x64\Release still finds it. False if there is none, and `out` is
// then untouched. assets.cpp wants `assets`, loc.cpp wants `lang`.
bool FindNearExe(const WCHAR* name, WCHAR* out, size_t cch);

// ------------------------------------------------------------------
// loc.cpp — the UI language
// ------------------------------------------------------------------

// Load the translation file for the user's Windows language, once at startup.
// Call before anything builds a menu, a dialog or a date. No file, or no
// language folder, and every T() below simply answers in English.
void LocInit();

// Translate one UI string. The English text IS the key — there are no numeric
// ids to allocate and nothing to keep in sync, and an untranslated or missing
// string answers itself, so English can never regress.
//
// The corollary is that the English literals at the call sites are load-bearing
// identifiers: changing one silently drops its translations, exactly the way
// renaming a FaceOpt label drops what was saved under it.
const WCHAR* T(const WCHAR* en);

// Whether the UI language reads right-to-left. Only the two windows and the
// menu care; the dial is a circle.
bool LocIsRTL();

// Fill the shared date-name tables from the user's regional settings. Separate
// from LocInit because it answers to a different setting — Windows display
// language and Windows region are set independently, and a user with an English
// UI and a German region should get German month names.
//
// The tables themselves, and the two date helpers that go with them, are
// declared in faces.h: a face reads them, and faces.h is where the face-facing
// surface lives. No face includes this header.
void LocInitDateNames();

// ------------------------------------------------------------------
// assets.cpp — the image faces on disk
// ------------------------------------------------------------------

// Locate the assets folder, once at startup. Everything below reports nothing
// if there isn't one.
void AssetsInit();

// Re-list the folder and return how many files it holds. Called every time the
// menu opens, which is what makes a newly dropped image appear without a
// restart — and why a double-click, which must not pay for a directory scan,
// cycles the built-in faces only.
int AssetsRefresh();

// The i'th filename as it is on disk. The menu strips the extension itself.
const WCHAR* AssetsName(int i);

// Where a filename sits in the current listing, or -1. What the INI saves for
// an image face is the name, not the index — the index is directory order, and
// one file dropped into the folder renumbers everything after it.
int AssetsFind(const WCHAR* name);

// Point the image face at the i'th file. The caller rebuilds the dial.
void AssetsSelect(int i);

// ------------------------------------------------------------------
// settings.cpp — the options as data. No windows anywhere in it.
// ------------------------------------------------------------------

// Read every face's section from the INI. Every face, not just the active
// one: the file is hand-editable, and a face's options must be right the
// first time it is selected. Call before the first dial build.
void ConfigLoad();

// Write every face's section back. The whole file is rewritten, so a section
// can never be left describing a value that is no longer set.
void ConfigSave();

// The INI section a face's options are stored under — its name, except for the
// image face, whose name is a full path and would make a section per file.
// Also what the configure dialog puts in its title bar, for the same reason.
const WCHAR* Section(const IClockFace* f);

// Force one option's value into range. Everything read from the INI goes
// through this: an out-of-range choice index would index past the
// null-terminated `choices` array on the very next repaint.
void ClampOpt(const FaceOpt& o);

// Put one face's options back to the values it was built with.
void ResetFaceDefaults(const IClockFace* f);

// Face index, window size and top-left corner. Each loader out-param is left
// alone when its key is absent, so the caller's startup defaults survive a
// missing or partial file.
//
// An image face is saved and restored by filename, the index being directory
// order — so the loader lists the assets folder and re-points the image face
// itself when it restores one, and answers 0 when that file is gone. The
// caller gets back an index that is already correct either way.
void ConfigSaveWindow(int face, int px, int x, int y);
void ConfigLoadWindow(int* face, int* px, int* x, int* y);

// The window toggles that are not face options — "Topmost", "Tray" — in the
// same [Window] section. One pair of calls rather than a named pair each.
bool ConfigGetFlag(const WCHAR* key, bool def);
void ConfigSetFlag(const WCHAR* key, bool on);

// Start with Windows, which is HKCU\...\Run rather than the INI: Windows is
// what reads it, and Task Manager's Startup tab can turn it off behind our
// back. So the registry is the state — there is nothing cached to keep in
// sync, and the menu asks every time it opens.
bool StartupEnabled();
void SetStartupEnabled(bool on);

// ------------------------------------------------------------------
// dlgchrome.cpp — what both windows wear
// ------------------------------------------------------------------

// The frame, named once because two calls must agree on it: whatever
// CreateWindowExW is given, AdjustWindowRectExForDpi has to be given too, or
// the client area comes out short by the difference in caption height.
//
// Deliberately NOT a tool window. WS_EX_TOOLWINDOW shrinks the caption, and
// the close button shrinks with it; this is the ordinary settings-dialog
// frame. The cost is a taskbar button and an Alt+Tab slot, which is the
// accepted trade here — and the reason both classes want the app's icon.
const DWORD DLG_STYLE   = WS_POPUPWINDOW | WS_CAPTION;
const int   ICON_APP    = 1;                    // DeskTick.rc

// The extended style, as a call rather than a constant, because one bit of it
// is decided at runtime: an RTL UI language adds WS_EX_LAYOUTRTL, which is what
// mirrors the dialogs' hand-laid-out rows without touching a single coordinate.
// Still named once, and both calls must still be given the same answer.
DWORD DlgExStyle();

const COLORREF DARK_BG = RGB(32, 32, 32), DARK_FG = RGB(255, 255, 255);

// The background brush for the current theme: the dark one ours, the light
// one the system's. Windows themes a control's glyphs but never the surface
// behind them, which is what each window's WM_CTLCOLOR* handlers need this for.
HBRUSH ThemeBrush();

// Point one control at its dark theme class, or clear the override in light
// mode. "DarkMode_CFD" for combos and edits, "DarkMode_Explorer" otherwise.
void ThemeControl(HWND c, const WCHAR* darkClass);

// Darken a window's title bar, which DWM owns and no theme class can reach.
void ThemeCaption(HWND h);

// The system message font at this DPI, as a LOGFONTW: the About box derives
// two further faces from it and needs the fields, not just a handle.
void DlgMessageLogFont(UINT dpi, LOGFONTW* out);

// ------------------------------------------------------------------
// customize.cpp — the configure dialog
// ------------------------------------------------------------------

// Open, or raise if already up. `owner` only places the window beside the
// clock — the dialog is deliberately ownerless.
void CustomizeShow(HWND owner, const IClockFace* face);

// Face changed under an open dialog: swap in the new option table. No-op if
// closed, and no-op if the face is unchanged — that early-out is load-bearing,
// see the definition.
void CustomizeRetarget(const IClockFace* face);

// Close if open. Shutdown has to say so: ownerless windows do not go when the
// clock does.
void CustomizeClose();

// ------------------------------------------------------------------
// about.cpp — the About box
// ------------------------------------------------------------------

// Open, or raise if already up. Ownerless like the dialog, and modeless for a
// harder reason: a modal loop would stop the clock.
void AboutShow(HWND owner);
void AboutClose();

// ------------------------------------------------------------------
// Both windows, one call each
// ------------------------------------------------------------------
// Light/dark switched. Driven from clock.cpp rather than either window's own
// WM_SETTINGCHANGE, so the uxtheme colour cache is refreshed first.
void CustomizeThemeChanged();
void AboutThemeChanged();

// Tab / Escape / mnemonics. Both are plain windows rather than real dialogs,
// so the engine's message loop must pump these or they get none of it.
bool CustomizeIsDialogMessage(MSG* m);
bool AboutIsDialogMessage(MSG* m);
