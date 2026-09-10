// faces.cpp — face registry + shared hand-drawing helper.
// Each face lives in its own face_*.cpp and exports a `const IClockFace*
// const g_faceX` pointer. Adding a face = new face_*.cpp + one extern +
// one array entry here (+ the .cpp in the vcxproj).

#include "faces.h"
#include <dwrite_1.h>          // IDWriteTextLayout1, for character spacing

const D2D1_COLOR_F DARK_HAND = { 0.12f, 0.13f, 0.15f, 1.0f };

// An option's COLORREF as a D2D colour, with the alpha the caller wants.
D2D1_COLOR_F FromRGB(int c, float a)
{
    COLORREF cr = (COLORREF)c;
    return D2D1::ColorF(GetRValue(cr) / 255.0f,
                        GetGValue(cr) / 255.0f,
                        GetBValue(cr) / 255.0f, a);
}

// The four date-name tables moved to loc.cpp, where they are filled from the
// user's regional settings at startup instead of being English literals. They
// keep the shape and the indexing they had, so no face changed to get
// translated names — see LocInitDateNames().

// Single thread (clock.cpp owns the only one), so no init guard is needed.
// Never released: DirectWrite keeps the shared factory alive process-wide
// regardless, so holding one reference costs a refcount, not a cache.
static IDWriteFactory* s_dw = nullptr;

// The process-wide factory, built on first call. Null if DirectWrite is
// unavailable, which every caller has to handle.
IDWriteFactory* SharedDWrite()
{
    if (!s_dw)
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                            __uuidof(IDWriteFactory), (IUnknown**)&s_dw);
    return s_dw;
}

// A text format with the paragraph alignment every face wants (CENTER) and
// the horizontal alignment it asks for. Null on failure — callers cope.
IDWriteTextFormat* MakeTextFormat(IDWriteFactory* dw, const WCHAR* family,
                                  float size, DWRITE_FONT_WEIGHT weight,
                                  DWRITE_TEXT_ALIGNMENT align)
{
    IDWriteTextFormat* f = nullptr;
    if (SUCCEEDED(dw->CreateTextFormat(family, nullptr, weight,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       size, L"", &f))) {
        f->SetTextAlignment(align);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return f;
}

// DrawTextW with the string length worked out, and a no-op when the format is
// null — so no caller has to guard a format that failed to build.
void DrawTextIn(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                IDWriteTextFormat* fmt, const WCHAR* s, const D2D1_RECT_F& r)
{
    if (fmt) rt->DrawTextW(s, (UINT32)lstrlenW(s), fmt, r, b);
}

// A layout with uniform letter spacing, which plain DrawText cannot do. This
// allocates, so callers cache the result and rebuild only when `s` changes.
IDWriteTextLayout* MakeTrackedLayout(IDWriteFactory* dw, const WCHAR* s,
                                     IDWriteTextFormat* fmt,
                                     float w, float h, float track)
{
    UINT32 len = (UINT32)lstrlenW(s);
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(dw->CreateTextLayout(s, len, fmt, w, h, &layout)))
        return nullptr;
    // Split the tracking either side of each glyph so a centred line stays
    // centred (all-trailing spacing shifts it visibly left).
    IDWriteTextLayout1* l1 = nullptr;
    if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1),
                                         (void**)&l1))) {
        DWRITE_TEXT_RANGE all = { 0, len };
        l1->SetCharacterSpacing(track, track, 0.0f, all);
        l1->Release();          // `layout` still holds the original ref
    }
    return layout;
}

// Draw `s` one glyph per fixed-width cell, so the run's width depends only on
// its length and a proportional font cannot make the clock twitch. `fmt` must
// be centre-aligned; see the header for when a face needs this.
void DrawCells(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
               IDWriteTextFormat* fmt, const WCHAR* s, float cell,
               float x, float y0, float y1)
{
    if (!fmt) return;
    for (int i = 0; s[i]; i++, x += cell)
        rt->DrawTextW(&s[i], 1, fmt, D2D1::RectF(x, y0, x + cell, y1), b);
}

// Fill an ellipse with a vertical gradient across its own height. The brushes
// live only for this call; a face that draws this every frame would cache them.
void FillEllipseVGrad(ID2D1RenderTarget* rt, const D2D1_ELLIPSE& e,
                      const D2D1_COLOR_F& top, const D2D1_COLOR_F& bottom)
{
    D2D1_GRADIENT_STOP stops[2] = { { 0.0f, top }, { 1.0f, bottom } };
    ID2D1GradientStopCollection* coll = nullptr;
    ID2D1LinearGradientBrush* lgb = nullptr;
    if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, &coll)) &&
        SUCCEEDED(rt->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(e.point.x, e.point.y - e.radiusY),
                D2D1::Point2F(e.point.x, e.point.y + e.radiusY)),
            coll, &lgb)))
        rt->FillEllipse(e, lgb);
    SafeRelease(lgb);
    SafeRelease(coll);
}

// The invisible backing that keeps a text face draggable. 1/255 is the floor,
// not a taste: the DIB is 8-bit per channel, so anything under 0.5/255 rounds
// to alpha 0 and Windows hands the click straight through to the desktop.
// Black rather than white because it survives on a light wallpaper the same
// way — at one part in 255 neither is visible, but only alpha is load-bearing.
void FillHitTestWash(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b)
{
    b->SetColor(D2D1::ColorF(0, 1.0f / 255.0f));
    rt->FillRectangle(D2D1::RectF(0, 0, DIAL, DIAL), b);
}

// Shared hand geometry: used by ClassicFace and directly by the engine
// for image faces (dark hands).
void DrawClassicHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                      const SYSTEMTIME& st, bool seconds,
                      const D2D1_MATRIX_3X2_F& base,
                      const D2D1_COLOR_F& handCol, const D2D1_COLOR_F& hubCol,
                      const D2D1_COLOR_F& secCol)
{
    D2D1_POINT_2F c = D2D1::Point2F(CX, CX);
    struct { float deg, len, tail, width; D2D1_COLOR_F col; } hands[] = {
        { (st.wHour % 12 + st.wMinute / 60.0f) * 30.0f,
          RADIUS * 0.52f, 12, 6.0f, handCol },
        { (st.wMinute + st.wSecond / 60.0f) * 6.0f,
          RADIUS * 0.78f, 12, 4.0f, handCol },
        { st.wSecond * 6.0f,                              // discrete jump, no sweep
          RADIUS * 0.86f, 20, 1.5f, secCol },
    };
    int nHands = seconds ? 3 : 2;
    for (int i = 0; i < nHands; i++) {
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(hands[i].deg, c) * base);
        b->SetColor(hands[i].col);
        rt->DrawLine(D2D1::Point2F(CX, CX + hands[i].tail),
                     D2D1::Point2F(CX, CX - hands[i].len), b, hands[i].width);
    }
    rt->SetTransform(base);
    b->SetColor(hubCol);
    rt->FillEllipse(D2D1::Ellipse(c, 5, 5), b);
}

// One pointer per face file (each is constant-initialized in its own TU,
// so reading them here during this array's initialization is safe).
extern const IClockFace* const g_faceClassicDark;
extern const IClockFace* const g_faceClassicLight;
extern const IClockFace* const g_faceMinimal;
extern const IClockFace* const g_faceAppIcon;
extern const IClockFace* const g_faceBold;
extern const IClockFace* const g_faceFlat;
extern const IClockFace* const g_faceSimple;
extern const IClockFace* const g_faceDigital;
extern const IClockFace* const g_faceDigitalBold;
extern const IClockFace* const g_faceWord;
extern const IClockFace* const g_faceStack;
extern const IClockFace* const g_faceTerminal;
extern const IClockFace* const g_faceGlass;
extern const IClockFace* const g_faceDots;
extern const IClockFace* const g_faceBanner;
extern const IClockFace* const g_faceSplit;

// First entry is the default face (g_face starts at 0) and the fallback
// when an image face fails to load.
const IClockFace* const g_builtinFaces[] = {
    g_faceSimple,
    g_faceClassicDark,
    g_faceClassicLight,
    g_faceMinimal,
    g_faceAppIcon,
    g_faceBold,
    g_faceFlat,
    g_faceGlass,
    g_faceDots,
    g_faceDigital,
    g_faceDigitalBold,
    g_faceWord,
    g_faceStack,
    g_faceTerminal,
    g_faceBanner,
    g_faceSplit,
};
const int NUM_BUILTIN = _countof(g_builtinFaces);
