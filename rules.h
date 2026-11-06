#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct PlacementRule {
    std::wstring exe;       // exe filename (case-insensitive match), e.g. "Firefox.exe"
    int monitor = 1;        // 1-based monitor index
    enum State { Windowed, Maximized, Minimized } state = Windowed;
    int x = 0, y = 0;      // offset within monitor work area (windowed only)
    int w = 800, h = 600;   // size (windowed only)
    bool bringToFront = true; // true = HWND_TOP, false = HWND_BOTTOM
};

// rules.cpp
std::vector<PlacementRule> LoadRules();
void SaveRules(const std::vector<PlacementRule>& rules);
PlacementRule SnapshotWindow(HWND hwnd);

// placement.cpp
void StartPlacementHook();
void StopPlacementHook();
void ReloadPlacementRules();
