// face_stack.cpp — "Stack": 24-hour time as two lines of very large Segoe UI
// Light, flush left, hour bright over a dimmed minute. No panel at all.
//
// Four things this face does that the other text faces don't:
//
// 1. No panel means no backing, and with nothing behind it the type has to
//    survive whatever wallpaper is under it. Every string is drawn twice:
//    black at SHADOW_DY and SHADOW_A first, then the ink. A hard offset
//    instead of a blur — D2D would need an effect graph for a real shadow,
//    and at this size the hard edge reads as a shadow anyway.
//
// 2. A face with no static art still needs a dial, because ULW pixels with
//    alpha 0 are click-through: a fully transparent dial would leave only the
//    glyphs draggable. DrawDial hands that to FillHitTestWash (faces.cpp),
//    which paints the square at the one alpha step above nothing.
//
// 3. The date line is tracked, which DrawText can't do — MakeTrackedLayout
//    allocates, so the layout is rebuilt only when the string changes, i.e.
//    once a day.
//
// 4. Segoe UI Light's digits are PROPORTIONAL — measured, "1" is 730 design
//    units against 1055 for the rest, 0.159 em narrower. At 108 DIP that is
//    17 DIP: drawn as one string, the second digit of a pair would jump that
//    far sideways every time the first crossed into or out of a 1. So the
//    digits go through DrawCells (faces.cpp), one glyph per fixed cell. The
//    cell is Light's COMMON advance, 0.515 em, so a pair is always exactly as
//    wide as two round digits — which is what the seconds' x below is measured
//    against — and it clears the 0.4883 em widest ink.

#include "faces.h"

// Rect midpoints, not glyph midpoints: DirectWrite centers the LINE BOX, and
// Segoe UI's baseline sits below that box's middle, so the digits land ~7 DIP
// lower than the rect center. These numbers are the rendered result, measured
// off a screenshot — moving them by eye in the abstract will get it wrong.
static const float LX = 18.0f;                  // left margin for everything
static const float HOUR_MID = 52.0f;            // 92 DIP apart
static const float MIN_MID  = 144.0f;

// One shadow recipe for every string on this face — see (1).
static const float SHADOW_DY = 1.5f;
static const float SHADOW_A  = 0.45f;

// Digit cells — see (4). 0.515 em is Segoe UI Light's common digit advance.
static const float BIG_SIZE = 108.0f;
static const float SEC_SIZE = 24.0f;
static const float BIG_CELL = BIG_SIZE * 0.515f;
static const float SEC_CELL = SEC_SIZE * 0.515f;

// ---- options ----
// The minute's dimming is the face's whole idea, so it is a choice rather
// than being folded into the ink colour — alpha is not something the colour
// picker can express.
static const WCHAR* const DIMMING[] = { L"Match hour", L"Dim", L"Faint", nullptr };
static const float DIM_A[] = { 1.0f, 0.50f, 0.25f };
// ClampOpt can only count the string list, so a fourth name without a fourth
// alpha would clamp clean and then read past DIM_A. +1 for the terminator.
static_assert(_countof(DIMMING) == _countof(DIM_A) + 1, "DIMMING/DIM_A disagree");

static int o_hour24 = 1;
static int o_ink    = RGB(255, 255, 255);
static int o_accent = RGB(227, 89, 46);
static int o_dim    = 1;
static int o_date   = 1;

static const FaceOpt s_opts[] = {
    { L"24-hour clock",  OPT_BOOL,   &o_hour24, nullptr },
    { L"Ink colour",     OPT_COLOR,  &o_ink,    nullptr },
    { L"Accent colour",  OPT_COLOR,  &o_accent, nullptr },
    { L"Minute dimming", OPT_CHOICE, &o_dim,    DIMMING },
    { L"Show date",      OPT_BOOL,   &o_date,   nullptr },
};

class StackFace : public IClockFace {
    mutable IDWriteFactory*    m_dw    = nullptr;   // lazy, cached forever
    mutable IDWriteTextFormat* m_fBig  = nullptr;   // 108 light — the digits
    mutable IDWriteTextFormat* m_fSec  = nullptr;   // 24 light  — seconds
    mutable IDWriteTextFormat* m_fDate = nullptr;   // 10        — tracked date
    mutable IDWriteTextLayout* m_date  = nullptr;   // rebuilt once a day
    mutable WCHAR              m_dateStr[32] = {};  // what m_date holds

    // Digit formats must be CENTER: DrawCells centres each glyph in its cell.
    // The date stays LEADING — it is a tracked layout drawn from a point.
    IDWriteTextFormat* MakeFormat(float size, DWRITE_TEXT_ALIGNMENT align) const
    {
        return MakeTextFormat(m_dw, L"Segoe UI", size, DWRITE_FONT_WEIGHT_LIGHT,
                              align);
    }

    // Shadow pass then ink pass, same cells offset down. See (1) and (4).
    void Put(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
             IDWriteTextFormat* f, const WCHAR* s, float cell, float x,
             float y0, float y1, const D2D1_COLOR_F& ink) const
    {
        b->SetColor(D2D1::ColorF(0, SHADOW_A));
        DrawCells(rt, b, f, s, cell, x, y0 + SHADOW_DY, y1 + SHADOW_DY);
        b->SetColor(ink);
        DrawCells(rt, b, f, s, cell, x, y0, y1);
    }

    // Rebuild the tracked date layout only when the text actually changed,
    // i.e. once a day — see (3).
    void SyncDate(const WCHAR* s) const
    {
        if (m_date && lstrcmpW(s, m_dateStr) == 0) return;
        SafeRelease(m_date);
        lstrcpynW(m_dateStr, s, _countof(m_dateStr));
        m_date = MakeTrackedLayout(m_dw, m_dateStr, m_fDate, 200.0f, 14.0f, 1.5f);
    }

public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Stack"; }

    // No static art — just the invisible hit-test wash. See (2).
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

    // Hour over dimmed minute, the seconds beside them, the accent rule and
    // the tracked date. Builds the three formats on first call.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        if (!m_dw) {
            m_dw = SharedDWrite();
            if (!m_dw) return;
            m_fBig  = MakeFormat(BIG_SIZE, DWRITE_TEXT_ALIGNMENT_CENTER);
            m_fSec  = MakeFormat(SEC_SIZE, DWRITE_TEXT_ALIGNMENT_CENTER);
            m_fDate = MakeFormat(10.0f,    DWRITE_TEXT_ALIGNMENT_LEADING);
        }
        if (!m_fBig || !m_fDate) return;

        rt->SetTransform(base);
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        WCHAR buf[64];      // wide enough for a formatted date, not just "%02d"
        // Paragraph-centered in an 80-DIP box, so the rect's midpoint is where
        // the digits land regardless of the 108pt line box overflowing it.
        wsprintfW(buf, L"%02d", o_hour24 ? st.wHour
                                         : (st.wHour % 12 ? st.wHour % 12 : 12));
        Put(rt, b, m_fBig, buf, BIG_CELL, LX, HOUR_MID - 40, HOUR_MID + 40,
            FromRGB(o_ink));

        // Minute dimmed: on a layered window that is a genuinely part-
        // transparent pixel, so it reads as the wallpaper showing through.
        wsprintfW(buf, L"%02d", st.wMinute);
        Put(rt, b, m_fBig, buf, BIG_CELL, LX, MIN_MID - 40, MIN_MID + 40,
            FromRGB(o_ink, DIM_A[o_dim]));

        // Clear of the minute's last digit and sharing its baseline. Fixed
        // cells make every minute the same width, so this x sits the same
        // distance off the digits whatever the minute reads.
        if (seconds) {
            wsprintfW(buf, L"%02d", st.wSecond);
            Put(rt, b, m_fSec, buf, SEC_CELL, 122, 159, 199, FromRGB(o_accent));
        }

        if (!o_date) return;
        b->SetColor(FromRGB(o_accent));
        rt->FillRectangle(D2D1::RectF(LX + 2, 202, LX + 50, 203.5f), b);

        // Short form: this face gives its date one narrow line under the rule,
        // and the tracked layout measures it. The abbreviated tables would keep
        // it shorter still, but only LocDate carries the locale's field order.
        SyncDate(LocDate(st, false, buf, _countof(buf)));
        if (m_date) {
            b->SetColor(D2D1::ColorF(0, SHADOW_A));
            rt->DrawTextLayout(D2D1::Point2F(LX + 2, 210.0f + SHADOW_DY), m_date, b);
            b->SetColor(FromRGB(o_ink, 0.74f));
            rt->DrawTextLayout(D2D1::Point2F(LX + 2, 210.0f), m_date, b);
        }
    }
};

static const StackFace s_stack;
extern const IClockFace* const g_faceStack = &s_stack;
