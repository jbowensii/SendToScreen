#include "rules.h"
#include "sendtoscreen.h"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

static HWINEVENTHOOK g_eventHook;
static std::vector<PlacementRule> g_rules;
static std::unordered_set<HWND> g_placed;

struct Enforcement { HWND hwnd; size_t ruleIdx; int ticksLeft; };
static std::unordered_map<UINT_PTR, Enforcement> g_enforce;

// Pending first-apply: window detected but waiting for the app to finish its own geometry setup
struct PendingApply { HWND hwnd; size_t ruleIdx; };
static std::unordered_map<UINT_PTR, PendingApply> g_pending;

void ReloadPlacementRules() { g_rules = LoadRules(); }

static std::wstring ExeFilename(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return L"";
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return L"";
    wchar_t buf[MAX_PATH * 2];
    DWORD n = _countof(buf);
    std::wstring name;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) {
        name.assign(buf, n);
        const size_t sep = name.find_last_of(L"\\/");
        if (sep != std::wstring::npos) name = name.substr(sep + 1);
    }
    CloseHandle(p);
    return name;
}

// Return bitmask of what drifted: 1=monitor, 2=state, 4=z-order
static int CheckPlacement(HWND hwnd, const PlacementRule& rule, const std::vector<MonitorInfo>& mons) {
    if (!IsWindow(hwnd)) return 0;
    if (rule.monitor < 1 || static_cast<size_t>(rule.monitor) > mons.size()) return 0;
    int drift = 0;

    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (hmon != mons[rule.monitor - 1].h) drift |= 1;

    WINDOWPLACEMENT wp{ sizeof(wp) };
    GetWindowPlacement(hwnd, &wp);
    if (rule.state == PlacementRule::Maximized && wp.showCmd != SW_SHOWMAXIMIZED) drift |= 2;
    if (rule.state == PlacementRule::Minimized && wp.showCmd != SW_SHOWMINIMIZED) drift |= 2;

    if (!rule.bringToFront && GetForegroundWindow() == hwnd) drift |= 4;

    return drift;
}

// SetWindowPlacement combines move+maximize atomically (one redraw).
// Z-order set via SetWindowPos LAST — ShowWindow/SetWindowPlacement always raise the window.
static void ApplyRule(HWND hwnd, const PlacementRule& rule, int drift = 7) {
    if (!IsWindow(hwnd)) return;
    const std::vector<MonitorInfo> mons = Monitors();
    if (rule.monitor < 1 || static_cast<size_t>(rule.monitor) > mons.size()) return;
    const MonitorInfo& target = mons[rule.monitor - 1];

    switch (rule.state) {
    case PlacementRule::Maximized: {
        if (drift & 3) {
            WINDOWPLACEMENT wp{ sizeof(wp) };
            GetWindowPlacement(hwnd, &wp);
            wp.showCmd = SW_SHOWMAXIMIZED;
            wp.rcNormalPosition = { target.work.left, target.work.top,
                                    target.work.left + (target.work.right - target.work.left) / 2,
                                    target.work.top + (target.work.bottom - target.work.top) / 2 };
            SetWindowPlacement(hwnd, &wp);
        }
        if (drift & 4 || drift & 2) {
            const HWND z = rule.bringToFront ? HWND_TOP : HWND_BOTTOM;
            SetWindowPos(hwnd, z, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        break;
    }
    case PlacementRule::Minimized:
        if (drift & 3) {
            MoveWindowToMonitor(hwnd, target);
            ShowWindow(hwnd, SW_MINIMIZE);
        }
        break;
    case PlacementRule::Windowed: {
        if (drift & 3) {
            WINDOWPLACEMENT wp{ sizeof(wp) };
            GetWindowPlacement(hwnd, &wp);
            if (wp.showCmd == SW_SHOWMAXIMIZED || wp.showCmd == SW_SHOWMINIMIZED)
                ShowWindow(hwnd, SW_RESTORE);
        }
        const HWND z = (drift & 4) ? (rule.bringToFront ? HWND_TOP : HWND_BOTTOM) : HWND_NOTOPMOST;
        UINT flags = SWP_NOACTIVATE;
        if (!(drift & 4)) flags |= SWP_NOZORDER;
        if (!(drift & 1)) flags |= SWP_NOMOVE | SWP_NOSIZE;
        SetWindowPos(hwnd, z,
            target.work.left + rule.x, target.work.top + rule.y,
            rule.w, rule.h, flags);
        break;
    }
    }
    Log(L"placement: " + rule.exe + L" mon=" + std::to_wstring(rule.monitor)
        + (rule.bringToFront ? L" front" : L" back"));
}

static void CALLBACK EnforceProc(HWND, UINT, UINT_PTR timerId, DWORD) {
    auto it = g_enforce.find(timerId);
    if (it == g_enforce.end()) { KillTimer(nullptr, timerId); return; }
    Enforcement& e = it->second;
    e.ticksLeft--;
    if (!IsWindow(e.hwnd) || e.ticksLeft <= 0 || e.ruleIdx >= g_rules.size()) {
        KillTimer(nullptr, timerId);
        g_enforce.erase(it);
        return;
    }
    int drift = CheckPlacement(e.hwnd, g_rules[e.ruleIdx], Monitors());
    if (drift) ApplyRule(e.hwnd, g_rules[e.ruleIdx], drift);
}

// Fires 200ms after window detection — lets the app finish its own geometry setup first
static void CALLBACK DelayedApplyProc(HWND, UINT, UINT_PTR timerId, DWORD) {
    KillTimer(nullptr, timerId);
    auto it = g_pending.find(timerId);
    if (it == g_pending.end()) return;
    PendingApply pa = it->second;
    g_pending.erase(it);

    if (!IsWindow(pa.hwnd) || pa.ruleIdx >= g_rules.size()) return;
    ApplyRule(pa.hwnd, g_rules[pa.ruleIdx]);
    UINT_PTR tid = SetTimer(nullptr, 0, 500, EnforceProc);
    if (tid) g_enforce[tid] = { pa.hwnd, pa.ruleIdx, 10 };
}

static void CALLBACK OnWindowShow(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    if (event != EVENT_OBJECT_SHOW || idObject != OBJID_WINDOW || !hwnd) return;
    if (g_rules.empty()) return;
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return;
    if (GetParent(hwnd) || (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return;
    if (g_placed.count(hwnd)) return;

    const std::wstring exe = ExeFilename(hwnd);
    if (exe.empty()) return;

    for (size_t i = 0; i < g_rules.size(); ++i) {
        if (_wcsicmp(g_rules[i].exe.c_str(), exe.c_str()) == 0) {
            g_placed.insert(hwnd);
            // Delay 200ms via timer (not Sleep) so the message pump stays responsive
            UINT_PTR tid = SetTimer(nullptr, 0, 200, DelayedApplyProc);
            if (tid) g_pending[tid] = { hwnd, i };
            return;
        }
    }
}

void StartPlacementHook() {
    ReloadPlacementRules();
    Log(L"placement: " + std::to_wstring(g_rules.size()) + L" rules loaded");
    g_eventHook = SetWinEventHook(
        EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
        nullptr, OnWindowShow, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void StopPlacementHook() {
    if (g_eventHook) { UnhookWinEvent(g_eventHook); g_eventHook = nullptr; }
    for (auto& [tid, _] : g_enforce) KillTimer(nullptr, tid);
    for (auto& [tid, _] : g_pending) KillTimer(nullptr, tid);
    g_enforce.clear();
    g_pending.clear();
    g_placed.clear();
}
