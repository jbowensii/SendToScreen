#include "sendtoscreen.h"
#include <shellapi.h>
#include <cstdio>

static const UINT WM_TRAY = WM_APP + 1;
static const UINT WM_TASKBAR_CLICK = WM_APP + 2;
static UINT WM_TASKBARCREATED;
static HWND g_hwnd;
static HHOOK g_hook;
static NOTIFYICONDATAW g_nid;

void Log(const std::wstring& line) {
    OutputDebugStringW((line + L"\n").c_str());
    static const std::wstring path = [] {
        wchar_t dir[MAX_PATH];
        return std::wstring(GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH) ? dir : L".") + L"\\SendToScreen.log";
    }();
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > (1U << 20)) DeleteFileW(path.c_str());
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"a, ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"%s\n", line.c_str());
        fclose(f);
    }
}

// Rect test only: nothing here may send a message to another process (a hung app would stall the hook).
static bool OverTaskbar(POINT pt) {
    for (const wchar_t* cls : { L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd" }) {
        for (HWND h = FindWindowExW(nullptr, nullptr, cls, nullptr); h; h = FindWindowExW(nullptr, h, cls, nullptr)) {
            RECT r;
            if (GetWindowRect(h, &r) && PtInRect(&r, pt)) return true;
        }
    }
    return false;
}

// Runs on this thread for every mouse event; only Ctrl + right button does any work.
static LRESULT CALLBACK MouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && (wp == WM_RBUTTONDOWN || wp == WM_RBUTTONUP)) {
        static bool down = false;
        const POINT pt = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp)->pt;
        if (wp == WM_RBUTTONDOWN) {
            down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && OverTaskbar(pt);
            if (down) return 1;
        } else if (down) {
            down = false;
            if (OverTaskbar(pt)) PostMessageW(g_hwnd, WM_TASKBAR_CLICK, static_cast<WPARAM>(pt.x), static_cast<LPARAM>(pt.y));
            return 1;
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

static int ShowMonitorMenu(const std::vector<MonitorInfo>& mons, HMONITOR current, POINT pt) {
    const HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < mons.size(); ++i) {
        const MonitorInfo& m = mons[i];
        wchar_t label[96];
        swprintf_s(label, L"Screen %zu     %ld x %ld%s", i + 1, m.full.right - m.full.left, m.full.bottom - m.full.top, m.primary ? L"  (primary)" : L"");
        AppendMenuW(menu, MF_STRING | (m.h == current ? MF_GRAYED : 0), i + 1, label);
    }
    // The hook swallowed the click, so we are not the last input receiver; borrow the foreground thread's queue.
    if (!SetForegroundWindow(g_hwnd)) {
        const HWND fg = GetForegroundWindow();
        const DWORD other = GetWindowThreadProcessId(fg, nullptr);
        const DWORD me = GetCurrentThreadId();
        if (other && other != me && !IsHungAppWindow(fg) && AttachThreadInput(other, me, TRUE)) {
            SetForegroundWindow(g_hwnd);
            AttachThreadInput(other, me, FALSE);
        }
    }
    const int pick = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return pick;
}

// Release private pages and unlock the system DLLs a gesture pulled in; they fault back in on the next one.
static void TrimWorkingSet() {
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

static void OnTaskbarClick(POINT pt) {
    struct Com { Com() { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); } ~Com() { CoUninitialize(); TrimWorkingSet(); } } com;
    std::wstring id;
    std::wstring name;
    if (!ButtonAtPoint(pt, id, name)) return;
    const std::vector<HWND> wins = MatchWindows(id, name, TaskbarWindows());
    Log(L"button id=" + id + L" name=" + name + L" matched=" + std::to_wstring(wins.size()));
    const std::vector<MonitorInfo> mons = Monitors();
    if (wins.empty() || mons.size() < 2) return;

    const int pick = ShowMonitorMenu(mons, MonitorFromWindow(wins[0], MONITOR_DEFAULTTONEAREST), pt);
    Log(L"pick=" + std::to_wstring(pick));
    if (pick <= 0 || static_cast<size_t>(pick) > mons.size()) return;

    for (auto it = wins.rbegin(); it != wins.rend(); ++it) {
        MoveWindowToMonitor(*it, mons[pick - 1]);
        SetWindowPos(*it, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    SetForegroundWindow(wins[0]);
}

static void ShowTrayMenu(HWND h) {
    const HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"Exit SendToScreen");
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(h);
    const int pick = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, h, nullptr);
    PostMessageW(h, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (pick == 1) DestroyWindow(h);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    static bool busy = false;
    if (msg == WM_TASKBARCREATED) {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        return 0;
    }
    switch (msg) {
    case WM_TASKBAR_CLICK:
        if (!busy) {
            busy = true;
            OnTaskbarClick({ static_cast<int>(wp), static_cast<int>(lp) });
            busy = false;
        }
        return 0;
    case WM_TRAY:
        if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP) ShowTrayMenu(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(h, msg, wp, lp);
    }
}

int WINAPI wWinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ PWSTR cmd, _In_ int) {
    if (cmd && wcsstr(cmd, L"--dump")) {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
        DumpWindows();
        return 0;
    }

    const HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SendToScreen.SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"SendToScreenWnd";
    RegisterClassW(&wc);
    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"SendToScreen", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);

    g_hook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, hInst, 0);
    if (!g_hook) {
        MessageBoxW(nullptr, L"Failed to install the mouse hook.", L"SendToScreen", MB_ICONERROR);
        return 1;
    }

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wcscpy_s(g_nid.szTip, L"SendToScreen: Ctrl+right-click a taskbar button");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    TrimWorkingSet();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_hook);
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    CloseHandle(mutex);
    return 0;
}
