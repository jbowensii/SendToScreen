#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct WinInfo { HWND h = nullptr; std::wstring aumid, exe, title, description; };
struct MonitorInfo { HMONITOR h = nullptr; RECT work{}; RECT full{}; bool primary = false; };

// logic.cpp: pure functions, covered by tests.cpp
std::wstring ButtonAumid(const std::wstring& automationId);
std::wstring ButtonLabel(const std::wstring& name);
std::vector<HWND> MatchWindows(const std::wstring& automationId, const std::wstring& name, const std::vector<WinInfo>& windows);
RECT TranslateRect(RECT r, RECT srcWork, RECT dstWork);

// taskbar.cpp
bool ButtonAtPoint(POINT pt, std::wstring& automationId, std::wstring& name);
std::vector<WinInfo> TaskbarWindows();
void DumpWindows();

// mover.cpp
std::vector<MonitorInfo> Monitors();
void MoveWindowToMonitor(HWND hwnd, const MonitorInfo& target);

// overlay.cpp
std::vector<HWND> ShowScreenNumbers(const std::vector<MonitorInfo>& mons);
void HideScreenNumbers(const std::vector<HWND>& wins);

// main.cpp
void Log(const std::wstring& line);
