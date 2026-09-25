// loc.cpp — the UI language, and the dates.
//
// Two jobs that look like one and are not. Which *words* the chrome uses comes
// from string tables built into the exe (T); how a *date* is written comes from
// Windows (LocInitDateNames, LocAmPm, LocDate, LocTimeSep).
//
// They answer to different Windows settings, and the code has to read the right
// one for each or it is wrong in a way no amount of translation fixes:
//
//   the UI half   -> GetUserPreferredUILanguages   (Settings > Display language)
//   the date half -> LOCALE_NAME_USER_DEFAULT      (Settings > Regional format)
//
// A user with English Windows and a German region wants English menus and
// German month names, and gets both. Reading one setting for both jobs gets one
// of them backwards for anyone whose two settings disagree, and they disagree
// by default on more machines than you would guess.
//
// ---- The string tables ----
//
// One STRINGTABLE per language in Localization\strings.rc, generated from
// Localization\translations.csv by Localization\build.ps1 and compiled into the
// exe, so the exe ships as a single file with nothing to find beside it. A
// blank translation is filled with the English at generation time, so every
// language block carries every id and a half-translated language is a working
// one.
//
// LoadStringW is not used because it takes no language: it answers in the
// thread's UI language, and SetThreadUILanguage to steer it would also change
// which language system UI on this thread loads (the colour picker, for one),
// with a fallback order that is not ours. Instead the language is chosen once
// here and T() reads that block directly.

#include <windows.h>
#include <strsafe.h>
#include "faces.h"
#include "app.h"

// RT_STRING resources are bundles of 16: ids n*16 .. n*16+15 live in bundle n+1.
static LPCWSTR Bundle(UINT id) { return MAKEINTRESOURCEW((id >> 4) + 1); }

static LANGID s_lang = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
static bool   s_rtl;
// The region every date lookup reads. Null is LOCALE_NAME_USER_DEFAULT; only a
// Debug --lang (LocInit) points it at a name, so that a promo recorded in German
// shows German weekdays under German menus.
static const WCHAR* s_region = LOCALE_NAME_USER_DEFAULT;
static WCHAR        s_forced[LOCALE_NAME_MAX_LENGTH];

// ------------------------------------------------------------------
// Choosing the language
// ------------------------------------------------------------------

// The languages strings.rc was built with, read back from the exe rather than
// listed again here — the same-language tier below needs the whole list, not
// a yes or no per LANGID. 32 is headroom over the 21 blocks shipped; a block
// past it is simply never chosen.
struct Shipped { LANGID id[32]; UINT n; };

static BOOL CALLBACK CollectLang(HMODULE, LPCWSTR, LPCWSTR, WORD lang, LONG_PTR param)
{
    Shipped* s = (Shipped*)param;
    if (s->n < _countof(s->id)) s->id[s->n++] = lang;
    return TRUE;
}

void LocInit(const WCHAR* forced)
{
    // Debug only: a --lang stands in for both Windows settings at once — the UI
    // chain below becomes that one name, and the dates read it as the region.
    // It is how the promo videos show each language without changing Windows.
    if (forced && SUCCEEDED(StringCchCopyW(s_forced, _countof(s_forced), forced)))
        s_region = s_forced;

    // The user's *display language* chain, most preferred first — NOT
    // GetUserDefaultLocaleName, which is the Region setting and answers a
    // different question. The two genuinely differ in the field: this machine
    // reports region en-IN and UI language en-GB.
    //
    // The chain matters as much as the name. Windows answers e.g.
    // "de-AT" -> "de" -> "de-DE", and following it is how a language pack we
    // have no exact block for still resolves to one we do. 512 WCHARs is
    // dozens of languages; a chain that overflows it leaves us in English.
    WCHAR langs[512] = { 0 };
    ULONG count = 0, cch = _countof(langs);
    if (s_region)                                    // a chain of one; langs is zeroed,
        StringCchCopyW(langs, _countof(langs) - 1, s_region);   // so it ends double-null
    else if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, langs, &cch))
        langs[0] = 0;

    // Mirroring follows the language the UI is *written in*, so it reads from
    // the same chain — an Arabic display language on a US region still wants
    // mirrored windows. A null name is LOCALE_NAME_USER_DEFAULT, the documented
    // fallback if the chain is empty.
    DWORD layout = 0;
    if (GetLocaleInfoEx(langs[0] ? langs : nullptr,
                        LOCALE_IREADINGLAYOUT | LOCALE_RETURN_NUMBER,
                        (WCHAR*)&layout, sizeof(layout) / sizeof(WCHAR)))
        s_rtl = (layout == 1);                       // 1 = RTL, reading LTR lines

    Shipped shipped{};
    EnumResourceLanguagesW(nullptr, RT_STRING, Bundle(IDS_LOC_FIRST), CollectLang,
                           (LONG_PTR)&shipped);

    // Both tiers for one language before moving to the next, which is the whole
    // point of a preference order: a de-AT primary must reach de-DE before an
    // en-US secondary is even considered.
    for (const WCHAR* p = langs; *p; p += lstrlenW(p) + 1) {
        // Plenty of real locales have no LCID of their own — pt-AO maps to a
        // transient one with primary language 0 — so those retry with just the
        // language part, which does have one ("pt" -> 0x16) and is all the
        // same-language tier needs. 0 after that is a name Windows cannot place
        // at all: next.
        LANGID want = LANGIDFROMLCID(LocaleNameToLCID(p, 0));
        if (PRIMARYLANGID(want) == LANG_NEUTRAL) {
            WCHAR lang[LOCALE_NAME_MAX_LENGTH];
            if (FAILED(StringCchCopyW(lang, _countof(lang), p))) continue;
            if (WCHAR* dash = wcschr(lang, L'-')) *dash = 0;
            want = LANGIDFROMLCID(LocaleNameToLCID(lang, LOCALE_ALLOW_NEUTRAL_NAMES));
            if (PRIMARYLANGID(want) == LANG_NEUTRAL) continue;
        }

        // Exact first, and that matters rather than being tidy: pt-PT/pt-BR and
        // zh-CN/zh-TW are different blocks, and going straight to the primary
        // language would hand half of those users the other half's translation.
        for (UINT i = 0; i < shipped.n; i++)
            if (shipped.id[i] == want) { s_lang = want; return; }

        // Then any block of the same language. This serves the regional variants
        // nobody ships a block for — es-MX and es-419 take es-ES, de-AT and de-CH
        // take de-DE — and also the neutral "de" entries in the chain. Without
        // it Spanish alone has twenty variants of which we ship exactly one.
        //
        // It resolves by enumeration order, which is LANGID order: the language
        // is right, the flavour can be wrong. zh-HK takes zh-TW (0x0404, before
        // zh-CN's 0x0804), which is right for Hong Kong; zh-SG takes it too,
        // which is not, as Singapore writes Simplified. pt-AO takes pt-BR. The
        // fix is a column in translations.csv, never code: an exact block wins.
        for (UINT i = 0; i < shipped.n; i++)
            if (PRIMARYLANGID(shipped.id[i]) == PRIMARYLANGID(want)) {
                s_lang = shipped.id[i];
                return;
            }
    }
}

// ------------------------------------------------------------------
// Lookup
// ------------------------------------------------------------------

const WCHAR* T(UINT id)
{
    // s_lang is always a language EnumResourceLanguagesW reported (or en-US,
    // which is always shipped), so this finds that exact block. A bundle holds
    // 16 entries back to back, each a WORD length and then that many WCHARs.
    // /n on strings.rc appends a null to every string and counts it in the
    // length, so the walk below steps over it and the pointer can be handed
    // out as is. An unused slot has length 0. Walked when a menu or a dialog is
    // built, never per frame.
    HRSRC   res = FindResourceExW(nullptr, RT_STRING, Bundle(id), s_lang);
    HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
    const WCHAR* p = mem ? (const WCHAR*)LockResource(mem) : nullptr;
    if (!p) return L"";
    for (UINT i = id & 0xF; i; i--) p += 1 + *p;
    return *p ? p + 1 : L"";                         // length 0: no such id
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
    if (GetLocaleInfoEx(s_region, what, slot, (int)cch))
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
        if (!GetLocaleInfoEx(s_region, LOCALE_STIME, s_sep, _countof(s_sep))
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
        if (!GetLocaleInfoEx(s_region, LOCALE_S1159, s_am, _countof(s_am)))
            s_am[0] = 0;
        if (!GetLocaleInfoEx(s_region, LOCALE_S2359, s_pm, _countof(s_pm)))
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
    if (!GetDateFormatEx(s_region,
                         longForm ? DATE_LONGDATE : DATE_SHORTDATE,
                         &st, nullptr, out, (int)cch, nullptr))
        out[0] = 0;
    CharUpperW(out);
    return out;
}
