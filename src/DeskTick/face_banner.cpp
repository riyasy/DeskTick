// face_banner.cpp — "Banner": 24-hour time over a tracked date line, no
// panel, white on whatever wallpaper is underneath.
//
// Two things worth knowing:
//
// 1. Digits go one per fixed cell (DrawCells, faces.cpp) instead of as one
//    centred string. Centre a proportional "17:49" and the run shifts
//    sideways whenever a digit's width changes — on a clock that reads as a
//    twitch every minute. Fixed cells cost no measuring and cannot drift.
//    Bahnschrift needs this: measured, its "1" is 681 design units against
//    ~1100 for every other digit, 40% narrower. Do not "simplify" this back
//    to one DrawTextW — Segoe UI's digits are tabular, Bahnschrift's are not.
//
// 2. The seconds sit on the time's baseline, not its rect centre. With
//    paragraph centring DirectWrite centres the LINE BOX, so the baseline
//    lands at roughly rectCentre + 0.38 * size: to share a baseline, a
//    smaller run's rect has to sit lower. Hence SEC_MID below. 0.38 is Segoe
//    UI's ratio and Bahnschrift is close but not identical, so SEC_MID is a
//    calibration point: nudge it if the seconds sit a hair off the baseline.
//
// Bahnschrift throughout: DIN-like, flat terminals, and the closest grotesk
// on the machine to what this face wants. Ships with Windows 10+.

#include "faces.h"

static const float TSIZE = 52.0f;               // hours and minutes
static const float CELL  = TSIZE * 0.58f;       // wider than any digit
static const float COLON = TSIZE * 0.28f;       // the colon's own narrower cell
static const float SSIZE = 20.0f;               // seconds
static const float SCELL = SSIZE * 0.58f;
static const float GAP   = 3.0f;                // time to seconds; the cells
                                                // already carry air either side

static const float TIME_MID = 112.5f;
static const float SEC_MID  = 124.6f;           // same baseline as TIME_MID — see (2)
static const float DATE_Y   = 141.0f;           // tracked layout origin
static const float DATE_H   = 14.0f;

// One shadow recipe for everything here: a hard offset, not a blur (D2D
// would need an effect graph). Same idiom as face_stack.cpp.
static const float SHADOW_DY = 1.5f;
static const float SHADOW_A  = 0.5f;

// The date comes from LocDate (loc.cpp) — the user's own long or short date,
// their field order included. Shared with Digital bold, Split and Stack.

// ---- options ----
// Same list and order as Digital bold, which draws the same two forms. They
// describe rather than sample, because the sample would differ per locale —
// see the note beside Digital bold's copy.
static const WCHAR* const DATEFMT[] = { L"Long date", L"Short date", nullptr };

static int o_hour24 = 1;
static int o_ink    = RGB(255, 255, 255);
static int o_date   = 1;
static int o_dfmt   = 0;
static int o_shadow = 1;

static const FaceOpt s_opts[] = {
    { L"24-hour clock", OPT_BOOL,   &o_hour24, nullptr },
    { L"Ink colour",    OPT_COLOR,  &o_ink,    nullptr },
    { L"Show date",     OPT_BOOL,   &o_date,   nullptr },
    { L"Date format",   OPT_CHOICE, &o_dfmt,   DATEFMT },
    { L"Drop shadow",   OPT_BOOL,   &o_shadow, nullptr },
};

class BannerFace : public IClockFace {
    mutable IDWriteFactory*    m_dw    = nullptr;   // lazy, cached forever
    mutable IDWriteTextFormat* m_fTime = nullptr;   // 52 bold
    mutable IDWriteTextFormat* m_fSec  = nullptr;   // 20 semibold
    mutable IDWriteTextFormat* m_fDate = nullptr;   // 9, tracked
    mutable IDWriteTextLayout* m_date  = nullptr;   // rebuilt once a day
    mutable WCHAR              m_dateStr[48] = {};  // what m_date holds

    // The whole time row at one vertical offset — called once for the
    // shadow, once for the ink, so the two can never disagree.
    void Row(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const WCHAR* hh,
             const WCHAR* mm, const WCHAR* ss, float x, float dy) const
    {
        DrawCells(rt, b, m_fTime, hh, CELL, x, TIME_MID - 32 + dy, TIME_MID + 32 + dy);
        x += 2 * CELL;
        DrawTextIn(rt, b, m_fTime, LocTimeSep(),
                   D2D1::RectF(x, TIME_MID - 32 + dy, x + COLON, TIME_MID + 32 + dy));
        x += COLON;
        DrawCells(rt, b, m_fTime, mm, CELL, x, TIME_MID - 32 + dy, TIME_MID + 32 + dy);
        if (!ss) return;
        DrawCells(rt, b, m_fSec, ss, SCELL, x + 2 * CELL + GAP,
                  SEC_MID - 13 + dy, SEC_MID + 13 + dy);
    }

    // Rebuild the tracked date layout only when the text actually changed,
    // i.e. once a day — a layout is an allocation.
    void SyncDate(const WCHAR* s) const
    {
        if (m_date && lstrcmpW(s, m_dateStr) == 0) return;
        SafeRelease(m_date);
        lstrcpynW(m_dateStr, s, _countof(m_dateStr));
        // Half of the 0.24em this line is tracked at: MakeTrackedLayout puts
        // `track` on BOTH sides of every glyph, so the run gets twice what
        // is passed.
        m_date = MakeTrackedLayout(m_dw, m_dateStr, m_fDate, DIAL, DATE_H, 1.08f);
    }

public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Banner"; }

    // No static art — just the hit-test wash, or the transparent square
    // would be click-through everywhere but the glyphs.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        FillHitTestWash(rt, b);
        return true;
    }

    // The five knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }

    // The time row, twice if the shadow is on, then the tracked date line.
    // Builds the three formats on first call.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        if (!m_dw) {                                  // one-time format build
            m_dw = SharedDWrite();
            if (!m_dw) return;
            m_fTime = MakeTextFormat(m_dw, L"Bahnschrift", TSIZE, DWRITE_FONT_WEIGHT_BOLD,
                                     DWRITE_TEXT_ALIGNMENT_CENTER);
            m_fSec  = MakeTextFormat(m_dw, L"Bahnschrift", SSIZE, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                     DWRITE_TEXT_ALIGNMENT_CENTER);
            m_fDate = MakeTextFormat(m_dw, L"Bahnschrift", 9.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                     DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        if (!m_fTime || !m_fDate) return;

        rt->SetTransform(base);
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        WCHAR hh[4], mm[4], ss[4], date[64];
        wsprintfW(hh, L"%02d", o_hour24 ? st.wHour
                                        : (st.wHour % 12 ? st.wHour % 12 : 12));
        wsprintfW(mm, L"%02d", st.wMinute);
        wsprintfW(ss, L"%02d", st.wSecond);

        float w = 4 * CELL + COLON + (seconds ? GAP + 2 * SCELL : 0.0f);
        float x = CX - w / 2;

        if (o_shadow) {
            b->SetColor(D2D1::ColorF(0, SHADOW_A));
            Row(rt, b, hh, mm, seconds ? ss : nullptr, x, SHADOW_DY);
        }
        b->SetColor(FromRGB(o_ink));
        Row(rt, b, hh, mm, seconds ? ss : nullptr, x, 0.0f);

        if (!o_date) return;
        // The time above is ASCII digits in Bahnschrift whatever the locale;
        // only this line changes script, and only it can fall back to another
        // font. It is also the only text here that is measured rather than
        // drawn in fixed cells, which is what lets it.
        SyncDate(LocDate(st, !o_dfmt, date, _countof(date)));
        if (m_date) {
            if (o_shadow) {
                b->SetColor(D2D1::ColorF(0, SHADOW_A));
                rt->DrawTextLayout(D2D1::Point2F(0, DATE_Y + SHADOW_DY), m_date, b);
            }
            b->SetColor(FromRGB(o_ink, 0.92f));
            rt->DrawTextLayout(D2D1::Point2F(0, DATE_Y), m_date, b);
        }
    }
};

static const BannerFace s_banner;
extern const IClockFace* const g_faceBanner = &s_banner;
