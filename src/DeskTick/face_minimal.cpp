// face_minimal.cpp — "Minimal": a pale, near-flat face.
// No numerals: 12 short rim ticks (red at 12, darker at 3/6/9), embossed
// center circle, short gray hands, thin red second hand with a center
// counterweight dot. The emboss is two opposing vertical gradients (face
// light->dark, inner circle dark->light); the drop shadow is stacked
// translucent ellipses rather than a blur. All of it happens only in the
// cached dial build — nothing here runs per tick.

#include "faces.h"

// ---- options ----
// The dial is a gradient, so the colour knob sets its midpoint and Shade()
// derives the two stops +/- 0.05 either side of it.
static int o_dial   = RGB(237, 237, 237);
static int o_hand   = RGB(84, 84, 84);
static int o_accent = RGB(171, 0, 0);
static int o_mark12 = 1;                    // 12 o'clock tick drawn in the accent

static const FaceOpt s_opts[] = {
    { L"Dial colour",       OPT_COLOR, &o_dial,   nullptr },
    { L"Hand colour",       OPT_COLOR, &o_hand,   nullptr },
    { L"Accent colour",     OPT_COLOR, &o_accent, nullptr },
    // Draws the 12 o'clock tick in the accent colour; off, it takes the same
    // darker shade of the dial as the 3/6/9 ticks.
    { L"Accent 12 marker",  OPT_BOOL,  &o_mark12, nullptr },
};

// Lighten/darken a configured colour by `d` so a single knob can still feed
// the two-stop gradients the emboss is built from.
static D2D1_COLOR_F Shade(int c, float d, float a = 1.0f)
{
    D2D1_COLOR_F k = FromRGB(c, a);
    k.r = min(1.0f, max(0.0f, k.r + d));
    k.g = min(1.0f, max(0.0f, k.g + d));
    k.b = min(1.0f, max(0.0f, k.b + d));
    return k;
}

class MinimalFace : public IClockFace {
public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Minimal"; }

    // Static art: shadow, the two opposing gradients that make the emboss,
    // and the twelve hour ticks.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);
        const float R = RADIUS - 3;               // room for the shadow rings
        // soft drop shadow: stacked translucent ellipses, shifted down
        for (int i = 3; i >= 1; i--) {
            b->SetColor(D2D1::ColorF(0, 0, 0, 0.05f));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(CX, CX + 1.5f * i), R + i, R + i), b);
        }
        // face: light at the top, darker at the bottom
        FillEllipseVGrad(rt, D2D1::Ellipse(c, R, R),
                         Shade(o_dial,  0.05f, 0.98f),
                         Shade(o_dial, -0.05f, 0.98f));
        // inner circle: opposite gradient = embossed dish
        FillEllipseVGrad(rt, D2D1::Ellipse(c, R * 0.60f, R * 0.60f),
                         Shade(o_dial, -0.04f),
                         Shade(o_dial,  0.04f));

        // 12 hour ticks only: accent at 12, darker at 3/6/9
        for (int i = 0; i < 12; i++) {
            if (i == 0 && o_mark12) b->SetColor(FromRGB(o_accent));
            else if (i % 3 == 0)    b->SetColor(Shade(o_dial, -0.22f));
            else                    b->SetColor(Shade(o_dial, -0.09f));
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(i * 30.0f, c));
            rt->DrawLine(D2D1::Point2F(CX, CX - RADIUS + 8),     // gap to the edge
                         D2D1::Point2F(CX, CX - RADIUS + 14), b, 2.6f);
        }
        rt->SetTransform(D2D1::Matrix3x2F::Identity());
        return true;
    }

    // The four knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }

    // Hub first, then the hands over it, then the second hand's counterweight
    // dot back over the hub.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);
        // hub, drawn under the hands
        b->SetColor(Shade(o_dial, -0.11f));
        rt->FillEllipse(D2D1::Ellipse(c, 9.5f, 9.5f), b);

        const D2D1_COLOR_F gray = FromRGB(o_hand);
        struct { float deg, tip, tail, width; D2D1_COLOR_F col; } hands[] = {
            { (st.wHour % 12 + st.wMinute / 60.0f) * 30.0f,
              RADIUS * 0.40f, RADIUS * 0.12f, 3.8f, gray },
            { (st.wMinute + st.wSecond / 60.0f) * 6.0f,   // tip stays clear of
              RADIUS * 0.50f, RADIUS * 0.12f, 3.8f, gray }, // the 0.60R circle
            { st.wSecond * 6.0f,
              RADIUS * 0.88f, RADIUS * 0.16f, 1.9f, FromRGB(o_accent) },
        };
        int nHands = seconds ? 3 : 2;
        for (int i = 0; i < nHands; i++) {
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(hands[i].deg, c) * base);
            b->SetColor(hands[i].col);
            rt->DrawLine(D2D1::Point2F(CX, CX + hands[i].tail),
                         D2D1::Point2F(CX, CX - hands[i].tip), b, hands[i].width);
        }
        if (seconds)  // accent counterweight dot covers the hub
            rt->FillEllipse(D2D1::Ellipse(c, 5.0f, 5.0f), b);
        rt->SetTransform(base);
    }
};

static const MinimalFace s_minimal;
extern const IClockFace* const g_faceMinimal = &s_minimal;
