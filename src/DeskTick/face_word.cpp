// face_word.cpp — "Word clock": the time as a sentence, lit out of an 11x10 letter
// grid ("IT IS TWENTY TO TEN"). Four corner dots carry the 1-4 minutes the
// phrase can't say.
//
// The one face that uses the whole 240 square instead of letterboxing a bar
// into it — which also makes it the cheapest text face here, because the
// split falls exactly along the engine's cached-dial line:
//
//   DrawDial  — the panel and all 110 letters at 12% white. Once per size.
//   DrawHands — only the ~20 letters that are lit, painted over the dim ones.
//
// So a tick costs ~20 DrawTextW calls into a 17x19 DIP box each, not 110.
// The dim grid never gets redrawn and never changes.

#include "faces.h"
#include <dwrite.h>

static const float PX0 = 8.0f, PX1 = DIAL - 8.0f;        // panel
// Grid origin: the 190x186 block centred in the panel's 224 square, so the
// margins match the corner dots' own symmetry (17 either side, 19 top and
// bottom). Taken off the mock's absolute coords it lands 4 DIP up and left.
static const float GX  = 25.0f, GY = 27.0f;              // grid origin
static const int   ROWS = 10, COLS = 11;
static const float CW  = 190.0f / COLS, CH = 186.0f / ROWS;   // cell size

static const WCHAR* const GRID[ROWS] = {
    L"ITLISASAMPM",
    L"ACQUARTERDC",
    L"TWENTYFIVEX",
    L"HALFSTENFTO",
    L"PASTERUNINE",
    L"ONESIXTHREE",
    L"FOURFIVETWO",
    L"EIGHTELEVEN",
    L"SEVENTWELVE",
    L"TENSEOCLOCK",
};

// A run of letters to light: row, first column, length. len 0 = nothing.
struct Run { int row, col, len; };

// Indexed by minute/5. hourAdd rolls the hour forward for the "TO" half, so
// 9:41 reads TWENTY TO TEN, not TWENTY TO NINE.
static const struct { Run a, b, conn; int hourAdd; } PHRASE[12] = {
    { {0,0,0}, {0,0,0}, {9,5,6}, 0 },   // :00  o'clock
    { {2,6,4}, {0,0,0}, {4,0,4}, 0 },   // :05  five past
    { {3,5,3}, {0,0,0}, {4,0,4}, 0 },   // :10  ten past
    { {1,2,7}, {0,0,0}, {4,0,4}, 0 },   // :15  quarter past
    { {2,0,6}, {0,0,0}, {4,0,4}, 0 },   // :20  twenty past
    { {2,0,6}, {2,6,4}, {4,0,4}, 0 },   // :25  twenty five past
    { {3,0,4}, {0,0,0}, {4,0,4}, 0 },   // :30  half past
    { {2,0,6}, {2,6,4}, {3,9,2}, 1 },   // :35  twenty five to
    { {2,0,6}, {0,0,0}, {3,9,2}, 1 },   // :40  twenty to
    { {1,2,7}, {0,0,0}, {3,9,2}, 1 },   // :45  quarter to
    { {3,5,3}, {0,0,0}, {3,9,2}, 1 },   // :50  ten to
    { {2,6,4}, {0,0,0}, {3,9,2}, 1 },   // :55  five to
};

static const Run HOURS[12] = {          // index 0 = twelve (hour 0 and 12)
    {8,5,6}, {5,0,3}, {6,8,3}, {5,6,5}, {6,0,4}, {6,4,4},
    {5,3,3}, {8,0,5}, {7,0,5}, {4,7,4}, {9,0,3}, {7,5,6},
};

// Corner dots, clockwise from top-left: +1, +2, +3, +4 minutes.
static const D2D1_POINT_2F DOTS[4] = {
    { 15.5f, 15.5f }, { 224.5f, 15.5f }, { 224.5f, 224.5f }, { 15.5f, 224.5f }
};

// Mark one run's letters lit, as a bit per column in that row's mask.
static void Light(unsigned* lit, const Run& r)
{
    for (int i = 0; i < r.len; i++) lit[r.row] |= 1u << (r.col + i);
}

// ---- options ----
// The dim letter grid is baked into the cached dial, so changing any of
// these needs the dial rebuild RefreshFace() already does.
static int o_lit    = RGB(244, 245, 247);
static int o_accent = RGB(227, 89, 46);
static int o_panel  = RGB(10, 13, 18);
static int o_dots   = 1;

static const FaceOpt s_opts[] = {
    { L"Lit letter colour", OPT_COLOR, &o_lit,    nullptr },
    { L"Accent colour",     OPT_COLOR, &o_accent, nullptr },
    { L"Panel colour",      OPT_COLOR, &o_panel,  nullptr },
    { L"Show minute dots",  OPT_BOOL,  &o_dots,   nullptr },
};

class WordFace : public IClockFace {
    mutable IDWriteFactory*    m_dw  = nullptr;   // lazy, cached forever
    mutable IDWriteTextFormat* m_fmt = nullptr;   // 12.5 semibold, cell-centered

    // Build the one text format on first use. False means no letters can be
    // drawn at all, which for this face means it cannot show the time.
    bool EnsureDW() const
    {
        if (m_dw) return m_fmt != nullptr;
        m_dw = SharedDWrite();
        if (!m_dw) return false;
        m_fmt = MakeTextFormat(m_dw, L"Segoe UI", 12.5f,
                               DWRITE_FONT_WEIGHT_SEMI_BOLD,
                               DWRITE_TEXT_ALIGNMENT_CENTER);
        return m_fmt != nullptr;
    }

    // One glyph, centered in its cell. Draws straight out of GRID — no copy.
    void Cell(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, int row, int col) const
    {
        float x = GX + (float)col * CW, y = GY + (float)row * CH;
        rt->DrawTextW(&GRID[row][col], 1, m_fmt,
                      D2D1::RectF(x, y, x + CW, y + CH), b);
    }

public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Word clock"; }

    // The whole dim grid bakes in here — 110 glyphs drawn once per size.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        b->SetColor(FromRGB(o_panel, 0.44f));
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(PX0, PX0, PX1, PX1), 14.0f, 14.0f), b);

        // Unlike Digital, whose dial genuinely is just a panel, this face is
        // nothing but letters — without DirectWrite it can never show the
        // time, so hand the engine its fallback rather than an empty box.
        if (!EnsureDW()) return false;

        b->SetColor(FromRGB(o_lit, 0.12f));
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++)
                Cell(rt, b, r, c);
        if (o_dots)
            for (int i = 0; i < 4; i++)
                rt->FillEllipse(D2D1::Ellipse(DOTS[i], 2.3f, 2.3f), b);
        return true;
    }

    // The four knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }

    // Work out which letters the phrase lights, repaint just those over the
    // dim grid, then the minute dots and the seconds rule.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        if (!EnsureDW()) return;

        rt->SetTransform(base);
        // Grayscale AA: the engine only sets this on the dial target.
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        unsigned lit[ROWS] = {};
        Light(lit, Run{ 0, 0, 2 });                     // IT
        Light(lit, Run{ 0, 3, 2 });                     // IS
        Light(lit, st.wHour < 12 ? Run{ 0, 7, 2 }       // AM (actual time, not
                                 : Run{ 0, 9, 2 });     // PM  the rolled hour)

        int m5 = st.wMinute / 5;
        Light(lit, PHRASE[m5].a);
        Light(lit, PHRASE[m5].b);
        Light(lit, PHRASE[m5].conn);
        Light(lit, HOURS[(st.wHour + PHRASE[m5].hourAdd) % 12]);

        b->SetColor(FromRGB(o_lit));
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++)
                if (lit[r] & (1u << c)) Cell(rt, b, r, c);

        // The phrase is only accurate to 5 minutes; the corner dots carry the
        // rest, one per minute past it.
        if (o_dots) {
            b->SetColor(FromRGB(o_accent));
            for (int i = 0; i < st.wMinute % 5; i++)
                rt->FillEllipse(D2D1::Ellipse(DOTS[i], 2.3f, 2.3f), b);
        }

        // Seconds have nowhere to go in words: a hairline under the grid.
        if (seconds) {
            b->SetColor(FromRGB(o_accent, 0.55f));
            rt->FillRectangle(
                D2D1::RectF(GX, 218.0f, GX + 190.0f * (float)st.wSecond / 60.0f,
                            219.5f), b);
        }
    }
};

static const WordFace s_word;
extern const IClockFace* const g_faceWord = &s_word;
