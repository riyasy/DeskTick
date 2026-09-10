// face_image.cpp — image face: dial is a PNG/BMP from the assets folder,
// hands are the default dark ones (asset images are typically light).
// One mutable static instance retargeted via SetImageFacePath() — not in
// g_builtinFaces (those are fixed, named, const); the engine selects it
// directly when the user picks a file from the menu.

#include "faces.h"
#include <wincodec.h>
#include <strsafe.h>

// ------------------------------------------------------------------
// Draw `path` scaled into the 240-DIP dial area. WIC lives only for
// this call (same pattern as DWrite): decode, convert to premultiplied
// BGRA, draw, release everything — the full-resolution source image never
// survives past the dial rebuild, only the window-sized cached dial does.
// ------------------------------------------------------------------
static bool DrawFaceImage(ID2D1RenderTarget* rt, const WCHAR* path, const D2D1_RECT_F& dest)
{
    // Scale in WIC (high-quality cubic) to the destination's exact pixel
    // size, so the D2D draw is 1:1 — D2D 1.0's own DrawBitmap can only do
    // linear, which visibly degrades large downscales.
    UINT side = (UINT)((dest.right - dest.left) * rt->GetPixelSize().width / DIAL + 0.5f);
    D2D1_SIZE_U dst = { side, side };
    IWICImagingFactory* wic = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scale = nullptr;
    IWICFormatConverter* conv = nullptr;
    ID2D1Bitmap* bmp = nullptr;
    bool ok =
        SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                 WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) &&
        SUCCEEDED(wic->CreateBitmapScaler(&scale)) &&
        SUCCEEDED(scale->Initialize(frame, dst.width, dst.height,
                                    WICBitmapInterpolationModeHighQualityCubic)) &&
        SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(scale, GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(rt->CreateBitmapFromWicBitmap(conv, nullptr, &bmp));
    if (ok)
        rt->DrawBitmap(bmp, dest);
    SafeRelease(bmp); SafeRelease(conv); SafeRelease(scale);
    SafeRelease(frame); SafeRelease(dec); SafeRelease(wic);
    return ok;
}

// ---- options ----
// Hands over an arbitrary image are the one thing this face genuinely needs
// configurable: DARK_HAND vanishes on a dark picture.
static int o_hand   = RGB(31, 33, 38);      // matches DARK_HAND
static int o_accent = RGB(242, 89, 77);

static const FaceOpt s_opts[] = {
    { L"Hand colour",   OPT_COLOR, &o_hand,   nullptr },
    { L"Accent colour", OPT_COLOR, &o_accent, nullptr },
};

class ImageFace : public IClockFace {
    WCHAR m_path[MAX_PATH] = {};
public:
    // The two knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }
    // Point the face at another image. The dial is rebuilt by the caller.
    // StringCchPrintfW rather than wsprintfW, which takes no buffer size and
    // stops only at 1024 characters: the assets folder and the filename are
    // each up to MAX_PATH, so the joined path can be twice this buffer. Same
    // hazard settings.cpp's Section() exists to dodge — see there.
    void SetPath(const WCHAR* dir, const WCHAR* file)
    {
        StringCchPrintfW(m_path, _countof(m_path), L"%s\\%s", dir, file);
    }
    // The image's full path. settings.cpp maps this to a fixed INI section, so
    // one file per picture is never written.
    const WCHAR* GetName() const override { return m_path; }
    // Static art: the image, scaled to the dial. False if it cannot be loaded,
    // which sends the engine to the default face.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush*) const override
    {
        return DrawFaceImage(rt, m_path, D2D1::RectF(0, 0, DIAL, DIAL));
    }
    // The shared tapered hands, in the two colours above.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        DrawClassicHands(rt, b, st, seconds, base, FromRGB(o_hand),
                         FromRGB(o_hand), FromRGB(o_accent));
    }
};

static ImageFace s_imageFace;   // mutable: path changes at selection time
extern const IClockFace* const g_faceImage = &s_imageFace;

// Retarget the one image face, before the engine rebuilds the dial with it.
void SetImageFacePath(const WCHAR* dir, const WCHAR* file) { s_imageFace.SetPath(dir, file); }
