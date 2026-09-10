// loc.cpp — the UI language, and the dates.
//
// Two jobs that look like one and are not. Which *words* the chrome uses comes
// from a file we ship (T); how a *date* is written comes from Windows
// (LocInitDateNames, LocAmPm, LocDate, LocTimeSep).
//
// They answer to different Windows settings, and the code has to read the right
// one for each or it is wrong in a way no amount of translation fixes:
//
//   the UI half   -> GetUserPreferredUILanguages   (Settings > Display language)
//   the date half -> LOCALE_NAME_USER_DEFAULT      (Settings > Regional format)
//
// A user with English Windows and a German region wants English menus and
// German month names, and gets both. Reading one setting for both jobs — which
// this file did until the Region call was replaced — gets one of them backwards
// for anyone whose two settings disagree, and they disagree by default on more
// machines than you would guess.
//
// ---- The translation file ----
//
// The English string is the key. No numeric ids, no resource.h, nothing to keep
// in sync, and no way for an id and a string to drift apart; a missing key
// answers itself, so a half-translated file is a working file and English is
// the fallback for free. The cost is that the English literal at each call site
// is now an identifier — editing one drops its translations, the same hazard
// FaceOpt::label already carries (faces.h).
//
// Storage is lang\<locale>.ini, read with GetPrivateProfileSectionW: one call
// returns the whole [Strings] section as a double-null-terminated block of
// key=value, which is a parser we then do not write. It is the same profile API
// settings.cpp uses for DeskTick.ini, so this file adds no dependency.
//
// **The file must be UTF-16LE with a BOM.** The profile APIs decide the
// encoding from the BOM alone; without one they read the file in the system
// codepage and every Malayalam, CJK and Arabic string arrives as mojibake —
// silently, with the source looking perfectly correct. This is the same trap a
// non-ASCII literal in a BOM-less .cpp carries, one file further out.
// Nothing here ever writes to these files, so the API cannot rewrite one as
// ANSI behind us.

#include <windows.h>
#include <strsafe.h>
#include "faces.h"
#include "app.h"

// One block holds every string, keys included. The whole UI is ~130 short
// strings; 32K of WCHARs is several times what that needs and is still a rounding
// error against one dial bitmap.
static WCHAR s_strings[32768];
static bool  s_rtl;

// ------------------------------------------------------------------
// Loading
// ------------------------------------------------------------------

// Try one lang\<name>.ini. False if it isn't there or holds no [Strings].
static bool LoadLang(const WCHAR* dir, const WCHAR* name)
{
    WCHAR path[MAX_PATH];
    if (FAILED(StringCchPrintfW(path, _countof(path), L"%s\\%s.ini", dir, name)))
        return false;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return false;
    // Returns the number of characters copied, not counting the final null.
    // Zero means no such section, or an empty one — either way, nothing to use.
    DWORD n = GetPrivateProfileSectionW(L"Strings", s_strings, _countof(s_strings), path);
    if (n == 0) { s_strings[0] = 0; return false; }
    return true;
}

// Last resort for one language: any lang\<prefix>-*.ini at all. This is what
// serves the regional variants nobody ships a file for — es-MX, es-AR and
// es-CO all land on es-ES.ini, de-AT and de-CH on de-DE.ini. Without it every
// one of those users would get English while a translation of their own
// language sat unread in the folder, and Spanish alone has twenty variants of
// which we ship exactly one.
//
// It resolves by directory order, which picks the *language* right and can
// pick the *flavour* wrong: zh-HK takes zh-CN.ini — Simplified, where Hong
// Kong writes Traditional — because zh-CN sorts ahead of zh-TW, and pt-AO
// takes pt-BR.ini for the same reason. The fix is one file and no code:
// tier 1 is tried before this is reached at all, so dropping in a zh-HK.ini
// overrides it.
static bool LoadLangByPrefix(const WCHAR* dir, const WCHAR* prefix)
{
    WCHAR pat[MAX_PATH];
    if (FAILED(StringCchPrintfW(pat, _countof(pat), L"%s\\%s-*.ini", dir, prefix)))
        return false;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        WCHAR* dot = wcsrchr(fd.cFileName, L'.');
        if (dot) *dot = 0;                           // LoadLang appends .ini itself
        ok = LoadLang(dir, fd.cFileName);
    } while (!ok && FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

void LocInit()
{
    // The user's *display language* chain, most preferred first — NOT
    // GetUserDefaultLocaleName, which is the Region setting and answers a
    // different question. The two genuinely differ in the field: this machine
    // reports region en-IN and UI language en-GB. Reading the region to choose
    // the UI language gets it wrong in both directions — English Windows with a
    // German region would come up in German, and German Windows with a US
    // region in English.
    //
    // The chain matters as much as the name. Windows answers e.g.
    // "de-AT" -> "de" -> "de-DE", and following it is how a language pack we
    // don't have an exact file for still resolves to one we do. 512 WCHARs is
    // dozens of languages; a chain that overflows it leaves us in English,
    // which is the same answer as no folder at all.
    WCHAR langs[512] = { 0 };
    ULONG count = 0, cch = _countof(langs);
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, langs, &cch))
        langs[0] = 0;

    // Mirroring follows the language the UI is *written in*, so it reads from
    // the same chain — an Arabic display language on a US region still wants
    // mirrored windows, with or without an ar-SA.ini behind them. A null name
    // is LOCALE_NAME_USER_DEFAULT, the documented fallback if the chain is
    // empty.
    DWORD layout = 0;
    if (GetLocaleInfoEx(langs[0] ? langs : nullptr,
                        LOCALE_IREADINGLAYOUT | LOCALE_RETURN_NUMBER,
                        (WCHAR*)&layout, sizeof(layout) / sizeof(WCHAR)))
        s_rtl = (layout == 1);                       // 1 = RTL, reading LTR lines

    WCHAR dir[MAX_PATH];
    if (!FindNearExe(L"lang", dir, _countof(dir))) return;   // English, then

    // Every tier for one language before moving to the next, which is the whole
    // point of a preference order: a de-AT primary must reach de-DE.ini before
    // an en-US secondary is even considered.
    //
    // Full name first, and that matters rather than being tidy: pt-PT/pt-BR and
    // zh-CN/zh-TW are different files, and going straight to the "pt" or "zh"
    // prefix would hand half of those users the other half's translation.
    for (const WCHAR* p = langs; *p; p += lstrlenW(p) + 1) {
        WCHAR name[LOCALE_NAME_MAX_LENGTH];
        if (FAILED(StringCchCopyW(name, _countof(name), p))) continue;
        if (LoadLang(dir, name)) return;                     // de-DE.ini
        WCHAR* dash = wcschr(name, L'-');
        if (!dash) continue;
        *dash = 0;
        if (LoadLang(dir, name)) return;                     // de.ini
        if (LoadLangByPrefix(dir, name)) return;             // de-AT -> de-DE.ini
    }
}

// ------------------------------------------------------------------
// Lookup
// ------------------------------------------------------------------

const WCHAR* T(const WCHAR* en)
{
    if (!en || !s_strings[0]) return en;
    // Linear scan of the double-null-terminated block. ~130 entries, walked a
    // couple of dozen times when a menu or the dialog is built, and never per
    // frame — an index would cost more code than it saves.
    for (const WCHAR* p = s_strings; *p; p += lstrlenW(p) + 1) {
        const WCHAR* eq = wcschr(p, L'=');
        if (!eq || eq == p) continue;                // no key, or no value: skip
        // Compare only the key half, without copying it out.
        if (CompareStringOrdinal(p, (int)(eq - p), en, -1, TRUE) != CSTR_EQUAL)
            continue;
        // An empty value is a string a translator has not filled in yet. That
        // is a normal state of a shipped file, and it means English.
        return eq[1] ? eq + 1 : en;
    }
    return en;
}

bool LocIsRTL()
{
    return s_rtl;
}

// ------------------------------------------------------------------
// Dates, from the region rather than from us
// ------------------------------------------------------------------

// Copy one LOCALE_* string into a table slot, uppercased. The faces uppercase
// their date deliberately, and one CharUpperW covers every locale we ship:
// it is a no-op on CJK, Malayalam and Arabic, which have no case at all, and
// it leaves German ß alone rather than expanding it. No per-script branch.
static void FillName(LCTYPE what, WCHAR* slot, size_t cch)
{
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, what, slot, (int)cch))
        CharUpperW(slot);
}

// Storage for the four tables faces.h publishes. They were compile-time English
// literals; the faces index them exactly as before, so no face's drawing code
// changed to get translated names.
static WCHAR s_monAbbr[12][32], s_monFull[12][64];
static WCHAR s_dayAbbr[7][32],  s_dayFull[7][64];

const WCHAR* MONTH_ABBR[12];
const WCHAR* DAY_ABBR[7];
const WCHAR* MONTH_FULL[12];
const WCHAR* DAY_FULL[7];

void LocInitDateNames()
{
    for (int i = 0; i < 12; i++) {
        FillName(LOCALE_SABBREVMONTHNAME1 + i, s_monAbbr[i], _countof(s_monAbbr[i]));
        FillName(LOCALE_SMONTHNAME1       + i, s_monFull[i], _countof(s_monFull[i]));
        MONTH_ABBR[i] = s_monAbbr[i];
        MONTH_FULL[i] = s_monFull[i];
    }
    // The rotation is the whole trick, and getting it wrong is invisible until a
    // Sunday: LOCALE_SDAYNAME1 is *Monday*, but these tables are indexed by
    // SYSTEMTIME::wDayOfWeek, where 0 is Sunday. So slot 0 takes DAYNAME7 and
    // the rest shift down by one.
    for (int i = 0; i < 7; i++) {
        int d = (i == 0) ? 6 : i - 1;                // 0(Sun)->...NAME7, 1(Mon)->...NAME1
        FillName(LOCALE_SABBREVDAYNAME1 + d, s_dayAbbr[i], _countof(s_dayAbbr[i]));
        FillName(LOCALE_SDAYNAME1       + d, s_dayFull[i], _countof(s_dayFull[i]));
        DAY_ABBR[i] = s_dayAbbr[i];
        DAY_FULL[i] = s_dayFull[i];
    }
}

const WCHAR* LocTimeSep()
{
    static WCHAR s_sep[8];
    static bool  s_done;
    if (!s_done) {
        // fi-FI writes 10.30, not 10:30 — one of the twenty locales we ship, so
        // this is not hypothetical. Falls back to a colon, which is both the
        // overwhelming majority answer and what every face's slot was measured
        // against.
        if (!GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STIME, s_sep, _countof(s_sep))
            || !s_sep[0])
            lstrcpynW(s_sep, L":", _countof(s_sep));
        s_done = true;
    }
    return s_sep;
}

const WCHAR* LocAmPm(bool pm)
{
    static WCHAR s_am[16], s_pm[16];
    static bool  s_done;
    if (!s_done) {
        // A 24-hour locale legitimately has none; the empty string is then the
        // right answer and the caller draws nothing.
        if (!GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_S1159, s_am, _countof(s_am)))
            s_am[0] = 0;
        if (!GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_S2359, s_pm, _countof(s_pm)))
            s_pm[0] = 0;
        CharUpperW(s_am);
        CharUpperW(s_pm);
        s_done = true;
    }
    return pm ? s_pm : s_am;
}

const WCHAR* LocDate(const SYSTEMTIME& st, bool longForm, WCHAR* out, size_t cch)
{
    out[0] = 0;
    // A null picture means "the user's own short or long date", which is the
    // point: it carries field *order* as well as names. ja-JP writes 3月2日,
    // hu-HU writes 2026. 03. 02. — an order no "%s %s %d" reaches.
    if (!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,
                         longForm ? DATE_LONGDATE : DATE_SHORTDATE,
                         &st, nullptr, out, (int)cch, nullptr))
        out[0] = 0;
    CharUpperW(out);
    return out;
}
