// face_digital.cpp — "Digital": Win7-sidebar style bar, [04] [DEC/MON]
// [03:12] [PM], on a dark translucent rounded rect so the text stays legible
// over any wallpaper.
//
// Unlike the analog faces, this one draws TEXT every frame, so the engine's
// "nothing per-frame touches DirectWrite" property does not hold here. Cost is
// held down the same way face_simple caches its geometries: the DWrite factory
// and the three text formats are built once on first draw and reused for the
// process lifetime (never freed — a handful of device-independent objects, the
// OS reclaims them at exit).
//
// The bar is drawn into the engine's square 240x240 scene, so it is a 48-tall
// strip with transparent space above and below. The window stays square and
// therefore larger than the clock — only noticeable when dragging (the whole
// square is a drag handle) or in resize mode (the corner overlays sit away from
// the bar). Matching the window to the bar would need a per-face aspect ratio in
// the engine: CreateResources, BuildDial, DrawFrame, the ULW blit and the
// grip/tick hit-tests all assume width == height.

#include "faces.h"

// Segoe UI Light's digits are proportional — measured, "1" is 730 design
// units against 1055 for every other digit. A centred "%02d:%02d" therefore
// slides sideways whenever a 1 enters or leaves the string. The time goes
// through DrawCells instead: 0.515 em is Light's common digit advance, so the
// run is exactly as wide as it has always been for round digits, and it
// clears the 0.4883 em widest ink with room to spare. (m_fLbl is Regular,
// whose digits ARE tabular, so the seconds field needs none of this.)
static const float TIME_SIZE  = 34.0f;
static const float TIME_CELL  = TIME_SIZE * 0.515f;
static const float TIME_COLON = TIME_SIZE * 0.24f;

// Bar geometry, centered in the 240-DIP scene. Three blocks of fixed width:
// whatever is hidden gives its width back and the rest re-centres, so the bar
// is never wider than what is in it. With everything shown the arithmetic
// lands on 8..232, the face's full-width bar.
static const float DATE_W = 88.0f;      // day number + month/weekday column
static const float TIME_W = 98.0f;      // slot the centred time run sits in
static const float SIDE_W = 38.0f;      // AM/PM, seconds, or both stacked
static const float BY0  = 96.0f;
static const float BY1  = 144.0f;
static const float BMID = (BY0 + BY1) / 2;

// Names come from MONTH_ABBR / DAY_ABBR, which loc.cpp fills from the user's
// regional settings — so they arrive translated with nothing here to change.
//
// This face deliberately does NOT use LocDate(): the three slots above are
// fixed widths, and a formatted date carries its own field order and length,
// which is exactly what they have no room to absorb. Abbreviated names in two
// stacked slots is the layout, and it survives translation because the OS's
// abbreviations are short in every locale by construction. The faces that show
// a date as one measured line — Digital bold, Stack, Banner, Split — do use it.

// ---- options ----
// Font size and weight stay off the menu: the three formats below are built
// once and never rebuilt, and the fixed-slot layout is measured against them.
static int o_hour24 = 0;                    // 12-hour, with the AM/PM slot
static int o_ampm   = 1;
static int o_date   = 1;
static int o_panel  = 1;
static int o_text   = RGB(247, 247, 250);
static int o_muted  = RGB(158, 184, 217);

static const FaceOpt s_opts[] = {
    { L"24-hour clock", OPT_BOOL,  &o_hour24, nullptr },
    { L"Show AM/PM",    OPT_BOOL,  &o_ampm,   nullptr },
    { L"Show date",     OPT_BOOL,  &o_date,   nullptr },
    { L"Show panel",    OPT_BOOL,  &o_panel,  nullptr },
    { L"Text colour",   OPT_COLOR, &o_text,   nullptr },
    // Colours every label on the bar: the month, weekday, AM/PM and seconds.
    { L"Label colour",  OPT_COLOR, &o_muted,  nullptr },
};

// Worked out in one place because two callers must agree on it: DrawDial
// draws the panel, DrawHands fills it. The right-hand slot holds AM/PM *or*
// the seconds *or* both stacked, so it survives hiding AM/PM whenever the
// second hand is on — otherwise the seconds would land outside the panel.
struct Bar { float x0, x1, date, time, side; bool showDate, showSide; };

// Place the three blocks for the options currently set.
static Bar LayOut()
{
    Bar r;
    r.showDate = o_date != 0;
    r.showSide = o_ampm != 0 || ShowSeconds();
    const float w = (r.showDate ? DATE_W : 0) + TIME_W + (r.showSide ? SIDE_W : 0);
    r.x0   = CX - w / 2;
    r.date = r.x0;
    r.time = r.x0 + (r.showDate ? DATE_W : 0);
    r.side = r.time + TIME_W;
    r.x1   = r.x0 + w;
    return r;
}

class DigitalFace : public IClockFace {
    mutable IDWriteFactory*    m_dw    = nullptr;   // lazy, cached forever
    mutable IDWriteTextFormat* m_fTime = nullptr;   // 34 light — "03:12"
    mutable IDWriteTextFormat* m_fDay  = nullptr;   // 30 light — "04"
    mutable IDWriteTextFormat* m_fLbl  = nullptr;   // 11.5 — DEC / MON / PM / SS

    // One Segoe UI format at this size and weight, centre-aligned.
    IDWriteTextFormat* MakeFormat(float size, DWRITE_FONT_WEIGHT weight) const
    {
        return MakeTextFormat(m_dw, L"Segoe UI", size, weight,
                              DWRITE_TEXT_ALIGNMENT_CENTER);
    }

public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Digital"; }

    // Static part only: the translucent bar. Everything time-dependent is text
    // and therefore lives in DrawHands.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        if (o_panel) {
            const Bar bar = LayOut();
            b->SetColor(D2D1::ColorF(0.05f, 0.07f, 0.12f, 0.35f));
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(bar.x0, BY0, bar.x1, BY1), 8.0f, 8.0f), b);
        } else {
            // Without the panel only the glyphs would hit-test.
            FillHitTestWash(rt, b);
        }
        return true;
    }

    // The six knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }

    // Everything time-dependent: the date block, the time, and whatever the
    // right-hand slot is carrying. Builds the three formats on first call.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        if (!m_dw) {                                  // one-time format build
            m_dw = SharedDWrite();
            if (!m_dw) return;
            m_fTime = MakeFormat(TIME_SIZE, DWRITE_FONT_WEIGHT_LIGHT);
            m_fDay  = MakeFormat(30.0f, DWRITE_FONT_WEIGHT_LIGHT);
            m_fLbl  = MakeFormat(11.5f, DWRITE_FONT_WEIGHT_NORMAL);
        }

        rt->SetTransform(base);
        // ClearType needs an opaque background this layered window doesn't
        // have; the engine only sets this on the dial target, so set it here
        // too or the per-frame text renders with colour fringing.
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        WCHAR buf[16];
        const Bar bar = LayOut();
        const D2D1_COLOR_F bright = FromRGB(o_text);
        const D2D1_COLOR_F muted  = FromRGB(o_muted);

        if (bar.showDate) {
            // day number
            b->SetColor(bright);
            wsprintfW(buf, L"%02d", st.wDay);
            DrawTextIn(rt, b, m_fDay, buf,
                D2D1::RectF(bar.date + 2, BMID - 20, bar.date + 44, BMID + 20));

            // month over weekday
            b->SetColor(muted);
            DrawTextIn(rt, b, m_fLbl, MONTH_ABBR[(st.wMonth - 1) % 12],
                D2D1::RectF(bar.date + 46, BY0 + 6,  bar.date + 86, BY0 + 22));
            DrawTextIn(rt, b, m_fLbl, DAY_ABBR[st.wDayOfWeek % 7],
                D2D1::RectF(bar.date + 46, BY0 + 24, bar.date + 86, BY0 + 40));
        }

        // One glyph per fixed cell so it cannot slide (see above), the whole
        // run centred in the slot the layout gave it — never a literal x, since
        // the blocks either side may be hidden.
        int h = o_hour24 ? st.wHour : (st.wHour % 12 ? st.wHour % 12 : 12);
        b->SetColor(bright);
        const float runW = 4 * TIME_CELL + TIME_COLON;
        float tx = bar.time + (TIME_W - runW) / 2;
        wsprintfW(buf, L"%02d", h);
        DrawCells(rt, b, m_fTime, buf, TIME_CELL, tx, BMID - 22, BMID + 22);
        tx += 2 * TIME_CELL;
        DrawTextIn(rt, b, m_fTime, LocTimeSep(),
                   D2D1::RectF(tx, BMID - 22, tx + TIME_COLON, BMID + 22));
        tx += TIME_COLON;
        wsprintfW(buf, L"%02d", st.wMinute);
        DrawCells(rt, b, m_fTime, buf, TIME_CELL, tx, BMID - 22, BMID + 22);

        // Right slot: AM/PM alone, or stacked over the seconds when the second
        // hand is on — stacking reuses the date slot's idiom and costs no width,
        // which appending ":SS" to the time would not have.
        b->SetColor(muted);
        // The locale's own designators — "AM"/"PM", 午前/午後, ص/م. A 24-hour
        // locale has none and answers "", which draws nothing; the option above
        // is what decides whether the slot appears at all.
        const WCHAR* ampm = LocAmPm(st.wHour >= 12);
        wsprintfW(buf, L"%02d", st.wSecond);
        const float sx0 = bar.side, sx1 = bar.side + SIDE_W;
        if (o_ampm && seconds) {
            DrawTextIn(rt, b, m_fLbl, ampm, D2D1::RectF(sx0, BY0 + 6,  sx1, BY0 + 22));
            DrawTextIn(rt, b, m_fLbl, buf,  D2D1::RectF(sx0, BY0 + 24, sx1, BY0 + 40));
        } else if (o_ampm) {
            DrawTextIn(rt, b, m_fLbl, ampm, D2D1::RectF(sx0, BMID - 12, sx1, BMID + 12));
        } else if (seconds) {
            DrawTextIn(rt, b, m_fLbl, buf,  D2D1::RectF(sx0, BMID - 12, sx1, BMID + 12));
        }
    }
};

static const DigitalFace s_digital;
extern const IClockFace* const g_faceDigital = &s_digital;
