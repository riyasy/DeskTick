// Native integration regression: render real faces and verify that Windows
// routes transparent margins to a backing window. No user settings are read
// or written. Link against the application's objects except clock.obj.
#include "../src/DeskTick/clock.cpp"
#include <stdio.h>
#include <stdlib.h>
#include <initializer_list>

static void Check(bool ok, const char* message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void SelectFace(const WCHAR* name)
{
    for (int i = 0; i < NUM_BUILTIN; ++i) {
        if (!lstrcmpW(g_builtinFaces[i]->GetName(), name)) {
            g_face = i;
            return;
        }
    }
    Check(false, "face exists");
}

static void SetOption(const WCHAR* key, int value)
{
    const FaceOpt* opts = nullptr;
    const int count = g_builtinFaces[g_face]->GetOptions(&opts);
    for (int i = 0; i < count; ++i) {
        if (!lstrcmpW(opts[i].key, key)) { *opts[i].value = value; return; }
    }
    Check(false, "option exists");
}

static HWND At(float x, float y)
{
    RECT r;
    GetWindowRect(g_hwnd, &r);
    POINT p = { r.left + (LONG)(x * g_winPx / DIAL),
                r.top + (LONG)(y * g_winPx / DIAL) };
    return WindowFromPoint(p);
}

int main()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LocInit(nullptr);
    LocInitDateNames();
    Check(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_factory)), "D2D factory");
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DeskTickHitTestRegression";
    RegisterClassW(&wc);
    HWND backing = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"Hit test backing", WS_POPUP, 40, 40, 400, 400,
        nullptr, nullptr, wc.hInstance, nullptr);
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"Hit test clock", WS_POPUP, 40, 40, 240, 240,
        nullptr, nullptr, wc.hInstance, nullptr);
    Check(backing && g_hwnd, "test windows");
    ShowWindow(backing, SW_SHOWNOACTIVATE);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    for (int px : { 96, 240, 360 }) {
        SelectFace(L"Split");
        SetOption(L"Show date", 0);
        Check(CreateResources(px), "Split resources");
        DrawFrame();
        Check(At(120, 30) == backing, "Split upper margin passes clicks");
        Check(At(120, 210) == backing, "Split lower margin passes clicks");
        Check(At(120, 110) == g_hwnd, "Split blocks remain interactive");
        Check(At(8, 58) == backing, "Split rounded corner passes clicks");

        Check(CreateResources(1024), "resize drag buffer");
        g_winPx = px;
        DrawFrame();
        Check(At(120, 30) == backing, "oversized drag buffer preserves transparent margins");
        Check(At(120, 110) == g_hwnd, "oversized drag buffer preserves clock hit testing");
        Check(CreateResources(px), "restore final-sized buffer");

        g_resize = true;
        DrawFrame();
        Check(At(120, 30) == backing, "resize mode leaves upper margin transparent");
        Check(At(218, 148) == g_hwnd, "resize grip is on the Split panel");
        Check(InGrip(MulDiv(px, 218, 240), MulDiv(px, 148, 240)), "tight grip hit test");
        Check(!InGrip(MulDiv(px, 226, 240), MulDiv(px, 226, 240)), "old canvas grip removed");
        g_resize = false;
        SetOption(L"Show date", 1);
        RefreshFace();
        Check(At(120, 180) == g_hwnd, "enabled date line remains interactive");
        Check(At(120, 210) == backing, "space below date passes clicks");

        SelectFace(L"Dots");
        RefreshFace();
        Check(At(30, 30) == backing, "Dots empty corners pass clicks");
        Check(At(120, 120) == g_hwnd, "Dots interior remains draggable");

        SelectFace(L"Classic light");
        RefreshFace();
        Check(At(15, 15) == backing, "Classic empty corners pass clicks");
        Check(At(120, 120) == g_hwnd, "Classic face remains draggable");

        for (const WCHAR* face : { L"Banner", L"Stack" }) {
            SelectFace(face);
            SetOption(L"Show date", 0);
            RefreshFace();
            Check(At(120, 5) == backing, "text face upper margin passes clicks");
            Check(At(120, 235) == backing, "text face lower margin passes clicks");
        }

        for (const WCHAR* face : { L"Digital", L"Digital bold" }) {
            SelectFace(face);
            SetOption(L"Show panel", 0);
            RefreshFace();
            Check(At(120, 20) == backing, "panel-free digital upper margin passes clicks");
            Check(At(120, 220) == backing, "panel-free digital lower margin passes clicks");
            Check(At(120, 120) == g_hwnd, "panel-free digital content remains draggable");
        }
    }
    DestroyWindow(g_hwnd);
    DestroyWindow(backing);
    puts("PASS: native layered-window click routing, face/date/panel changes, resize grip, and three scales");
    return 0;
}
