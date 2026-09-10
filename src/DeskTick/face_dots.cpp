// face_dots.cpp — "Dots": twelve dots, two rounded needles, one flat blue,
// nothing else. No rim, no numerals, no hub, no shading.
//
// Deliberately the thinnest face here: one colour, one layer, no halo and no
// shadow. On a wallpaper close to #215c82 it will go quiet — that is the
// price of the flat treatment, not a bug.

#include "faces.h"

static const float RING = 93.0f;                // dot ring radius

// ---- options ----
// Counts ascend, because a list of numbers that doesn't reads as a mistake.
// The default is whichever index holds it — here, the middle one.
static const WCHAR* const COUNTS[] = { L"4", L"12", L"60", nullptr };
static const int   COUNT_N[]  = { 4, 12, 60 };
// "Normal size" rather than "Normal", so it cannot share a translation with
// Bold's ring width — see the note there.
static const WCHAR* const SIZES[]  = { L"Small", L"Normal size", L"Large", nullptr };
static const float SIZE_PX[] = { 2.6f, 4.0f, 6.0f };
// ClampOpt can only count the string lists, so a fourth choice without its
// value would clamp clean and then read past these. +1 for the terminator.
static_assert(_countof(COUNTS) == _countof(COUNT_N)  + 1, "COUNTS/COUNT_N disagree");
static_assert(_countof(SIZES)  == _countof(SIZE_PX)  + 1, "SIZES/SIZE_PX disagree");

static int o_ink   = RGB(33, 92, 130);      // #215c82
static int o_count = 1;                     // 12 dots — index 1 in COUNTS
static int o_size  = 1;

static const FaceOpt s_opts[] = {
    { L"Ink colour", OPT_COLOR,  &o_ink,   nullptr },
    { L"Dot count",  OPT_CHOICE, &o_count, COUNTS  },
    { L"Dot size",   OPT_CHOICE, &o_size,  SIZES   },
};

class DotsFace : public IClockFace {
public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Dots"; }

    // Static art: the hit-test wash and the ring of dots, count and size as
    // the two choices ask for.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        // The ring leaves most of the square empty, and an empty ULW pixel is
        // click-through — see FillHitTestWash.
        FillHitTestWash(rt, b);

        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);
        b->SetColor(FromRGB(o_ink));
        int n = COUNT_N[o_count];
        // 60 dots at the normal size would nearly touch, so scale them down
        // with the count rather than adding a fourth size choice.
        float dot = SIZE_PX[o_size] * (n == 60 ? 0.45f : 1.0f);
        for (int i = 0; i < n; i++) {
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(i * 360.0f / n, c));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(CX, CX - RING), dot, dot), b);
        }
        rt->SetTransform(D2D1::Matrix3x2F::Identity());
        return true;
    }

    // The three knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }

    // Two or three tailed needles in the single ink colour. No hub is drawn.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);
        // Every needle carries a tail past the centre — without one they look
        // pinned to a point rather than pivoting through it. There is no hub
        // on this face; the tails crossing are the hub.
        // Hour and minute share a width; the second hand stays thinner so it
        // reads as the odd one out without needing a second colour, which
        // this face does not have.
        struct { float deg, halfW, len, tail; } arms[3] = {
            { (st.wHour % 12 + st.wMinute / 60.0f) * 30.0f, 2.75f, 48.0f, 7.0f },
            { (st.wMinute + st.wSecond / 60.0f) * 6.0f,     2.75f, 71.0f, 7.0f },
            { st.wSecond * 6.0f,                            1.25f, 85.0f, 9.0f },
        };
        int n = seconds ? 3 : 2;
        b->SetColor(FromRGB(o_ink));
        for (int i = 0; i < n; i++) {
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(arms[i].deg, c) * base);
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(CX - arms[i].halfW, CX - arms[i].len,
                                              CX + arms[i].halfW, CX + arms[i].tail),
                                  arms[i].halfW, arms[i].halfW), b);
        }
        rt->SetTransform(base);
    }
};

static const DotsFace s_dots;
extern const IClockFace* const g_faceDots = &s_dots;
