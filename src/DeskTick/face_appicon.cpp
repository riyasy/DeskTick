// face_appicon.cpp — "Icon": iOS-style clock-app icon.
// Black rounded square filling the window, light circle, plain numerals,
// rounded dark hands drawn as rounded rects, red second hand with a center
// dot. There is no smooth sweep: discrete ticks are what keep the widget at
// 0% idle CPU.

#include "faces.h"
#include <dwrite.h>
#include <math.h>

// The geometry is a 192px circle in a 220px square, scaled to the 240-DIP
// window. All lengths below are pre-scaled (× 240/220).
static const float CIRCLE_R = 105.0f;   // 96 × 240/220

// ---- options ----
// "Circle" is just the plate radius taken to half the square, which turns
// the rounded square into a disc — no separate code path.
static const WCHAR* const SHAPES[] = { L"Rounded", L"Square", L"Circle", nullptr };
static const float SHAPE_R[] = { 41.0f, 0.0f, DIAL / 2 - 1 };
// ClampOpt can only count the string list, so a fourth name without a fourth
// radius would clamp clean and then read past SHAPE_R. +1 for the terminator.
static_assert(_countof(SHAPES) == _countof(SHAPE_R) + 1, "SHAPES/SHAPE_R disagree");

static int o_plate  = RGB(0, 0, 0);
static int o_dial   = RGB(241, 241, 241);
static int o_ink    = RGB(48, 48, 48);
static int o_accent = RGB(224, 61, 28);
static int o_shape  = 0;

static const FaceOpt s_opts[] = {
    { L"Plate colour",  OPT_COLOR,  &o_plate,  nullptr },
    { L"Dial colour",   OPT_COLOR,  &o_dial,   nullptr },
    { L"Ink colour",    OPT_COLOR,  &o_ink,    nullptr },
    { L"Accent colour", OPT_COLOR,  &o_accent, nullptr },
    { L"Plate shape",   OPT_CHOICE, &o_shape,  SHAPES  },
};

class AppIconFace : public IClockFace {
public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Icon"; }

    // Static art: the plate, the light circle on it, and twelve numerals.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        // rounded square = the icon plate (fills the whole window)
        b->SetColor(FromRGB(o_plate, 0.97f));
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(1, 1, DIAL - 1, DIAL - 1),
                              SHAPE_R[o_shape], SHAPE_R[o_shape]), b);

        // light clock circle
        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);
        b->SetColor(FromRGB(o_dial));
        rt->FillEllipse(D2D1::Ellipse(c, CIRCLE_R, CIRCLE_R), b);

        // numerals — plain weight (shared factory, format released here;
        // see face_classic.cpp)
        b->SetColor(FromRGB(o_ink));
        IDWriteFactory* dw = SharedDWrite();
        IDWriteTextFormat* fmt = nullptr;
        if (dw &&
            SUCCEEDED(dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                           22.0f, L"", &fmt)))
        {
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            for (int n = 1; n <= 12; n++) {
                float a = n * 30.0f * 3.14159265f / 180.0f;
                float r = CIRCLE_R - 26;
                float x = CX + r * sinf(a), y = CX - r * cosf(a);
                WCHAR t[4]; int len = wsprintfW(t, L"%d", n);
                rt->DrawTextW(t, len, fmt,
                              D2D1::RectF(x - 20, y - 16, x + 20, y + 16), b);
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

    // Three rounded-rect hands, then the hub and its accent dot on top.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);
        // Hands are rounded rects rotated about the center: halfW, tip above
        // center, tail below.
        struct { float deg, halfW, tip, tail; D2D1_COLOR_F col; } hands[] = {
            { (st.wHour % 12 + st.wMinute / 60.0f) * 30.0f,
              4.4f,  69.0f,  2.0f, FromRGB(o_ink) },
            { (st.wMinute + st.wSecond / 60.0f) * 6.0f,
              3.3f,  96.0f,  2.0f, FromRGB(o_ink) },
            { st.wSecond * 6.0f,
              1.1f, 101.0f, 13.0f, FromRGB(o_accent) },
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
        // dark hub, then a small accent dot on top when seconds are shown
        b->SetColor(FromRGB(o_ink));
        rt->FillEllipse(D2D1::Ellipse(c, 7.6f, 7.6f), b);
        if (seconds) {
            b->SetColor(FromRGB(o_accent));
            rt->FillEllipse(D2D1::Ellipse(c, 4.4f, 4.4f), b);
        }
    }
};

static const AppIconFace s_appIcon;
extern const IClockFace* const g_faceAppIcon = &s_appIcon;
