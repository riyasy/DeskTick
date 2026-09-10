// face_flat.cpp — "Flat": a flat dark watch face.
// Charcoal dial with a darker rim, ring of 60 faint dots (larger every
// 5th), stubby flat light hands, hairline tomato second hand, tomato hub.
// No hover readout and nothing animated: the engine's tick loop and the
// cached dial are the whole of it.

#include "faces.h"
#include <math.h>

static const float FR = RADIUS - 10;                     // face inside the rim

// ---- options ----
static int o_dial   = RGB(51, 51, 51);
static int o_rim    = RGB(37, 37, 37);
static int o_hand   = RGB(235, 235, 235);
static int o_accent = RGB(255, 99, 71);     // tomato

static const FaceOpt s_opts[] = {
    { L"Dial colour",   OPT_COLOR, &o_dial,   nullptr },
    { L"Rim colour",    OPT_COLOR, &o_rim,    nullptr },
    { L"Hand colour",   OPT_COLOR, &o_hand,   nullptr },
    { L"Accent colour", OPT_COLOR, &o_accent, nullptr },
};

class FlatFace : public IClockFace {
public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Flat"; }

    // Static art: the rim, the face inside it, and the ring of 60 dots.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);
        // rim (darker) + face
        b->SetColor(FromRGB(o_rim, 0.96f));
        rt->FillEllipse(D2D1::Ellipse(c, RADIUS, RADIUS), b);
        b->SetColor(FromRGB(o_dial, 0.96f));
        rt->FillEllipse(D2D1::Ellipse(c, FR, FR), b);

        // 60 faint dots hugging the rim, larger + brighter every 5th
        for (int i = 0; i < 60; i++) {
            bool major = (i % 5) == 0;
            float a = i * 6.0f * 3.14159265f / 180.0f;
            float r = FR * 0.90f;
            D2D1_POINT_2F p = D2D1::Point2F(CX + r * sinf(a), CX - r * cosf(a));
            b->SetColor(FromRGB(o_hand, major ? 0.18f : 0.10f));
            rt->FillEllipse(D2D1::Ellipse(p, major ? 2.4f : 1.2f, major ? 2.4f : 1.2f), b);
        }
        return true;
    }

    // The four knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }

    // Stubby hands, then the two-tone hub over the top of them.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);
        const D2D1_COLOR_F light = FromRGB(o_hand);
        // chunky rounded hour/minute hands, no tails; hairline second hand
        struct { float deg, halfW, tip, tail; D2D1_COLOR_F col; } hands[] = {
            { (st.wHour % 12 + st.wMinute / 60.0f) * 30.0f,
              3.5f, FR * 0.33f, 0,          light },
            { (st.wMinute + st.wSecond / 60.0f) * 6.0f,
              3.5f, FR * 0.52f, 0,          light },
            { st.wSecond * 6.0f,
              0.9f, FR * 0.70f, FR * 0.14f, FromRGB(o_accent) },
        };
        int nHands = seconds ? 3 : 2;
        for (int i = 0; i < nHands; i++) {
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(hands[i].deg, c) * base);
            b->SetColor(hands[i].col);
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(CX - hands[i].halfW, CX - hands[i].tip,
                                              CX + hands[i].halfW, CX + hands[i].tail),
                                  hands[i].halfW, hands[i].halfW), b);
        }
        rt->SetTransform(base);
        // white hub with a tomato center, on top of the hands
        b->SetColor(light);
        rt->FillEllipse(D2D1::Ellipse(c, 8.0f, 8.0f), b);
        b->SetColor(FromRGB(o_accent));
        rt->FillEllipse(D2D1::Ellipse(c, 4.5f, 4.5f), b);
    }
};

static const FlatFace s_flat;
extern const IClockFace* const g_faceFlat = &s_flat;
