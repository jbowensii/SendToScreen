#include "sendtoscreen.h"
#include <shellapi.h>
#include <uiautomation.h>
#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#include <dwmapi.h>
#include <appmodel.h>
#include <wrl/client.h>
#include <map>

using Microsoft::WRL::ComPtr;

static std::wstring Take(BSTR b) {
    std::wstring s = b ? std::wstring(b, SysStringLen(b)) : L"";
    SysFreeString(b);
    return s;
}

bool ButtonAtPoint(POINT pt, std::wstring& automationId, std::wstring& name) {
    ComPtr<IUIAutomation> uia;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia)))) return false;
    ComPtr<IUIAutomationElement> el;
    if (FAILED(uia->ElementFromPoint(pt, &el)) || !el) return false;
    ComPtr<IUIAutomationTreeWalker> walker;
    if (FAILED(uia->get_ControlViewWalker(&walker)) || !walker) return false;
    for (int depth = 0; el && depth < 8; ++depth) {
        BSTR cls = nullptr;
        el->get_CurrentClassName(&cls);
        if (Take(cls).find(L"TaskListButton") != std::wstring::npos) {
            BSTR id = nullptr;
            BSTR nm = nullptr;
            el->get_CurrentAutomationId(&id);
            el->get_CurrentName(&nm);
            automationId = Take(id);
            name = Take(nm);
            return true;
        }
        ComPtr<IUIAutomationElement> parent;
        if (FAILED(walker->GetParentElement(el.Get(), &parent))) break;
        el = parent;
    }
    return false;
}

static std::wstring WindowAumid(HWND h) {
    std::wstring s;
    ComPtr<IPropertyStore> ps;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(h, IID_PPV_ARGS(&ps)))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_AppUserModel_ID, &v)) && v.vt == VT_LPWSTR) s = v.pwszVal;
        PropVariantClear(&v);
    }
    if (s.empty()) {   // packaged apps carry it on the process
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            wchar_t buf[APPLICATION_USER_MODEL_ID_MAX_LENGTH];
            UINT32 n = _countof(buf);
            if (GetApplicationUserModelId(p, &n, buf) == ERROR_SUCCESS) s = buf;
            CloseHandle(p);
        }
    }
    return s;
}

static std::wstring ExePath(HWND h) {
    std::wstring s;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buf[MAX_PATH * 2];
        DWORD n = _countof(buf);
        if (QueryFullProcessImageNameW(p, 0, buf, &n)) s.assign(buf, n);
        CloseHandle(p);
    }
    return s;
}

static std::wstring FileDescription(const std::wstring& path) {
    const DWORD n = GetFileVersionInfoSizeW(path.c_str(), nullptr);
    if (!n) return L"";
    std::vector<BYTE> buf(n);
    if (!GetFileVersionInfoW(path.c_str(), 0, n, buf.data())) return L"";
    struct Tr { WORD lang, cp; };
    Tr* tr = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&tr), &len) || len < sizeof(Tr)) return L"";
    wchar_t sub[64];
    swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr[0].lang, tr[0].cp);
    wchar_t* desc = nullptr;
    if (!VerQueryValueW(buf.data(), sub, reinterpret_cast<void**>(&desc), &len) || !desc) return L"";
    return desc;
}

static bool TaskbarEligible(HWND h) {
    if (!IsWindowVisible(h) || IsHungAppWindow(h)) return false;
    const LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    if (GetWindow(h, GW_OWNER) && !(ex & WS_EX_APPWINDOW)) return false;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return cloaked == 0;
}

std::vector<WinInfo> TaskbarWindows() {
    struct Ctx { std::vector<WinInfo> all; std::map<std::wstring, std::wstring> desc; } ctx;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        if (TaskbarEligible(h)) {
            Ctx& c = *reinterpret_cast<Ctx*>(lp);
            wchar_t t[512] = {};
            GetWindowTextW(h, t, 512);
            const std::wstring exe = ExePath(h);
            if (!c.desc.count(exe)) c.desc[exe] = exe.empty() ? L"" : FileDescription(exe);
            c.all.push_back({ h, WindowAumid(h), exe, t, c.desc[exe] });
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.all;
}

void DumpWindows() {
    for (const WinInfo& w : TaskbarWindows()) {
        Log(L"win title=[" + w.title + L"] aumid=" + w.aumid + L" exe=" + w.exe + L" description=" + w.description);
    }
}
