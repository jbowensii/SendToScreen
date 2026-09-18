#include "sendtoscreen.h"
#include <algorithm>

static bool IEq(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
static bool IEndsWith(const std::wstring& s, const std::wstring& tail) {
    return s.size() >= tail.size() && _wcsicmp(s.c_str() + s.size() - tail.size(), tail.c_str()) == 0;
}

std::wstring ButtonAumid(const std::wstring& automationId) {
    const std::wstring prefix = L"Appid: ";
    return automationId.rfind(prefix, 0) == 0 ? automationId.substr(prefix.size()) : automationId;
}

// "Notepad - 1 running window pinned" -> "Notepad"
std::wstring ButtonLabel(const std::wstring& name) {
    std::wstring label = name;
    if (IEndsWith(label, L" pinned")) label.erase(label.size() - 7);
    const size_t cut = label.rfind(L" - ");
    if (cut != std::wstring::npos && label.find(L"running window", cut) != std::wstring::npos) label.erase(cut);
    return label;
}

std::vector<HWND> MatchWindows(const std::wstring& automationId, const std::wstring& name, const std::vector<WinInfo>& windows) {
    std::vector<HWND> out;
    auto collect = [&](auto pred) {
        for (const WinInfo& w : windows) if (pred(w)) out.push_back(w.h);
        return !out.empty();
    };

    const std::wstring aumid = ButtonAumid(automationId);
    if (!aumid.empty()) {
        if (collect([&](const WinInfo& w) { return IEq(w.aumid, aumid); })) return out;
        // Unregistered exes: "C:\...\app.exe" or "{KNOWNFOLDER-GUID}\rest\app.exe"
        std::wstring tail = aumid;
        if (tail[0] == L'{') {
            const size_t close = tail.find(L"}\\");
            if (close != std::wstring::npos) tail.erase(0, close + 1);
        }
        if (tail.find(L'\\') != std::wstring::npos && collect([&](const WinInfo& w) { return IEndsWith(w.exe, tail); })) return out;
    }

    // Registry-only AUMIDs (Office) never appear on the window: fall back to the label, then take the whole exe group
    const std::wstring label = ButtonLabel(name);
    if (label.empty()) return out;
    const auto hit = std::find_if(windows.begin(), windows.end(), [&](const WinInfo& w) {
        return IEq(w.title, label) || IEndsWith(w.title, L" - " + label) || IEq(w.description, label);
    });
    if (hit != windows.end()) collect([&](const WinInfo& w) { return hit->exe.empty() ? w.h == hit->h : IEq(w.exe, hit->exe); });
    return out;
}

RECT TranslateRect(RECT r, RECT src, RECT dst) {
    const LONG w = std::min(r.right - r.left, dst.right - dst.left);
    const LONG h = std::min(r.bottom - r.top, dst.bottom - dst.top);
    const LONG x = std::max(dst.left, std::min(dst.left + (r.left - src.left), dst.right - w));
    const LONG y = std::max(dst.top, std::min(dst.top + (r.top - src.top), dst.bottom - h));
    return { x, y, x + w, y + h };
}
