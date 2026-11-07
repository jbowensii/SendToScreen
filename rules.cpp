#include "rules.h"
#include "sendtoscreen.h"
#include <shlobj.h>
#include <cstdio>
#include <algorithm>

// Minimal JSON: no dependency. Rules are a small array of simple objects.

static std::wstring RulesPath() {
    wchar_t dir[MAX_PATH];
    if (GetEnvironmentVariableW(L"SENDTOSCREEN_RULES_DIR", dir, MAX_PATH))
        return std::wstring(dir) + L"\\rules.json";
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH)) return L"";
    std::wstring path = std::wstring(dir) + L"\\SendToScreen";
    CreateDirectoryW(path.c_str(), nullptr);
    return path + L"\\rules.json";
}

static std::wstring ExeFilename(const std::wstring& fullPath) {
    const size_t sep = fullPath.find_last_of(L"\\/");
    return sep != std::wstring::npos ? fullPath.substr(sep + 1) : fullPath;
}

// Tiny JSON writer — no escaping needed for filenames and ints
static void WriteJson(FILE* f, const std::vector<PlacementRule>& rules) {
    fwprintf(f, L"[\n");
    for (size_t i = 0; i < rules.size(); ++i) {
        const PlacementRule& r = rules[i];
        const wchar_t* st = r.state == PlacementRule::Maximized ? L"maximized" : r.state == PlacementRule::Minimized ? L"minimized" : L"windowed";
        fwprintf(f, L"  { \"exe\": \"%s\", \"monitor\": %d, \"state\": \"%s\", \"front\": %s", r.exe.c_str(), r.monitor, st, r.bringToFront ? L"true" : L"false");
        if (r.state == PlacementRule::Windowed)
            fwprintf(f, L", \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d", r.x, r.y, r.w, r.h);
        fwprintf(f, L" }%s\n", i + 1 < rules.size() ? L"," : L"");
    }
    fwprintf(f, L"]\n");
}

// Tiny JSON reader — hand-rolled for the known schema
static std::wstring GetJsonString(const std::wstring& obj, const std::wstring& key) {
    const std::wstring needle = L"\"" + key + L"\"";
    size_t p = obj.find(needle);
    if (p == std::wstring::npos) return L"";
    p = obj.find(L':', p + needle.size());
    if (p == std::wstring::npos) return L"";
    p = obj.find(L'"', p + 1);
    if (p == std::wstring::npos) return L"";
    const size_t end = obj.find(L'"', p + 1);
    if (end == std::wstring::npos) return L"";
    return obj.substr(p + 1, end - p - 1);
}

static int GetJsonInt(const std::wstring& obj, const std::wstring& key, int def = 0) {
    const std::wstring needle = L"\"" + key + L"\"";
    size_t p = obj.find(needle);
    if (p == std::wstring::npos) return def;
    p = obj.find(L':', p + needle.size());
    if (p == std::wstring::npos) return def;
    return _wtoi(obj.c_str() + p + 1);
}

std::vector<PlacementRule> LoadRules() {
    std::vector<PlacementRule> rules;
    const std::wstring path = RulesPath();
    if (path.empty()) return rules;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f) return rules;
    std::wstring text;
    wchar_t buf[1024];
    while (fgetws(buf, _countof(buf), f)) text += buf;
    fclose(f);

    // Split by '{' ... '}' pairs
    size_t pos = 0;
    while ((pos = text.find(L'{', pos)) != std::wstring::npos) {
        const size_t end = text.find(L'}', pos);
        if (end == std::wstring::npos) break;
        const std::wstring obj = text.substr(pos, end - pos + 1);
        pos = end + 1;

        PlacementRule r;
        r.exe = GetJsonString(obj, L"exe");
        if (r.exe.empty()) continue;
        r.monitor = GetJsonInt(obj, L"monitor", 1);
        const std::wstring st = GetJsonString(obj, L"state");
        r.state = (st == L"maximized") ? PlacementRule::Maximized : (st == L"minimized") ? PlacementRule::Minimized : PlacementRule::Windowed;
        r.x = GetJsonInt(obj, L"x");
        r.y = GetJsonInt(obj, L"y");
        r.w = GetJsonInt(obj, L"w", 800);
        r.h = GetJsonInt(obj, L"h", 600);
        // JSON boolean: unquoted true/false, with or without space after colon
        r.bringToFront = (obj.find(L"\"front\":false") == std::wstring::npos
                       && obj.find(L"\"front\": false") == std::wstring::npos);
        rules.push_back(r);
    }
    return rules;
}

void SaveRules(const std::vector<PlacementRule>& rules) {
    const std::wstring path = RulesPath();
    if (path.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) return;
    WriteJson(f, rules);
    fclose(f);
}

PlacementRule SnapshotWindow(HWND hwnd) {
    PlacementRule r;

    // Exe filename
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buf[MAX_PATH * 2];
        DWORD n = _countof(buf);
        if (QueryFullProcessImageNameW(p, 0, buf, &n)) r.exe = ExeFilename(std::wstring(buf, n));
        CloseHandle(p);
    }

    // Which monitor (1-based, matching Monitors() order)
    const HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    const std::vector<MonitorInfo> mons = Monitors();
    r.monitor = 1;
    for (size_t i = 0; i < mons.size(); ++i) {
        if (mons[i].h == hmon) { r.monitor = static_cast<int>(i) + 1; break; }
    }

    // State and position
    WINDOWPLACEMENT wp{ sizeof(wp) };
    GetWindowPlacement(hwnd, &wp);
    if (wp.showCmd == SW_SHOWMAXIMIZED) {
        r.state = PlacementRule::Maximized;
    } else if (wp.showCmd == SW_SHOWMINIMIZED) {
        r.state = PlacementRule::Minimized;
    } else {
        r.state = PlacementRule::Windowed;
        RECT wr;
        GetWindowRect(hwnd, &wr);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(hmon, &mi);
        r.x = wr.left - mi.rcWork.left;
        r.y = wr.top - mi.rcWork.top;
        r.w = wr.right - wr.left;
        r.h = wr.bottom - wr.top;
    }
    return r;
}
