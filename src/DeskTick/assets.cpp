// assets.cpp — the assets folder: finding it, listing what is in it, and
// pointing the image face at one of them.
//
// The whole of "which picture faces exist" lives here. The engine knows only
// how many there are and what to call them in the menu; it never sees the
// folder, the filenames' full paths, or the image face itself.
//
// The list is rebuilt on every right-click rather than cached, which is what
// makes dropping a new image into the folder show up without a restart. That
// is also why a double-click on the clock cycles the built-in faces only —
// it must not pay for a directory scan.

#include <windows.h>
#include <strsafe.h>
#include "faces.h"
#include "app.h"

// The menu is a popup on a widget; past a dozen or so entries it stops being
// something you can pick from anyway, so the list simply stops there.
static const int MAX_FACES = 15;

static WCHAR s_dir[MAX_PATH];                   // resolved assets folder ("" if none)
static WCHAR s_names[MAX_FACES][MAX_PATH];      // filenames, as listed
static int   s_n;

// Find the assets folder next to the exe (FindNearExe, winutil.cpp — the same
// walk lang\ needs). Leaves s_dir empty if there is none, and everything below
// then reports nothing.
void AssetsInit()
{
    FindNearExe(L"assets", s_dir, _countof(s_dir));
}

// Re-list the folder and return how many files are in it. Called every time
// the menu opens, so a new image needs no restart.
int AssetsRefresh()
{
    s_n = 0;
    if (!s_dir[0]) return 0;
    // Bounded: s_dir can itself reach MAX_PATH, and wsprintfW takes no size —
    // the two extra characters would run off the end.
    WCHAR pat[MAX_PATH];
    StringCchPrintfW(pat, _countof(pat), L"%s\\*", s_dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && s_n < MAX_FACES)
                lstrcpyW(s_names[s_n++], fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return s_n;
}

// The i'th filename, as it is on disk — the menu strips the extension for
// display, but SetImageFacePath needs the whole thing. Empty string rather
// than a null out of range: every caller draws it or copies it, and neither
// wants a null to check.
const WCHAR* AssetsName(int i)
{
    return (i >= 0 && i < s_n) ? s_names[i] : L"";
}

// Where the i'th file is in the current listing, by name, or -1. What the INI
// stores for an image face is the filename and not the index: the index is a
// position in directory order, so one file dropped into the folder ahead of it
// would silently restore a different picture under the same number.
// Case-insensitive, the folder being NTFS.
int AssetsFind(const WCHAR* name)
{
    for (int i = 0; i < s_n; i++)
        if (!lstrcmpiW(s_names[i], name)) return i;
    return -1;
}

// Point the one image face at the i'th file. The caller rebuilds the dial.
void AssetsSelect(int i)
{
    if (i >= 0 && i < s_n) SetImageFacePath(s_dir, s_names[i]);
}
