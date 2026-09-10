// face_split.cpp — "Split": the hour on a dark block and the minute on an
// accent block, butted together, over a tracked date on the wallpaper. No
// colon: the seam between the two blocks is the separator.
//
// Three things worth knowing before editing:
//
// 1. Each half is the SAME rounded rectangle drawn under a clip that keeps
//    only its side, so the outer corners are round and the inner ones square.
//    The obvious alternative — fill a rounded rect, then square the inner
//    corners by overdrawing a plain rect — does not work here: both blocks are
//    translucent, so an 8-DIP strip would get the fill twice and read as a
//    seam (0.86 alpha applied twice is 0.98, which is visible). A clip cannot
//    double anything.
//
// 2. The digits are Segoe UI Bold, whose digits are tabular (measured: every
//    one advances 0.5752 em), so each pair is centred in its own block with a
//    single DrawTextW and cannot slide. No DrawCells needed — that is only for
//    the proportional-digit families (Segoe UI Light, Bahnschrift).
//
// 3. Vertical placement is by INK, not by rect. DirectWrite centres the line
//    box, and Segoe UI's ink centre lands 0.0640 em BELOW that box's middle
//    (ascent 1.0791, line 1.3301, cap 0.7002 — all measured on this machine).
//    At 62 DIP that is 4 DIP, so the digit rect is raised 4 DIP off the
//    block's centre to put the ink on it. Same arithmetic as face_stack's
//    measured "~7 DIP at 108pt" note.
//
// Text is drawn per frame, so grayscale AA must be set here (the engine only
// sets it on the dial target), and the formats are cached for the process
// lifetime like every other text face.
//
// DAY_FULL / MONTH_FULL come from faces.cpp — shared with Banner and Digital bold.

#include "faces.h"

// Blocks. SY0 matches Digital bold's panel top so the two faces sit on the
// same line when you flip between them.
static const float SX0 = 8.0f,  SX1 = DIAL - 8.0f;
static const float SY0 = 58.0f, SY1 = 162.0f;
static const float SPLIT = CX;                  // the seam, dead centre
static const float RAD   = 8.0f;

// Digit rect, raised off the block centre by 0.0640 em so the INK centres —
// see (3). Half-height 52 keeps the 62pt line box comfortably inside.
static const float BIG_SIZE = 62.0f;
static const float BIG_MID  = (SY0 + SY1) / 2 - 0.0640f * BIG_SIZE;

// Seconds ride the accent block's top-right corner; the date sits below both
// blocks, on the wallpaper.
static const float SEC_SIZE = 12.0f;
static const float DATE_Y   = 172.0f;
static const float DATE_H   = 14.0f;

// Same hard-offset shadow recipe as face_banner / face_stack: the date has no
// panel behind it, and a real blur would need a D2D effect graph.
static const float SHADOW_DY = 1.5f;
static const float SHADOW_A  = 0.5f;

// ---- options ----
// Both block colours live in DrawDial, so they land in the cached dial —
// RefreshFace() rebuilds it, which is why they need nothing else.
static int o_hourBlock = RGB(18, 19, 23);
static int o_minBlock  = RGB(227, 89, 46);
static int o_digit     = RGB(250, 250, 250);
static int o_hour24    = 1;
static int o_date      = 1;

static const FaceOpt s_opts[] = {
    { L"Hour block colour",   OPT_COLOR, &o_hourBlock, nullptr },
    { L"Minute block colour", OPT_COLOR, &o_minBlock,  nullptr },
    { L"Digit colour",        OPT_COLOR, &o_digit,     nullptr },
    { L"24-hour clock",       OPT_BOOL,  &o_hour24,    nullptr },
    { L"Show date",           OPT_BOOL,  &o_date,      nullptr },
};

class SplitFace : public IClockFace {
    mutable IDWriteFactory*    m_dw    = nullptr;   // lazy, cached forever
    mutable IDWriteTextFormat* m_fBig  = nullptr;   // 62 bold — the digits
    mutable IDWriteTextFormat* m_fSec  = nullptr;   // 12 bold — seconds
    mutable IDWriteTextFormat* m_fDate = nullptr;   // 10 bold, tracked
    mutable IDWriteTextLayout* m_date  = nullptr;   // rebuilt once a day
    mutable WCHAR              m_dateStr[48] = {};  // what m_date holds

    // One Segoe UI Bold format at this size, centre-aligned — each digit pair
    // is centred in its own block.
    IDWriteTextFormat* MakeFormat(float size) const
    {
        return MakeTextFormat(m_dw, L"Segoe UI", size, DWRITE_FONT_WEIGHT_BOLD,
                              DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Half of one rounded rect — see (1).
    void Half(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
              float x0, float x1, const D2D1_COLOR_F& col) const
    {
        rt->PushAxisAlignedClip(D2D1::RectF(x0, SY0, x1, SY1),
                                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        b->SetColor(col);
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(SX0, SY0, SX1, SY1), RAD, RAD), b);
        rt->PopAxisAlignedClip();
    }

    // Rebuild the tracked date layout only when the text actually changed,
    // i.e. once a day — a layout is an allocation.
    void SyncDate(const WCHAR* s) const
    {
        if (m_date && lstrcmpW(s, m_dateStr) == 0) return;
        SafeRelease(m_date);
        lstrcpynW(m_dateStr, s, _countof(m_dateStr));
        // MakeTrackedLayout puts `track` on BOTH sides of every glyph, so the
        // run gets twice what is passed — 1.2 here gives the 2.4 DIP wanted.
        m_date = MakeTrackedLayout(m_dw, m_dateStr, m_fDate, DIAL, DATE_H, 1.2f);
    }

public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Split"; }

    // Static art: the hit-test wash and the two blocks.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        // The date sits outside the blocks, so without the wash only the
        // blocks themselves would be draggable.
        FillHitTestWash(rt, b);

        Half(rt, b, SX0,   SPLIT, FromRGB(o_hourBlock, 0.58f));
        Half(rt, b, SPLIT, SX1,   FromRGB(o_minBlock,  0.86f));
        return true;
    }

    // The five knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }

    // One digit pair per block, the seconds in the accent block's corner, and
    // the tracked date under both. Builds the three formats on first call.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        if (!m_dw) {
            m_dw = SharedDWrite();
            if (!m_dw) return;
            m_fBig  = MakeFormat(BIG_SIZE);
            m_fSec  = MakeFormat(SEC_SIZE);
            m_fDate = MakeFormat(10.0f);
        }
        if (!m_fBig || !m_fDate) return;

        rt->SetTransform(base);
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        WCHAR buf[64];      // wide enough for a formatted date, not just "%02d"
        const float y0 = BIG_MID - 52, y1 = BIG_MID + 52;

        // One pair per block. The minute keeps a touch of warmth so it doesn't
        // read as a colder white than the hour beside it.
        b->SetColor(FromRGB(o_digit));
        wsprintfW(buf, L"%02d", o_hour24 ? st.wHour
                                         : (st.wHour % 12 ? st.wHour % 12 : 12));
        DrawTextIn(rt, b, m_fBig, buf, D2D1::RectF(SX0, y0, SPLIT, y1));

        D2D1_COLOR_F warm = FromRGB(o_digit);
        warm.b *= 0.968f;                               // just off-white, not a tint
        warm.g *= 0.985f;
        b->SetColor(warm);
        wsprintfW(buf, L"%02d", st.wMinute);
        DrawTextIn(rt, b, m_fBig, buf, D2D1::RectF(SPLIT, y0, SX1, y1));

        if (seconds && m_fSec) {
            b->SetColor(FromRGB(o_digit, 0.78f));
            wsprintfW(buf, L"%02d", st.wSecond);
            DrawTextIn(rt, b, m_fSec, buf, D2D1::RectF(205, 64, 225, 83));
        }

        if (!o_date) return;
        // Long form: this face has always spelled its date out, and it has the
        // full width of the scene to do it in.
        SyncDate(LocDate(st, true, buf, _countof(buf)));
        if (m_date) {
            b->SetColor(D2D1::ColorF(0, SHADOW_A));
            rt->DrawTextLayout(D2D1::Point2F(0, DATE_Y + SHADOW_DY), m_date, b);
            b->SetColor(FromRGB(o_digit, 0.94f));
            rt->DrawTextLayout(D2D1::Point2F(0, DATE_Y), m_date, b);
        }
    }
};

static const SplitFace s_split;
extern const IClockFace* const g_faceSplit = &s_split;
