// face_glass.cpp — "Glass": a frosted disc with a light-catching rim, 12
// numerals, 48 minute ticks and an orange second hand.
//
// Real glass would blur whatever shows through it. A layered window has
// nothing to sample — UpdateLayeredWindow composites our DIB onto the
// desktop and never reads the desktop back — so "frost" here is a
// translucent white fill, brightest at the top. Over a busy wallpaper it
// reads the same, and it costs one gradient instead of a blur pass we have
// no device context to run.
//
// The rim is a conic gradient sampled into 72 chords: two bright arcs
// 180 deg apart, dim between them. All of it is dial work, cached.
//
// Deliberately no inset shadows: the body gradient, the rim and the drop
// shadow already give the disc its form. If it ever reads flat, they are a
// gradient-stroked ellipse just inside the rim.

#include "faces.h"
#include <dwrite.h>
#include <math.h>

static const float PI = 3.14159265f;
static const float FR = RADIUS - 6;                 // 110: face radius

// Ring radii, all measured at 2x and halved.
static const float TICK_OUT = 104.0f;               // minute tick, outer end
static const float TICK_IN  = 97.5f;                // minute tick, inner end
static const float NUM_R    = 91.0f;                // numeral centres

// ---- options ----
// Frost is a choice, not a colour: it is the fill's alpha pair, and
// ChooseColorW has no alpha to give.
static const WCHAR* const FROST[] = { L"Clear", L"Frosted", L"Opaque", nullptr };
static const float FROST_TOP[] = { 0.34f, 0.62f, 0.88f };
static const float FROST_BOT[] = { 0.20f, 0.44f, 0.74f };
// ClampOpt (settings.cpp) clamps a choice against the length of the STRING list,
// which is the only one it can count — so a fourth name without a fourth pair
// of stops would be admitted and then read past these arrays on the next
// repaint. +1 for the null terminator the string list carries.
static_assert(_countof(FROST) == _countof(FROST_TOP) + 1, "FROST/FROST_TOP disagree");
static_assert(_countof(FROST) == _countof(FROST_BOT) + 1, "FROST/FROST_BOT disagree");

static int o_ink    = RGB(50, 50, 50);
static int o_accent = RGB(255, 107, 0);     // #ff6b00 — second hand and hub
static int o_frost  = 1;
static int o_nums   = 1;

static const FaceOpt s_opts[] = {
    { L"Ink colour",    OPT_COLOR,  &o_ink,    nullptr },
    { L"Accent colour", OPT_COLOR,  &o_accent, nullptr },
    { L"Frost",         OPT_CHOICE, &o_frost,  FROST   },
    { L"Numerals",      OPT_BOOL,   &o_nums,   nullptr },
};

class GlassFace : public IClockFace {
public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Glass"; }

    // Static art, and the expensive half of this face: shadow, frost, the
    // 72-chord rim light, the minute ticks and the numerals.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);

        // soft drop shadow: stacked translucent ellipses, shifted down
        b->SetColor(D2D1::ColorF(0, 0.06f));
        for (int i = 3; i >= 1; i--)
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(CX, CX + 2.0f * i),
                                          FR + i, FR + i), b);

        // Frost. One gradient carries both the body and the top reflection —
        // brightest at the top, never fully opaque, so the wallpaper still
        // shows through the way glass should.
        FillEllipseVGrad(rt, D2D1::Ellipse(c, FR, FR),
                         D2D1::ColorF(1.0f, 1.0f, 1.0f, FROST_TOP[o_frost]),
                         D2D1::ColorF(1.0f, 1.0f, 1.0f, FROST_BOT[o_frost]));

        // Rim light. |cos(angle + 45 deg)|^8 is the conic gradient's shape:
        // ~1 at the two light points, ~0 a few degrees either side.
        for (int i = 0; i < 72; i++) {
            float a0 = i * 5.0f * PI / 180.0f, a1 = a0 + 5.0f * PI / 180.0f;
            float k = cosf(a0 + PI / 4);
            k *= k; k *= k; k *= k;                 // |cos|^8, sign gone with the first square
            b->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.20f + 0.75f * k));
            rt->DrawLine(D2D1::Point2F(CX + FR * sinf(a0), CX - FR * cosf(a0)),
                         D2D1::Point2F(CX + FR * sinf(a1), CX - FR * cosf(a1)), b, 1.6f);
        }

        // 48 minute ticks — the 12 skipped positions carry a numeral instead.
        // With numerals off there is nothing to make room for, so draw all 60.
        b->SetColor(FromRGB(o_ink, 0.5f));
        for (int i = 0; i < 60; i++) {
            if (o_nums && i % 5 == 0) continue;
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(i * 6.0f, c));
            rt->DrawLine(D2D1::Point2F(CX, CX - TICK_OUT),
                         D2D1::Point2F(CX, CX - TICK_IN), b, 0.9f);
        }
        rt->SetTransform(D2D1::Matrix3x2F::Identity());

        // numerals (shared DWrite, as everywhere else on the dial path)
        if (!o_nums) return true;
        b->SetColor(FromRGB(o_ink, 0.9f));
        IDWriteFactory* dw = SharedDWrite();
        IDWriteTextFormat* fmt = nullptr;
        if (dw) {
            fmt = MakeTextFormat(dw, L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_MEDIUM,
                                 DWRITE_TEXT_ALIGNMENT_CENTER);
            for (int n = 0; n < 12; n++) {
                float a = n * 30.0f * PI / 180.0f;
                float x = CX + NUM_R * sinf(a), y = CX - NUM_R * cosf(a);
                WCHAR t[4]; wsprintfW(t, L"%d", n ? n : 12);
                DrawTextIn(rt, b, fmt, t, D2D1::RectF(x - 12, y - 9, x + 12, y + 9));
            }
        }
        SafeRelease(fmt);
        return true;
    }

    // The four knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }

    // Hour and minute bars, the frosted centre piece over them, then the
    // second hand and hub on top.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);

        // Rounded bars pivoting on their own bottom edge. The hour sits at
        // the classic 0.70 of the face and the minute at 0.78, reaching under
        // the numeral ring — shorter than this reads stubby at widget size,
        // and hands passing over numerals is normal on an analog dial.
        struct { float deg, len, halfW; } arms[2] = {
            { (st.wHour % 12 + st.wMinute / 60.0f) * 30.0f, 60.0f, 2.75f },
            { (st.wMinute + st.wSecond / 60.0f) * 6.0f,     86.0f, 1.75f },
        };
        b->SetColor(FromRGB(o_ink, 0.9f));
        for (int i = 0; i < 2; i++) {
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(arms[i].deg, c) * base);
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(CX - arms[i].halfW, CX - arms[i].len,
                                              CX + arms[i].halfW, CX),
                                  arms[i].halfW, arms[i].halfW), b);
        }
        rt->SetTransform(base);

        // Frosted centre piece: over the hour and minute hands, under the
        // second hand.
        b->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f));
        rt->FillEllipse(D2D1::Ellipse(c, 13.5f, 13.5f), b);
        b->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f));
        rt->FillEllipse(D2D1::Ellipse(c, 11.5f, 11.5f), b);

        if (seconds) {
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(st.wSecond * 6.0f, c) * base);
            b->SetColor(FromRGB(o_accent));
            // Follows the minute hand out: a second hand shorter than the
            // minute hand reads as a broken clock. Tip stops just inside the
            // tick track at 97.5.
            rt->FillRectangle(D2D1::RectF(CX - 0.625f, CX - 96.0f, CX + 0.625f, CX), b);
            rt->FillRoundedRectangle(          // counterweight past the hub
                D2D1::RoundedRect(D2D1::RectF(CX - 1.875f, CX - 2, CX + 1.875f, CX + 9),
                                  1.875f, 1.875f), b);
            rt->SetTransform(base);
        }
        b->SetColor(FromRGB(o_accent));
        rt->FillEllipse(D2D1::Ellipse(c, 3.75f, 3.75f), b);
    }
};

static const GlassFace s_glass;
extern const IClockFace* const g_faceGlass = &s_glass;
