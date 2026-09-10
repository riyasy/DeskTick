// face_simple.cpp — "Simple": white dial with soft drop shadow, 60 thin
// minute ticks (longer every 5th), gray numerals at 12/3/6/9 only, black
// kite-shaped hands with sharp tips, thin red second hand, small ringed hub.
// The kite hands are cached ID2D1PathGeometry objects: built once on first
// draw via rt->GetFactory(), reused for the process lifetime (never freed —
// two tiny device-independent objects, OS reclaims at exit).

#include "faces.h"
#include <dwrite.h>
#include <math.h>

static const float FR = RADIUS - 3;      // face radius, room for the shadow

// ---- options ----
// Hand length and width stay off the menu: they are baked into the cached
// path geometries below, which nothing invalidates.
// Same list and same order as face_classic.cpp: two faces offering the same
// choice in a different order is a trap when you switch between them. Where
// the defaults differ, the default moves — the list does not.
static const WCHAR* const NUMERALS[] = { L"All 12", L"12, 3, 6, 9", L"None", nullptr };
static const WCHAR* const TICKS[]    = { L"Minute", L"Hour", L"None", nullptr };

static int o_face   = RGB(255, 255, 255);
static int o_hand   = RGB(20, 20, 20);
static int o_accent = RGB(237, 28, 36);
static int o_nums   = 1;                    // 12, 3, 6, 9 — index 1 in NUMERALS
static int o_ticks  = 0;

static const FaceOpt s_opts[] = {
    { L"Face colour",   OPT_COLOR,  &o_face,   nullptr  },
    { L"Hand colour",   OPT_COLOR,  &o_hand,   nullptr  },
    { L"Accent colour", OPT_COLOR,  &o_accent, nullptr  },
    { L"Numerals",      OPT_CHOICE, &o_nums,   NUMERALS },
    { L"Tick marks",    OPT_CHOICE, &o_ticks,  TICKS    },
};

// Kite-shaped hand: sharp tip at `len` above center, widest (`halfW`)
// exactly at the rotation center, short pointed tail behind it — so the
// hand pivots on its widest point.
static ID2D1PathGeometry* MakeHand(ID2D1Factory* f, float len, float halfW)
{
    ID2D1PathGeometry* g = nullptr;
    ID2D1GeometrySink* s = nullptr;
    if (SUCCEEDED(f->CreatePathGeometry(&g)) && SUCCEEDED(g->Open(&s))) {
        s->BeginFigure(D2D1::Point2F(CX, CX - len), D2D1_FIGURE_BEGIN_FILLED);
        s->AddLine(D2D1::Point2F(CX + halfW, CX));
        s->AddLine(D2D1::Point2F(CX, CX + 14));
        s->AddLine(D2D1::Point2F(CX - halfW, CX));
        s->EndFigure(D2D1_FIGURE_END_CLOSED);
        s->Close();
    }
    SafeRelease(s);
    return g;
}

class SimpleFace : public IClockFace {
    mutable ID2D1PathGeometry* m_hour   = nullptr; // lazy, cached forever
    mutable ID2D1PathGeometry* m_min    = nullptr;
    mutable ID2D1StrokeStyle*  m_round  = nullptr; // round joins: softens corners
public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Simple"; }

    // Static art: shadow, face, tick marks and numerals — the last two as the
    // Numerals and Tick marks choices ask for.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);
        // soft drop shadow (same trick as Minimal)
        for (int i = 3; i >= 1; i--) {
            b->SetColor(D2D1::ColorF(0, 0, 0, 0.05f));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(CX, CX + 1.5f * i), FR + i, FR + i), b);
        }
        // flat face
        b->SetColor(FromRGB(o_face, 0.98f));
        rt->FillEllipse(D2D1::Ellipse(c, FR, FR), b);

        // 60 thin ticks, longer + thicker every 5th; "Hour" keeps only those.
        if (o_ticks != 2) {
            int step = o_ticks == 1 ? 5 : 1;
            b->SetColor(D2D1::ColorF(0.27f, 0.27f, 0.27f));
            for (int i = 0; i < 60; i += step) {
                bool major = (i % 5) == 0;
                rt->SetTransform(D2D1::Matrix3x2F::Rotation(i * 6.0f, c));
                rt->DrawLine(D2D1::Point2F(CX, CX - FR + 4),
                             D2D1::Point2F(CX, CX - FR + (major ? 14.0f : 9.0f)),
                             b, major ? 2.0f : 1.0f);
            }
            rt->SetTransform(D2D1::Matrix3x2F::Identity());
        }

        // numerals (shared DWrite, see face_classic.cpp)
        if (o_nums == 2) return true;
        int nStep = o_nums == 1 ? 3 : 1;        // index 1 is every third numeral
        b->SetColor(D2D1::ColorF(0.42f, 0.42f, 0.42f));
        IDWriteFactory* dw = SharedDWrite();
        IDWriteTextFormat* fmt = nullptr;
        if (dw &&
            SUCCEEDED(dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                           26.0f, L"", &fmt)))
        {
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            for (int n = nStep; n <= 12; n += nStep) {
                float a = n * 30.0f * 3.14159265f / 180.0f;
                float r = FR - 27;
                float x = CX + r * sinf(a), y = CX - r * cosf(a);
                WCHAR t[4]; int len = wsprintfW(t, L"%d", n);
                rt->DrawTextW(t, len, fmt,
                              D2D1::RectF(x - 22, y - 18, x + 22, y + 18), b);
            }
        }
        SafeRelease(fmt);
        return true;
    }

    // The five knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }

    // The two kite hands, the second hand and its counterweight, then the
    // ringed hub. Builds the cached geometries on first call.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        if (!m_hour) {                            // one-time geometry build
            ID2D1Factory* f = nullptr;
            rt->GetFactory(&f);
            m_hour = MakeHand(f, FR * 0.58f, 7.0f);
            m_min  = MakeHand(f, FR * 0.85f, 6.0f);
            f->CreateStrokeStyle(
                D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                            D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                nullptr, 0, &m_round);
            f->Release();
        }
        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);
        b->SetColor(FromRGB(o_hand));
        float hourDeg = (st.wHour % 12 + st.wMinute / 60.0f) * 30.0f;
        float minDeg  = (st.wMinute + st.wSecond / 60.0f) * 6.0f;
        // fill + same-color round-join outline = corners rounded by half the
        // stroke width (1.5 -> ~0.75 DIP radius: takes the razor edge off
        // without going soft)
        if (m_hour) {
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(hourDeg, c) * base);
            rt->FillGeometry(m_hour, b);
            rt->DrawGeometry(m_hour, b, 1.5f, m_round);
        }
        if (m_min) {
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(minDeg, c) * base);
            rt->FillGeometry(m_min, b);
            rt->DrawGeometry(m_min, b, 1.5f, m_round);
        }
        if (seconds) {
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(st.wSecond * 6.0f, c) * base);
            b->SetColor(FromRGB(o_accent));
            rt->DrawLine(D2D1::Point2F(CX, CX + 20),
                         D2D1::Point2F(CX, CX - FR * 0.82f), b, 1.6f);
            // balancing counterweight on the tail
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(CX - 2.5f, CX + 8, CX + 2.5f, CX + 22),
                                  2.5f, 2.5f), b);
        }
        rt->SetTransform(base);
        // tiny hub: white dot with a black ring
        b->SetColor(FromRGB(o_face));
        rt->FillEllipse(D2D1::Ellipse(c, 3.5f, 3.5f), b);
        b->SetColor(FromRGB(o_hand));
        rt->DrawEllipse(D2D1::Ellipse(c, 3.5f, 3.5f), b, 1.8f);
    }
};

static const SimpleFace s_simple;
extern const IClockFace* const g_faceSimple = &s_simple;
