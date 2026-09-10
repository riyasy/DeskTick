// face_terminal.cpp — "Terminal": Consolas on near-black, phosphor green, one
// prompt line over the time, with a block cursor.
//
// The cursor is the only interesting part. Its x has to follow the string,
// which is 5 chars in minute mode and 8 in second mode — but measuring text
// means an IDWriteTextLayout allocation per frame. Consolas is monospaced with
// an advance of exactly 1126/2048 em, so the position is arithmetic instead:
// left margin + chars * size * 0.55. Costs nothing and cannot drift.
// That only holds while the font really is Consolas. It has shipped with
// Windows since Vista; if DirectWrite ever falls back, the cursor lands wrong
// (nothing else breaks) and this needs GetMetrics on a cached layout.
//
// Blinking is bounded by the tick: the engine only repaints on a real
// wall-clock boundary, so the cursor can blink at 1 Hz in second mode and
// nothing at all in minute mode, where it stays solid.

#include "faces.h"
#include <dwrite.h>
#include <strsafe.h>

static const float PX0 = 8.0f,  PX1 = DIAL - 8.0f;    // panel
static const float PY0 = 64.0f, PY1 = 176.0f;
static const float TX  = 22.0f;                       // text left margin
static const float TIME_SIZE = 30.0f;
static const float ADVANCE   = 0.55f;                 // Consolas em advance

// Lower case on purpose — the shared DAY_ABBR is upper, which would fight the
// shell-output look this face is built around. It used to be a private English
// table; now it is DAY_ABBR lowered, so it follows the user's region like every
// other date name, and a script with no case (CJK, Malayalam, Arabic) simply
// passes through CharLowerW unchanged.
//
// Filled on first draw rather than at startup: this TU has nowhere to be called
// from, and by the time anything draws, LocInitDateNames() has long since run.
static const WCHAR* DayLower(int dow)
{
    static WCHAR s_days[7][32];
    static bool  s_done;
    if (!s_done) {
        for (int i = 0; i < 7; i++) {
            lstrcpynW(s_days[i], DAY_ABBR[i], _countof(s_days[i]));
            CharLowerW(s_days[i]);
        }
        s_done = true;
    }
    return s_days[dow];
}

// ---- options ----
// The prompt is a choice, not free text, so every option value stays an int.
// Each is drawn verbatim as the prompt line, so all three carry the command:
// the face reads as `time` typed at a shell, with the clock as its output.
static const WCHAR* const PROMPTS[] = { L"$ time", L"> time", L"~/clock $ time", nullptr };

static int o_phosphor = RGB(127, 231, 135);
static int o_prompt   = 0;
static int o_hour24   = 1;
static int o_date     = 1;
static int o_blink    = 1;

static const FaceOpt s_opts[] = {
    { L"Phosphor colour", OPT_COLOR,  &o_phosphor, nullptr  },
    { L"Prompt",          OPT_CHOICE, &o_prompt,   PROMPTS  },
    { L"24-hour clock",   OPT_BOOL,   &o_hour24,   nullptr  },
    { L"Show date",       OPT_BOOL,   &o_date,     nullptr  },
    { L"Blinking cursor", OPT_BOOL,   &o_blink,    nullptr  },
};

class TerminalFace : public IClockFace {
    mutable IDWriteFactory*    m_dw     = nullptr;    // lazy, cached forever
    mutable IDWriteTextFormat* m_fTime  = nullptr;    // Consolas 30
    mutable IDWriteTextFormat* m_fSmall = nullptr;    // Consolas 10

    // One Consolas format at this size, left-aligned: every line on this face
    // starts at the same margin.
    IDWriteTextFormat* MakeFormat(float size) const
    {
        return MakeTextFormat(m_dw, L"Consolas", size, DWRITE_FONT_WEIGHT_NORMAL,
                              DWRITE_TEXT_ALIGNMENT_LEADING);
    }

public:
    // Menu label, and the INI section the options above are saved under.
    const WCHAR* GetName() const override { return L"Terminal"; }

    // Static art: the near-black panel and its phosphor hairline border.
    bool DrawDial(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) const override
    {
        D2D1_ROUNDED_RECT rr =
            D2D1::RoundedRect(D2D1::RectF(PX0, PY0, PX1, PY1), 3.0f, 3.0f);
        b->SetColor(D2D1::ColorF(0.024f, 0.043f, 0.031f, 0.74f));
        rt->FillRoundedRectangle(rr, b);
        b->SetColor(FromRGB(o_phosphor, 0.26f));
        rt->DrawRoundedRectangle(rr, b, 1.0f);
        return true;
    }

    // The five knobs above, in the order the dialog lays them out.
    int GetOptions(const FaceOpt** out) const override
    {
        *out = s_opts;
        return _countof(s_opts);
    }

    // Prompt line, the time under it, the cursor after the time, and the date
    // below. Builds the two formats on first call.
    void DrawHands(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b,
                   const SYSTEMTIME& st, bool seconds,
                   const D2D1_MATRIX_3X2_F& base) const override
    {
        if (!m_dw) {
            m_dw = SharedDWrite();
            if (!m_dw) return;
            m_fTime  = MakeFormat(TIME_SIZE);
            m_fSmall = MakeFormat(10.0f);
        }

        rt->SetTransform(base);
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        WCHAR buf[32];

        b->SetColor(FromRGB(o_phosphor, 0.42f));
        DrawTextIn(rt, b, m_fSmall, PROMPTS[o_prompt], D2D1::RectF(TX, 79, PX1, 91));

        int h = o_hour24 ? st.wHour : (st.wHour % 12 ? st.wHour % 12 : 12);
        // The separator is the locale's, so this is no longer a format of ours
        // with a known width — StringCchPrintfW, and the length read back rather
        // than returned. It stays one Consolas cell per character either way,
        // which is what the cursor arithmetic below is counting.
        const WCHAR* sep = LocTimeSep();
        if (seconds) StringCchPrintfW(buf, _countof(buf), L"%02d%s%02d%s%02d",
                                      h, sep, st.wMinute, sep, st.wSecond);
        else         StringCchPrintfW(buf, _countof(buf), L"%02d%s%02d",
                                      h, sep, st.wMinute);
        int len = lstrlenW(buf);
        b->SetColor(FromRGB(o_phosphor));
        DrawTextIn(rt, b, m_fTime, buf, D2D1::RectF(TX, 97, PX1, 135));

        // Solid in minute mode; 1 Hz in second mode, which is exactly the
        // rate the engine repaints at.
        if (!o_blink || !seconds || (st.wSecond & 1) == 0) {
            float cx = TX + (float)len * TIME_SIZE * ADVANCE + 7.0f;
            b->SetColor(FromRGB(o_phosphor, 0.85f));
            rt->FillRectangle(D2D1::RectF(cx, 104, cx + 7.5f, 128), b);
        }

        if (o_date) {
            // The ISO date stays ISO in every locale, deliberately: it is what
            // a shell prints, and this face is the one place a locale-ordered
            // date would look wrong rather than right. Only the weekday is
            // translated.
            // StringCchPrintfW rather than wsprintfW: the weekday is now a
            // string from the OS rather than one of ours, so its length is no
            // longer something this buffer's size was chosen against.
            StringCchPrintfW(buf, _countof(buf), L"%s %04d-%02d-%02d",
                             DayLower(st.wDayOfWeek % 7),
                             st.wYear, st.wMonth, st.wDay);
            b->SetColor(FromRGB(o_phosphor, 0.40f));
            DrawTextIn(rt, b, m_fSmall, buf, D2D1::RectF(TX, 146, PX1, 158));
        }
    }
};

static const TerminalFace s_terminal;
extern const IClockFace* const g_faceTerminal = &s_terminal;
