#include "rules.h"
#include "sendtoscreen.h"
#include <commctrl.h>
#include <objbase.h>
#include <vector>
#include <algorithm>

struct RunningApp {
    HWND hwnd;
    std::wstring title;
    std::wstring exe;
    std::wstring detail;
};

static std::vector<RunningApp> g_apps;
static std::vector<PlacementRule> g_dlgRules;

enum {
    IDC_APP_LIST = 100, IDC_REMEMBER, IDC_RULE_LIST, IDC_REMOVE,
    IDC_FRONT, IDC_BACK, IDC_APPLY
};

static int Dpi(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(GetModuleHandleW(L"user32"), "GetDpiForWindow"));
    return fn ? static_cast<int>(fn(hwnd)) : 96;
}

static int Scale(int val, int dpi) { return MulDiv(val, dpi, 96); }

static std::wstring WindowDetail(HWND hwnd) {
    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    std::vector<MonitorInfo> mons = Monitors();
    int monIdx = 1;
    RECT monWork{};
    for (size_t i = 0; i < mons.size(); ++i) {
        if (mons[i].h == hmon) { monIdx = static_cast<int>(i) + 1; monWork = mons[i].work; break; }
    }
    WINDOWPLACEMENT wp{ sizeof(wp) };
    GetWindowPlacement(hwnd, &wp);
    wchar_t buf[128];
    if (wp.showCmd == SW_SHOWMAXIMIZED)
        swprintf_s(buf, L"Screen %d, maximized", monIdx);
    else if (wp.showCmd == SW_SHOWMINIMIZED)
        swprintf_s(buf, L"Screen %d, minimized", monIdx);
    else {
        RECT r; GetWindowRect(hwnd, &r);
        swprintf_s(buf, L"Screen %d at %ld, %ld", monIdx, r.left - monWork.left, r.top - monWork.top);
    }
    return buf;
}

static std::vector<RunningApp> GetRunningApps() {
    std::vector<RunningApp> apps;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::vector<WinInfo> wins = TaskbarWindows();
    CoUninitialize();

    std::vector<std::wstring> seen;
    for (const WinInfo& w : wins) {
        std::wstring exeLower = w.exe;
        for (auto& c : exeLower) c = towlower(c);
        if (std::find(seen.begin(), seen.end(), exeLower) != seen.end()) continue;
        seen.push_back(exeLower);

        std::wstring exeName = w.exe;
        const size_t sep = exeName.find_last_of(L"\\/");
        if (sep != std::wstring::npos) exeName = exeName.substr(sep + 1);
        if (exeName.empty()) exeName = w.title;

        std::wstring detail = WindowDetail(w.h);
        std::wstring display = exeName + L"  -  " + (w.title.empty() ? L"(untitled)" : w.title);
        apps.push_back({ w.h, display, exeName, detail });
    }
    std::sort(apps.begin(), apps.end(), [](const RunningApp& a, const RunningApp& b) {
        return _wcsicmp(a.exe.c_str(), b.exe.c_str()) < 0;
    });
    return apps;
}

static std::wstring RuleSummary(const PlacementRule& r) {
    wchar_t line[256];
    const wchar_t* zOrder = r.bringToFront ? L"front" : L"back";
    if (r.state == PlacementRule::Maximized)
        swprintf_s(line, L"%s  ->  Screen %d, maximized, %s", r.exe.c_str(), r.monitor, zOrder);
    else if (r.state == PlacementRule::Minimized)
        swprintf_s(line, L"%s  ->  Screen %d, minimized, %s", r.exe.c_str(), r.monitor, zOrder);
    else
        swprintf_s(line, L"%s  ->  Screen %d at %d, %d  (%dx%d), %s", r.exe.c_str(), r.monitor, r.x, r.y, r.w, r.h, zOrder);
    return line;
}

static void RefreshRuleList(HWND list) {
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const PlacementRule& r : g_dlgRules)
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(RuleSummary(r).c_str()));
}

static void RefreshAppList(HWND list) {
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const RunningApp& a : g_apps) {
        std::wstring line = a.title + L"     [" + a.detail + L"]";
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
}

static HFONT CreateUIFont(int size, int dpi, bool bold = false) {
    return CreateFontW(-Scale(size, dpi), 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

// Update z-order radio buttons to match the selected rule
static void SyncZOrderToSelection(HWND hwnd) {
    const int sel = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_RULE_LIST, LB_GETCURSEL, 0, 0));
    if (sel >= 0 && static_cast<size_t>(sel) < g_dlgRules.size()) {
        CheckDlgButton(hwnd, IDC_FRONT, g_dlgRules[sel].bringToFront ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_BACK, g_dlgRules[sel].bringToFront ? BST_UNCHECKED : BST_CHECKED);
    }
}

static LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HFONT hFontNormal, hFontBold;

    switch (msg) {
    case WM_CREATE: {
        const int dpi = Dpi(hwnd);
        hFontNormal = CreateUIFont(13, dpi);
        hFontBold = CreateUIFont(13, dpi, true);

        const int m = Scale(16, dpi);
        const int bw = Scale(170, dpi), bh = Scale(32, dpi);
        const int listW = Scale(580, dpi);
        const int appListH = Scale(200, dpi);
        const int ruleListH = Scale(180, dpi);
        const int lblH = Scale(22, dpi);
        const int radioH = Scale(22, dpi);
        const int groupH = Scale(76, dpi);
        const int gap = Scale(8, dpi);

        // -- Section 1: Running Applications --
        const int btnX = m + listW + m;

        HWND lbl1 = CreateWindowExW(0, L"STATIC", L"Running Applications",
            WS_CHILD | WS_VISIBLE, m, m, Scale(300, dpi), lblH, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(lbl1, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);

        CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY | WS_TABSTOP,
            m, m + lblH + gap, listW, appListH, hwnd, reinterpret_cast<HMENU>(IDC_APP_LIST), nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Remember Placement",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, btnX, m + lblH + gap, bw, bh,
            hwnd, reinterpret_cast<HMENU>(IDC_REMEMBER), nullptr, nullptr);

        // -- Section 2: Saved Rules --
        const int sec2Top = m + lblH + gap + appListH + m;
        HWND lbl2 = CreateWindowExW(0, L"STATIC", L"Saved Placement Rules",
            WS_CHILD | WS_VISIBLE, m, sec2Top, Scale(300, dpi), lblH, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(lbl2, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);

        CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY | WS_TABSTOP,
            m, sec2Top + lblH + gap, listW, ruleListH, hwnd, reinterpret_cast<HMENU>(IDC_RULE_LIST), nullptr, nullptr);

        // Right column for rules section: Remove, Z-Order group, Apply
        int rightY = sec2Top + lblH + gap;
        CreateWindowExW(0, L"BUTTON", L"Remove Selected",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, btnX, rightY, bw, bh,
            hwnd, reinterpret_cast<HMENU>(IDC_REMOVE), nullptr, nullptr);
        rightY += bh + m;

        // Z-Order group box with radios
        CreateWindowExW(0, L"BUTTON", L"Z-Order",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, btnX, rightY, bw, groupH,
            hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Front (top)",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
            btnX + Scale(12, dpi), rightY + Scale(22, dpi), bw - Scale(24, dpi), radioH,
            hwnd, reinterpret_cast<HMENU>(IDC_FRONT), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Back (bottom)",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            btnX + Scale(12, dpi), rightY + Scale(46, dpi), bw - Scale(24, dpi), radioH,
            hwnd, reinterpret_cast<HMENU>(IDC_BACK), nullptr, nullptr);
        CheckDlgButton(hwnd, IDC_FRONT, BST_CHECKED);
        rightY += groupH + m;

        // Apply button — always lower-right of the dialog
        // We'll position it in WM_SIZE or just calculate from window client rect
        // For now, put it at bottom of right column
        const int totalH = sec2Top + lblH + gap + ruleListH + m;
        CreateWindowExW(0, L"BUTTON", L"Apply",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, btnX, totalH - bh, bw, bh,
            hwnd, reinterpret_cast<HMENU>(IDC_APPLY), nullptr, nullptr);

        // Populate
        g_apps = GetRunningApps();
        RefreshAppList(GetDlgItem(hwnd, IDC_APP_LIST));
        g_dlgRules = LoadRules();
        RefreshRuleList(GetDlgItem(hwnd, IDC_RULE_LIST));

        // Set normal font on all children that don't already have bold
        EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
            if (!SendMessageW(child, WM_GETFONT, 0, 0))
                SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(hFontNormal));
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_APP_LIST:
            // No z-order sync needed for app list
            break;
        case IDC_RULE_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) SyncZOrderToSelection(hwnd);
            break;
        case IDC_REMEMBER: {
            const int sel = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_APP_LIST, LB_GETCURSEL, 0, 0));
            if (sel < 0 || static_cast<size_t>(sel) >= g_apps.size()) {
                MessageBoxW(hwnd, L"Select a running application first.", L"SendToScreen", MB_ICONINFORMATION);
                break;
            }
            PlacementRule rule = SnapshotWindow(g_apps[sel].hwnd);
            if (rule.exe.empty()) {
                MessageBoxW(hwnd, L"Could not read that window's placement.", L"SendToScreen", MB_ICONWARNING);
                break;
            }
            // Default z-order for new rules: front
            rule.bringToFront = true;

            bool replaced = false;
            for (PlacementRule& r : g_dlgRules) {
                if (_wcsicmp(r.exe.c_str(), rule.exe.c_str()) == 0) { r = rule; replaced = true; break; }
            }
            if (!replaced) g_dlgRules.push_back(rule);
            SaveRules(g_dlgRules);
            ReloadPlacementRules();
            RefreshRuleList(GetDlgItem(hwnd, IDC_RULE_LIST));
            break;
        }
        case IDC_REMOVE: {
            const int sel = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_RULE_LIST, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && static_cast<size_t>(sel) < g_dlgRules.size()) {
                g_dlgRules.erase(g_dlgRules.begin() + sel);
                SaveRules(g_dlgRules);
                ReloadPlacementRules();
                RefreshRuleList(GetDlgItem(hwnd, IDC_RULE_LIST));
            }
            break;
        }
        case IDC_FRONT:
        case IDC_BACK: {
            // Update the selected rule's z-order immediately
            const int sel = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_RULE_LIST, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && static_cast<size_t>(sel) < g_dlgRules.size()) {
                g_dlgRules[sel].bringToFront = (IsDlgButtonChecked(hwnd, IDC_FRONT) == BST_CHECKED);
                SaveRules(g_dlgRules);
                ReloadPlacementRules();
                RefreshRuleList(GetDlgItem(hwnd, IDC_RULE_LIST));
                SendDlgItemMessageW(hwnd, IDC_RULE_LIST, LB_SETCURSEL, sel, 0);
            }
            break;
        }
        case IDC_APPLY:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (hFontNormal) { DeleteObject(hFontNormal); hFontNormal = nullptr; }
        if (hFontBold) { DeleteObject(hFontBold); hFontBold = nullptr; }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void ShowRulesDialog(HINSTANCE hInst) {

    // Enable per-monitor DPI awareness for this dialog
    using SetThreadDpiAwarenessContextFn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
    static auto setDpi = reinterpret_cast<SetThreadDpiAwarenessContextFn>(
        GetProcAddress(GetModuleHandleW(L"user32"), "SetThreadDpiAwarenessContext"));
    DPI_AWARENESS_CONTEXT oldCtx = nullptr;
    if (setDpi) oldCtx = setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DlgProc;
        wc.hInstance = hInst;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SendToScreenRulesWnd";
        if (!RegisterClassW(&wc)) {
            Log(L"RegisterClassW failed: " + std::to_wstring(GetLastError()));
            if (setDpi && oldCtx) setDpi(oldCtx);
            return;
        }
        registered = true;
    }

    HWND existing = FindWindowW(L"SendToScreenRulesWnd", nullptr);
    if (existing) { SetForegroundWindow(existing); if (setDpi && oldCtx) setDpi(oldCtx); return; }

    // Query primary monitor DPI to scale window size
    using GetDpiForSystemFn = UINT(WINAPI*)();
    static auto getDpiSys = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(GetModuleHandleW(L"user32"), "GetDpiForSystem"));
    const int sysDpi = getDpiSys ? static_cast<int>(getDpiSys()) : 96;
    const int w = MulDiv(870, sysDpi, 96), h = MulDiv(560, sysDpi, 96);
    HWND dlg = CreateWindowExW(0, L"SendToScreenRulesWnd", L"SendToScreen - Placement Rules",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, hInst, nullptr);
    if (!dlg) {
        Log(L"CreateWindowExW failed: " + std::to_wstring(GetLastError()));
        if (setDpi && oldCtx) setDpi(oldCtx);
        return;
    }
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    if (setDpi && oldCtx) setDpi(oldCtx);
}
