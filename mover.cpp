#include "sendtoscreen.h"
#include <algorithm>

std::vector<MonitorInfo> Monitors() {
    std::vector<MonitorInfo> v;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR h, HDC, LPRECT, LPARAM lp) -> BOOL {
        MONITORINFO mi{ sizeof(mi) };
        if (GetMonitorInfoW(h, &mi)) {
            reinterpret_cast<std::vector<MonitorInfo>*>(lp)->push_back({ h, mi.rcWork, mi.rcMonitor, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0 });
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&v));
    std::sort(v.begin(), v.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        return a.full.left != b.full.left ? a.full.left < b.full.left : a.full.top < b.full.top;
    });
    return v;
}

// Restore, move in screen coordinates, re-maximize: avoids rcNormalPosition's workspace-coordinate rules.
void MoveWindowToMonitor(HWND hwnd, const MonitorInfo& target) {
    WINDOWPLACEMENT wp{ sizeof(wp) };
    GetWindowPlacement(hwnd, &wp);
    if (wp.showCmd == SW_SHOWMINIMIZED) { ShowWindow(hwnd, SW_RESTORE); GetWindowPlacement(hwnd, &wp); }
    const bool wasMax = wp.showCmd == SW_SHOWMAXIMIZED;
    if (wasMax) ShowWindow(hwnd, SW_RESTORE);

    MONITORINFO src{ sizeof(src) };
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &src);
    RECT r{};
    GetWindowRect(hwnd, &r);
    RECT t = TranslateRect(r, src.rcWork, target.work);
    SetWindowPos(hwnd, nullptr, t.left, t.top, t.right - t.left, t.bottom - t.top, SWP_NOZORDER | SWP_NOACTIVATE);
    // A DPI-aware app rescales itself on WM_DPICHANGED during that call; clamp once more so it stays on screen.
    GetWindowRect(hwnd, &r);
    t = TranslateRect(r, target.work, target.work);
    SetWindowPos(hwnd, nullptr, t.left, t.top, t.right - t.left, t.bottom - t.top, SWP_NOZORDER | SWP_NOACTIVATE);
    if (wasMax) ShowWindow(hwnd, SW_MAXIMIZE);
}
