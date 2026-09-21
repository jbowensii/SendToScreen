#include "sendtoscreen.h"
#include <gdiplus.h>

static ULONG_PTR g_gdip;

static HWND NumberWindow(const MonitorInfo& m, int number) {
    using namespace Gdiplus;
    const int side = (m.full.bottom - m.full.top) / 4;
    const REAL d = side / 4.0f;
    Bitmap bmp(side, side, PixelFormat32bppPARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    GraphicsPath box;
    box.AddArc(0.0f, 0.0f, d, d, 180, 90);
    box.AddArc(side - d, 0.0f, d, d, 270, 90);
    box.AddArc(side - d, side - d, d, d, 0, 90);
    box.AddArc(0.0f, side - d, d, d, 90, 90);
    box.CloseFigure();
    SolidBrush dark(Color(178, 0, 0, 0));
    g.FillPath(&dark, &box);

    const wchar_t* face = FontFamily(L"Segoe UI Variable Display").IsAvailable() ? L"Segoe UI Variable Display" : L"Segoe UI";
    Font font(face, side * 0.6f, FontStyleRegular, UnitPixel);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    wchar_t text[8];
    swprintf_s(text, L"%d", number);
    SolidBrush white(Color(255, 255, 255, 255));
    g.DrawString(text, -1, &font, RectF(0, 0, static_cast<REAL>(side), static_cast<REAL>(side)), &fmt, &white);

    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), side, -side, 1, 32, BI_RGB };
    void* bits = nullptr;
    const HDC dc = CreateCompatibleDC(nullptr);
    const HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    const HGDIOBJ old = SelectObject(dc, dib);
    BitmapData data{};
    data.Width = side;
    data.Height = side;
    data.Stride = side * 4;
    data.PixelFormat = PixelFormat32bppPARGB;
    data.Scan0 = bits;
    Rect rc(0, 0, side, side);
    bmp.LockBits(&rc, ImageLockModeRead | ImageLockModeUserInputBuf, PixelFormat32bppPARGB, &data);
    bmp.UnlockBits(&data);

    const HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        L"SendToScreenNumber", nullptr, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    POINT dst{ (m.full.left + m.full.right - side) / 2, (m.full.top + m.full.bottom - side) / 2 };
    SIZE sz{ side, side };
    POINT src{};
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(h, nullptr, &dst, &sz, dc, &src, 0, &blend, ULW_ALPHA);
    SelectObject(dc, old);
    DeleteObject(dib);
    DeleteDC(dc);
    ShowWindow(h, SW_SHOWNOACTIVATE);
    return h;
}

std::vector<HWND> ShowScreenNumbers(const std::vector<MonitorInfo>& mons) {
    static const bool registered = [] {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"SendToScreenNumber";
        return RegisterClassW(&wc) != 0;
    }();
    Gdiplus::GdiplusStartupInput in;
    if (!registered || Gdiplus::GdiplusStartup(&g_gdip, &in, nullptr) != Gdiplus::Ok) return {};
    std::vector<HWND> out;
    for (size_t i = 0; i < mons.size(); ++i) out.push_back(NumberWindow(mons[i], static_cast<int>(i) + 1));
    return out;
}

void HideScreenNumbers(const std::vector<HWND>& wins) {
    for (HWND h : wins) DestroyWindow(h);
    if (!wins.empty()) Gdiplus::GdiplusShutdown(g_gdip);
}
