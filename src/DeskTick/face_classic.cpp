// face_classic.cpp — the classic procedural face: rim, 60 ticks, DirectWrite
// numerals. Same geometry for dark and light, just an inverted palette.

#include "faces.h"
#include <dwrite.h>
#include <math.h>

#pragma comment(lib, "dwrite")

// ---- options ----
// Dark and Light are two instances of one class, so each needs its own value
// array and its own table; the table is a ctor argument. Every colour the
// face draws lives here — the class itself holds no palette.
static const WCHAR* const NUMERALS[] = { L"All 12", L"12, 3, 6, 9", L"None", nullptr };
static const WCHAR* const TICKS[]    = { L"Minute", L"Hour", L"None", nullptr };

struct ClassicOpts {
    int face, mark, hand, accent, nums, ticks;
};

static ClassicOpts o_dark  = { RGB(20, 23, 28),    RGB(217, 219, 230),
                               RGB(235, 235, 242), RGB(242, 89, 77), 0, 0 };
static ClassicOpts o_light = { RGB(245, 245, 250), RGB(38, 41, 48),
                               RGB(31, 33, 38),    RGB(242, 89, 77), 0, 0 };

#define CLASSIC_TABLE(v) {                                        \
    { L"Face colour",     OPT_COLOR,  &(v).face,   nullptr  },    \
    { L"Marking colour",  OPT_COLOR,  &(v).mark,   nullptr  },    \
    { L"Hand colour",     OPT_COLOR,  &(v).hand,   nullptr  },    \
    { L"Accent colour",   OPT_COLOR,  &(v).accent, nullptr  },    \
    { L"Numerals",        OPT_CHOICE, &(v).nums,   NUMERALS },    \
    { L"Tick marks",      OPT_CHOICE, &(v).ticks,  TICKS    },    \
}

static const FaceOpt s_darkOpts[]  = CLASSIC_TABLE(o_dark);
static const FaceOpt s_lightOpts[] = CLASSIC_TABLE(o_light);

class ClassicFace : public IClockFace {
    const WCHAR*       m_name;
    const ClassicOpts& m_o;
    const FaceOpt*     m_opts;
    int                m_nOpts;
    float              m_faceAlpha;    // dark and light differ here only
public:
    // One instance per palette: `o` and `opts` are that instance's value
    // struct and table, `alpha` how opaque its face fill is.
    //
    // The count is a ctor argument rather than a literal because the table
    // reaches this class as a bare pointer, where _countof cannot see it: the
    // only place the array's real length is still known is the construction
    // site, so that is where it has to be read.
    ClassicFace(const WCHAR* name, const ClassicOpts& o,
                const FaceOpt* opts, int nOpts, float alpha)
        : m_name(name), m_o(o), m_opts(opts), m_nOpts(nOpts), m_faceAlpha(alpha) {}

    // Menu label, and the INI section this instance's options are saved under.
    const WCHAR* GetName() const override { return m_name; }

    // This instance's knobs, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = m_opts;
        return m_nOpts;
    }

    // Static art: face, rim, tick marks and numerals — the last two as the
    // Numerals and Tick marks choices ask for.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        // face + rim
        D2D1_ELLIPSE face = D2D1::Ellipse(D2D1::Point2F(CX, CX), RADIUS, RADIUS);
        b->SetColor(FromRGB(m_o.face, m_faceAlpha));
        rt->FillEllipse(face, b);
        b->SetColor(FromRGB(m_o.mark));
        rt->DrawEllipse(face, b, 3.0f);

        // 60 tick marks, rotated line at 12 o'clock; "Hour" keeps every 5th
        if (m_o.ticks != 2) {
            int step = m_o.ticks == 1 ? 5 : 1;
            for (int i = 0; i < 60; i += step) {
                bool major = (i % 5) == 0;
                rt->SetTransform(D2D1::Matrix3x2F::Rotation(i * 6.0f, D2D1::Point2F(CX, CX)));
                rt->DrawLine(D2D1::Point2F(CX, CX - RADIUS + 6),
                             D2D1::Point2F(CX, CX - RADIUS + (major ? 16.0f : 10.0f)),
                             b, major ? 2.5f : 1.0f);
            }
            rt->SetTransform(D2D1::Matrix3x2F::Identity());
        }
        if (m_o.nums == 2) return true;
        int nStep = m_o.nums == 1 ? 3 : 1;

        // Numerals. The factory is the process-wide shared one (faces.cpp) and
        // is NOT released here — only the format is. A factory of our own
        // would re-enumerate the font collection on every dial build, which is
        // twice per resize drag.
        IDWriteFactory* dw = SharedDWrite();
        IDWriteTextFormat* fmt = nullptr;
        if (dw &&
            SUCCEEDED(dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                           DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                           22.0f, L"", &fmt)))
        {
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            for (int n = nStep; n <= 12; n += nStep) {
                float a = n * 30.0f * 3.14159265f / 180.0f;
                float r = RADIUS - 34;
                float x = CX + r * sinf(a), y = CX - r * cosf(a);
                WCHAR t[4]; int len = wsprintfW(t, L"%d", n);   // wsprintf: no CRT printf pulled in
                rt->DrawTextW(t, len, fmt,
                              D2D1::RectF(x - 20, y - 16, x + 20, y + 16), b);
            }
        }
        SafeRelease(fmt);     // the shared factory outlives this call
        return true;
    }

    // The shared tapered hands, in this instance's colours.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        DrawClassicHands(rt, b, st, seconds, base, FromRGB(m_o.hand),
                         FromRGB(m_o.mark), FromRGB(m_o.accent));
    }
};

static const ClassicFace s_dark (L"Classic dark",  o_dark,  s_darkOpts,
                                 _countof(s_darkOpts),  0.90f);
static const ClassicFace s_light(L"Classic light", o_light, s_lightOpts,
                                 _countof(s_lightOpts), 0.94f);

extern const IClockFace* const g_faceClassicDark  = &s_dark;
extern const IClockFace* const g_faceClassicLight = &s_light;
