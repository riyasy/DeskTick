// face_bold.cpp — "Bold": white dial, heavy ring.
// Thick black ring with a hard offset shadow (drawn as an offset black
// ellipse — no blur needed), no ticks or numerals, chunky black hands, red
// second hand with a big red center dot. Deliberately no digital readout:
// it would change every minute, pulling DirectWrite into the per-tick path
// this widget keeps free of it.

#include "faces.h"

static const float R = RADIUS - 6;      // room for the border + offset shadow

// ---- options ----
// "Normal width", not "Normal": choice strings are translated by their English
// text, so a bare "Normal" here and in Dots' size list would share one
// translation — and in a language whose adjectives agree with their noun, one
// word cannot serve a width and a size. Free to rename: only the index is
// persisted (settings.cpp), never the text.
static const WCHAR* const RING_W[] = { L"Thin", L"Normal width", L"Thick", nullptr };
static const float RING_PX[] = { 4.0f, 6.0f, 9.0f };
// ClampOpt can only count the string list, so a fourth name without a fourth
// width would clamp clean and then read past RING_PX. +1 for the terminator.
static_assert(_countof(RING_W) == _countof(RING_PX) + 1, "RING_W/RING_PX disagree");

static int o_face   = RGB(255, 255, 255);
static int o_ring   = RGB(0, 0, 0);
static int o_accent = RGB(255, 0, 0);
static int o_ringW  = 1;

static const FaceOpt s_opts[] = {
    { L"Face colour",    OPT_COLOR,  &o_face,   nullptr },
    { L"Ring colour",    OPT_COLOR,  &o_ring,   nullptr },
    { L"Accent colour",  OPT_COLOR,  &o_accent, nullptr },
    { L"Ring thickness", OPT_CHOICE, &o_ringW,  RING_W  },
};

class BoldFace : public IClockFace {
public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Bold"; }

    // Static art: drop shadow, the two-tone face, and the heavy ring.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        // Both shadows are hard-edged, done with offset solid ellipses:
        // outer drop shadow: black shifted down-right, past the ring
        b->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.95f));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(CX + 4, CX + 8), R, R), b);
        // inset highlight: gainsboro face, then the white face shifted
        // down-right on top — leaves a gray crescent on the top-left rim
        // (the ring stroke covers the white's slight overhang bottom-right)
        D2D1_COLOR_F face = FromRGB(o_face);
        b->SetColor(D2D1::ColorF(face.r * 0.86f, face.g * 0.86f, face.b * 0.86f));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(CX, CX), R, R), b);
        b->SetColor(face);
        // offset + radius must stay within R+3 (ring outer edge), or a white
        // sliver shows over the drop shadow at the bottom-right
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(CX + 2, CX + 4), R - 3, R - 3), b);
        // thick ring
        b->SetColor(FromRGB(o_ring));
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(CX, CX), R, R), b, RING_PX[o_ringW]);
        return true;
    }

    // The four knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }

    // Chunky tailless hands, then the accent dot that doubles as the hub.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        D2D1_POINT_2F c = D2D1::Point2F(CX, CX);
        // Chunky rounded-rect hands, no tails: they pivot on their bottom edge.
        struct { float deg, halfW, tip; D2D1_COLOR_F col; } hands[] = {
            { (st.wHour % 12 + st.wMinute / 60.0f) * 30.0f,
              7.0f, R * 0.64f, FromRGB(o_ring) },
            { (st.wMinute + st.wSecond / 60.0f) * 6.0f,
              5.2f, R * 0.84f, FromRGB(o_ring) },
            { st.wSecond * 6.0f,
              2.6f, R * 0.80f, FromRGB(o_accent, 0.8f) },
        };
        int nHands = seconds ? 3 : 2;
        for (int i = 0; i < nHands; i++) {
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(hands[i].deg, c) * base);
            b->SetColor(hands[i].col);
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(CX - hands[i].halfW, CX - hands[i].tip,
                                              CX + hands[i].halfW, CX),
                                  2.0f, 2.0f), b);
        }
        rt->SetTransform(base);
        // Big accent center dot — the only hub, and large enough to hide
        // where the hands join.
        b->SetColor(FromRGB(o_accent));
        rt->FillEllipse(D2D1::Ellipse(c, 11.0f, 11.0f), b);
    }
};

static const BoldFace s_bold;
extern const IClockFace* const g_faceBold = &s_bold;
