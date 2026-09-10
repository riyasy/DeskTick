// face_digital_bold.cpp — "Digital bold": heavy 24-hour time with an
// accent-coloured colon over a letter-spaced full date, centered on a dark
// panel. Two-line stack, ~2:1 content block.
//
// Three things worth knowing before editing:
//
// 1. The accent colon is its OWN run, and must stay that way. The tempting
//    shortcut is to draw the whole white "HH:MM" centred and then draw L":"
//    centred in the SAME rect on top, landing the orange glyph exactly over
//    the white one — no metrics, no layout. It leaves a visible pale rim
//    around each dot, because both passes are antialiased: an edge pixel takes
//    partial white coverage `a`, then the same partial orange over it, and the
//    leftover (1-a)*a of WHITE is a halo no amount of orange can cover. It is
//    structural, not an antialiasing setting — the only fix is to paint each
//    pixel once. So hour, colon and minute are drawn as three runs at their own
//    advances. The cells are exactly one advance wide, so the glyphs land
//    exactly where a single centred string would put them.
//
// 2. The date's letter spacing needs a tracked layout (MakeTrackedLayout in
//    faces.cpp) — plain DrawText cannot track text. A layout is an
//    allocation, so it is cached and rebuilt only when the date STRING
//    changes, i.e. once a day.
//
// 3. The seconds share the time's BASELINE (see SecMid), trailing the time
//    rather than sitting in a corner of the panel: the width arithmetic at
//    SEC_X0 shows a 54pt line leaves 11 DIP of room for them at 23:59:59.
//
// Same caveats as face_digital.cpp: text is drawn per frame (cached formats
// keep it cheap), grayscale AA must be set here, and the content is letterboxed
// into the engine's square scene.
//
// DAY_FULL / MONTH_FULL come from faces.cpp — shared with Banner and Split.

#include "faces.h"

// Panel. Two stacked bands — the time's, and the date's below it — so hiding
// the date drops a band and the rest re-centres on PMID.
static const float PX0 = 8.0f, PX1 = DIAL - 8.0f;
static const float PMID      = 118.0f;               // full panel spans 58..178
static const float TIME_BAND = 80.0f;                // 58..138
static const float DATE_BAND = 40.0f;                // 138..178

// Segoe UI Bold, measured with GetDesignGlyphMetrics on this machine: every
// digit advances 0.5752 em (tabular — which is why the runs below need no
// fixed cells of their own), the colon 0.2710 em, cap height 0.7002 em,
// ascent 1.0791 em, line 1.3301 em.
//
// Width: "00:00" at TIME_SIZE is (4 * 0.5752 + 0.2710) * 54 = 138.9 DIP.
// Centred in the panel it spans x50.6..189.4 — the same for every minute of
// the day, because the digits are tabular. So the seconds slot below clears
// it by 11 DIP at 23:59:59, and the time never has to move to make room.
//
// Baseline: DirectWrite centres the LINE BOX in the rect, and the baseline
// sits (ascent - line/2) = 0.4141 em below that box's middle. Two runs
// therefore share a baseline only if the smaller one's rect sits LOWER by
// 0.4141 * the size difference — that is all SecMid is. (Cross-check: the
// same constant predicts face_stack's measured "digits land ~7 DIP below the
// rect centre at 108pt" exactly.)
static const float TIME_SIZE = 54.0f;
static const float SEC_SIZE  = 20.0f;

// Right-aligned on x224 — the edge the date's tracked layout box already ends
// at, so the two share a margin instead of each floating on its own.
static const float SEC_X0 = 200.0f, SEC_X1 = 225.0f;

// Run geometry for (1.). Each cell is exactly one glyph advance, and DrawCells
// centres a glyph in its cell, so a cell-drawn run is positioned identically to
// the natural one — it is just painted in separable pieces.
// ---- options ----
// Changing the date format changes the STRING, which is the one thing
// SyncDate already watches, so the tracked layout rebuilds itself with no
// extra invalidation.
//
// These used to be sample dates ("MONDAY MARCH 1", "MON MAR 1"), which was the
// better label while the date was always English. It cannot be one now: the two
// forms are the user's own long and short date (LocDate), so the sample would be
// a different string in each of the twenty locales and wrong in nineteen of
// them. A description is what is left. Only labels are INI keys, so changing
// choice text orphans nothing — the saved value is the index.
static const WCHAR* const DATEFMT[] = { L"Long date", L"Short date", nullptr };

static int o_hour24 = 1;
static int o_accent = RGB(227, 89, 46);
static int o_text   = RGB(250, 250, 250);
static int o_date   = 1;
static int o_dfmt   = 0;
static int o_panel  = 1;

static const FaceOpt s_opts[] = {
    { L"24-hour clock", OPT_BOOL,   &o_hour24, nullptr },
    { L"Accent colour", OPT_COLOR,  &o_accent, nullptr },
    { L"Text colour",   OPT_COLOR,  &o_text,   nullptr },
    { L"Show date",     OPT_BOOL,   &o_date,   nullptr },
    { L"Date format",   OPT_CHOICE, &o_dfmt,   DATEFMT },
    { L"Show panel",    OPT_BOOL,   &o_panel,  nullptr },
};

static const float DIGIT_ADV = TIME_SIZE * 0.5752f;        // 31.06
static const float COLON_ADV = TIME_SIZE * 0.2710f;        // 14.63
static const float TIME_W    = 4 * DIGIT_ADV + COLON_ADV;  // 138.88
static const float TIME_X    = CX - TIME_W / 2;            // 50.56
static const float COLON_X   = TIME_X + 2 * DIGIT_ADV;     // 112.68

// The panel, sized to what is actually in it. DrawDial and DrawHands both
// derive from these, so the art and the text cannot disagree.
// Digit ink is not centred in its rect. DirectWrite centres the LINE BOX, the
// baseline lands 0.4141 em below that middle, and cap height is 0.7002 em with
// no descender to balance it — so the ink sits 0.0641 em low. With the date
// shown its band absorbs that slack and nothing looks off; with the date
// hidden the panel reads top-heavy, 24.5 DIP of air above the digits against
// 17.6 below. So only the date-off case lifts the digits by INK_DROP.
static const float INK_DROP = TIME_SIZE * (0.4141f - 0.7002f / 2);   // 3.46

static float PanelH()  { return TIME_BAND + (o_date ? DATE_BAND : 0); }   // one band or two
static float PanelY0() { return PMID - PanelH() / 2; }                    // panel top
static float TimeMid() { return PanelY0() + TIME_BAND / 2 - (o_date ? 0 : INK_DROP); }  // time rect centre
static float SecMid()  { return TimeMid() + 0.4141f * (TIME_SIZE - SEC_SIZE); }        // shares that baseline

// Symmetric about CX, because the time is centred there and toggling the
// second hand must not move a clock sideways — so the seconds widen the panel
// on both sides rather than pushing the time off centre.
//
// With the date shown the panel keeps its full width. The date is a tracked
// layout whose string changes daily, so a panel measured against it would
// breathe every midnight — worse than one that doesn't move at all.
static float PanelHalf()
{
    if (o_date)        return (PX1 - PX0) / 2;              // 112: the date sets it
    if (ShowSeconds()) return SEC_X1 + 7 - CX;              // 112 again: seconds reach x225
    return TIME_W / 2 + 8;                                  // 77.4 — just the time
}

class DigitalBoldFace : public IClockFace {
    mutable IDWriteFactory*    m_dw    = nullptr;    // lazy, cached forever
    mutable IDWriteTextFormat* m_fTime = nullptr;    // 54 bold
    mutable IDWriteTextFormat* m_fDate = nullptr;    // 11 bold, tracked
    mutable IDWriteTextFormat* m_fSec  = nullptr;    // 20 bold, on the baseline
    mutable IDWriteTextLayout* m_date  = nullptr;    // rebuilt once a day
    mutable WCHAR              m_dateStr[64] = {};   // what m_date holds

    // One Segoe UI Bold format at this size, centre-aligned — which DrawCells
    // requires, since it centres each glyph in its own cell.
    IDWriteTextFormat* MakeFormat(float size) const
    {
        return MakeTextFormat(m_dw, L"Segoe UI", size, DWRITE_FONT_WEIGHT_BOLD,
                              DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Rebuild the tracked date layout only when the text actually changed.
    void SyncDate(const WCHAR* s) const
    {
        if (m_date && lstrcmpW(s, m_dateStr) == 0) return;
        SafeRelease(m_date);
        lstrcpynW(m_dateStr, s, _countof(m_dateStr));
        m_date = MakeTrackedLayout(m_dw, m_dateStr, m_fDate,
                                   PX1 - PX0 - 16, 18.0f, 1.0f);
    }

public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Digital bold"; }

    // Static art: the panel, sized by PanelHalf/PanelH to what is on it — or
    // the bare hit-test wash when the panel is off.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        if (o_panel) {
            const float half = PanelHalf(), y0 = PanelY0();
            b->SetColor(D2D1::ColorF(0.09f, 0.09f, 0.10f, 0.25f));
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(CX - half, y0, CX + half, y0 + PanelH()),
                                  8.0f, 8.0f), b);
        } else {
            FillHitTestWash(rt, b);      // keep the square clickable
        }
        return true;
    }

    // The six knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }

    // The time as three separate runs (see 1.), the seconds on its baseline,
    // and the tracked date line. Builds the three formats on first call.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        if (!m_dw) {
            m_dw = SharedDWrite();
            if (!m_dw) return;
            m_fTime = MakeFormat(TIME_SIZE);
            m_fDate = MakeFormat(11.0f);
            m_fSec  = MakeFormat(SEC_SIZE);
        }
        if (!m_fTime || !m_fDate) return;

        rt->SetTransform(base);
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        WCHAR buf[64];
        // Derived from the panel, not written twice — SecMid() comes off it too.
        const float y0 = TimeMid() - 32, y1 = TimeMid() + 32;

        // Time in three runs — hour, colon, minute — so no pixel is ever
        // painted twice. See (1.) for why that matters.
        b->SetColor(FromRGB(o_text));
        wsprintfW(buf, L"%02d", o_hour24 ? st.wHour
                                         : (st.wHour % 12 ? st.wHour % 12 : 12));
        DrawCells(rt, b, m_fTime, buf, DIGIT_ADV, TIME_X, y0, y1);
        wsprintfW(buf, L"%02d", st.wMinute);
        DrawCells(rt, b, m_fTime, buf, DIGIT_ADV, COLON_X + COLON_ADV, y0, y1);

        b->SetColor(FromRGB(o_accent));
        DrawTextIn(rt, b, m_fTime, LocTimeSep(),
                   D2D1::RectF(COLON_X, y0, COLON_X + COLON_ADV, y1));

        // Seconds trail the time on its own baseline (see 3.). The time stays
        // dead-centre whether or not they are shown — toggling the second hand
        // must not shift a clock sideways.
        if (seconds && m_fSec) {
            b->SetColor(D2D1::ColorF(0.55f, 0.55f, 0.58f));
            wsprintfW(buf, L"%02d", st.wSecond);
            const float sm = SecMid();
            rt->DrawTextW(buf, 2, m_fSec,
                          D2D1::RectF(SEC_X0, sm - 13, SEC_X1, sm + 13), b);
        }

        if (!o_date) return;
        // The user's own date, field order included — this line is laid out by
        // MakeTrackedLayout, which measures, so it can absorb a width that a
        // composed "%s %s %d" in fixed slots could not.
        SyncDate(LocDate(st, !o_dfmt, buf, _countof(buf)));
        if (m_date) {
            b->SetColor(FromRGB(o_text));
            rt->DrawTextLayout(D2D1::Point2F(PX0 + 8, PanelY0() + TIME_BAND), m_date, b);
        }
    }
};

static const DigitalBoldFace s_digitalBold;
extern const IClockFace* const g_faceDigitalBold = &s_digitalBold;
