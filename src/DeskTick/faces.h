// faces.h — procedural clock face interface + built-in face registry.
// The engine (clock.cpp) only sees this header; faces live in faces.cpp.

#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

// ---- layout, all in a fixed 240-DIP logical space shared by the engine
// ---- and every face. Resize just changes the DPI we hand Direct2D. ----
const float DIAL   = 240.0f;          // logical size
const float CX     = DIAL / 2;        // center
const float RADIUS = DIAL / 2 - 4;    // dial radius

// Release a COM pointer if it is set, and null it so a second call is safe.
template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// ------------------------------------------------------------------
// Per-face options. Every value is an int — bool is 0/1, choice is an
// index, colour is a COLORREF — so the dialog, the INI reader and the
// INI writer are each one loop with a three-way switch.
//
// Storage is a file-static int in the face's own TU, which is why a
// `static const` face instance can still be configured: the table is
// const, the ints it points at are not. Each default is the literal the
// face is designed around, so a missing INI still draws the intended look.
//
// What must NOT become an option: anything that invalidates a cached
// IDWriteTextFormat/Layout (font family, size, weight, tracking) or a
// cached path geometry (face_simple's hands). Nothing here does, so
// applying a change is just a dial rebuild — see RefreshFace().
// ------------------------------------------------------------------
enum OptKind { OPT_BOOL, OPT_CHOICE, OPT_COLOR };

struct FaceOpt {
    const WCHAR*        label;     // dialog label AND INI key
    OptKind             kind;
    int*                value;
    const WCHAR* const* choices;   // OPT_CHOICE only, null-terminated
};

// COLORREF -> D2D. Alpha is supplied by the caller because ChooseColorW
// has no alpha channel: a face keeps its own opacity and exposes only hue.
D2D1_COLOR_F FromRGB(int c, float a = 1.0f);

// ------------------------------------------------------------------
// Each built-in face implements IClockFace and registers a static
// instance in g_builtinFaces (faces.cpp) — no heap, no factory.
// Adding a face = write the class, add one instance + one array entry;
// the menu, dial rebuild, and hand drawing pick it up automatically.
// ------------------------------------------------------------------
struct IClockFace {
    // No virtual destructor: instances are static const, never deleted
    // through the base pointer.
    virtual const WCHAR* GetName() const = 0;
    // Draw the static dial into the 240-DIP scene. Return false if the dial
    // couldn't be drawn (e.g. image load failure) — the engine then falls
    // back to the default face.
    virtual bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const = 0;
    // Draw hands + hub for the given time. `base` is the frame's scale
    // transform — hand rotations must compose with it (Rotation * base).
    virtual void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                           const SYSTEMTIME& st, bool seconds,
                           const D2D1_MATRIX_3X2_F& base) const = 0;
    // Options this face exposes in the configure dialog. Not pure: a face
    // with nothing to configure says nothing.
    virtual int GetOptions(const FaceOpt** out) const { (void)out; return 0; }
};

// Shared hand geometry: also used directly by the engine for image faces.
void DrawClassicHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                      const SYSTEMTIME& st, bool seconds,
                      const D2D1_MATRIX_3X2_F& base,
                      const D2D1_COLOR_F& handCol, const D2D1_COLOR_F& hubCol,
                      const D2D1_COLOR_F& secCol);

// Defined by the engine, not by faces.cpp. The second hand is a menu toggle
// rather than a face option (it is one flag for every face), but the two
// Digital faces share one slot between the seconds and AM/PM, so their *panel*
// has to know. Toggling it rebuilds the dial (CMD_SECONDS), which is what makes
// it safe to read from DrawDial.
bool ShowSeconds();

extern const D2D1_COLOR_F DARK_HAND;            // image-face hand/hub color
extern const IClockFace* const g_builtinFaces[];
extern const int NUM_BUILTIN;

// ------------------------------------------------------------------
// Shared text helpers (faces.cpp). The DirectWrite boilerplate every text
// face needs, in one place; each face still owns the formats it builds from
// these and releases them on its own terms.
// ------------------------------------------------------------------

// The one DirectWrite factory in the process, created on first use and held
// for the lifetime — do NOT release what this returns. Null if creation
// failed; every caller must cope, since a face that cannot get a factory
// still has to draw or fail cleanly.
//
// It is DWRITE_FACTORY_TYPE_SHARED on purpose: an isolated factory carries a
// font cache of its own, so a factory per face would keep one cache resident
// per text face and make the dial-path faces re-enumerate the font collection
// on every rebuild. Shared hands all of them the process-wide cache.
IDWriteFactory* SharedDWrite();

// Paragraph alignment is always CENTER (what every face uses); `align`
// picks horizontal LEADING vs CENTER. Returns null on failure.
IDWriteTextFormat* MakeTextFormat(IDWriteFactory* dw, const WCHAR* family,
                                  float size, DWRITE_FONT_WEIGHT weight,
                                  DWRITE_TEXT_ALIGNMENT align);

// DrawTextW with the length worked out; a no-op when fmt is null, so
// callers don't each guard a failed format.
void DrawTextIn(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                IDWriteTextFormat* fmt, const WCHAR* s, const D2D1_RECT_F& r);

// A layout with uniform character spacing — plain DrawText cannot track.
// An allocation, so callers cache it and rebuild only when the string
// changes. Spacing is split either side of each glyph so a centred line
// stays centred.
IDWriteTextLayout* MakeTrackedLayout(IDWriteFactory* dw, const WCHAR* s,
                                     IDWriteTextFormat* fmt,
                                     float w, float h, float track);

// Draw `s` one glyph per fixed-width cell, first cell's left edge at `x`.
// `fmt` must be centre-aligned: each glyph is centred in its own cell, so
// the run's width depends only on how many characters it has. That is what
// keeps a clock still — centre a proportional "17:49" as one string and it
// shifts sideways whenever a digit's width changes, which reads as a twitch
// every minute. Cells are the caller's to size; make them wider than the
// font's widest digit and nothing can overlap. No measuring, no layout, no
// allocation — the cheap half of what tabular-figure typography would buy.
void DrawCells(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
               IDWriteTextFormat* fmt, const WCHAR* s, float cell,
               float x, float y0, float y1);

// Vertical linear-gradient ellipse fill, `top` to `bottom` across the
// ellipse's own height. The brushes live only for this call.
void FillEllipseVGrad(ID2D1RenderTarget* rt, const D2D1_ELLIPSE& e,
                      const D2D1_COLOR_F& top, const D2D1_COLOR_F& bottom);

// Flood the 240-DIP square with the faintest pixel that still hit-tests, for
// a face whose art doesn't cover it (the text faces, Dots, Split). Call it
// first in DrawDial: UpdateLayeredWindow makes any pixel with alpha 0
// click-through, so without it only the inked glyphs could be dragged or
// right-clicked and the rest of the window belongs to the desktop.
void FillHitTestWash(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b);

// Month and weekday names in the user's regional language, uppercased. Filled
// once at startup by LocInitDateNames() (loc.cpp) from LOCALE_S[ABBREV]MONTHNAME
// / S[ABBREV]DAYNAME — so a face indexes them exactly as it did when they were
// English literals, and gets the right language for free.
//
// The pointers are non-const because they are assigned at startup; what they
// point at is not to be written. Both weekday tables are indexed by
// SYSTEMTIME::wDayOfWeek, where **0 is Sunday** — which is not how Windows
// numbers LOCALE_SDAYNAME1 (Monday), and LocInitDateNames rotates for it.
//
// Read only after LocInitDateNames(); before it they are null, so nothing may
// touch them from a static initialiser.
extern const WCHAR* MONTH_ABBR[12];             // "JAN".."DEC" in the user's language
extern const WCHAR* DAY_ABBR[7];                // "SUN".."SAT", 0 = Sunday
extern const WCHAR* MONTH_FULL[12];             // "JANUARY".."DECEMBER"
extern const WCHAR* DAY_FULL[7];                // "SUNDAY".."SATURDAY", 0 = Sunday

// The locale's time separator — ":" nearly everywhere, "." in fi-FI. Never
// null and never empty; a face draws it into the fixed slot it already sized
// for a colon, since every separator Windows reports is one narrow glyph.
const WCHAR* LocTimeSep();

// The locale's AM and PM designators, uppercased — "AM"/"PM", 午前/午後, ص/م.
// Never null, but a 24-hour locale legitimately has none and answers "", which
// draws nothing.
const WCHAR* LocAmPm(bool pm);

// The date written the way the user's region writes it, uppercased. This is the
// one thing the tables above cannot give you: field *order*. ja-JP writes
// 3月2日 and hu-HU writes 2026. 03. 02., neither of which any "%s %s %d"
// reaches, whatever language its names are in.
//
// Only for a face that *measures* its date line — the string's width varies by
// locale far more than an English one does, so a fixed-width slot should keep
// using the abbreviated tables instead (see face_digital.cpp). Returns `out`,
// so it can be called straight into a draw.
const WCHAR* LocDate(const SYSTEMTIME& st, bool longForm, WCHAR* out, size_t cch);

// Image face (face_image.cpp): dial from a PNG/BMP in the assets folder.
// One mutable instance, retargeted by SetImageFacePath — deliberately not
// in g_builtinFaces; the engine selects it when the user picks a file.
extern const IClockFace* const g_faceImage;
void SetImageFacePath(const WCHAR* dir, const WCHAR* file);
