# Changelog

## 1.1.0 - 2026-09-21

### Fixed

- Moved windows now reliably come to the front with focus. The final activation borrows foreground rights the same way the menu does, since the swallowed click leaves the process without them; the log records `foreground=1/0`.

### Added

- While the screen menu is open, every monitor shows its number in a large centered badge (dark translucent rounded square, white Segoe UI Variable number, in the style of Settings > Display > Identify). Numbers match the menu, so a display Windows has renumbered after a monitor was off is still unambiguous.
- `--identify` previews the badges for five seconds.

## 1.0.0 - 2026-09-18

First release.

### Features

- Ctrl + right-click a running app's taskbar button on any monitor's taskbar to get a menu of screens; picking one moves every window of that app there.
- Windows keep their size and offset from the screen's top-left, clamped to fit the target work area. Maximized windows stay maximized on the new screen, minimized windows are restored. Moved windows come to the front and take focus.
- Works with Win32 apps, packaged and Store apps, and apps whose AUMID is registered only in the registry (Office).
- Mixed-DPI monitors: windows that rescale themselves on the move are clamped again so they stay on screen.
- Tray icon with Exit; survives Explorer restarts and taskbar recreation at sign-in.
- Signed per-user installer with a start-at-login option and clean uninstall.

### Design

- Single 240 KB exe, no runtime dependencies beyond Windows, no Explorer injection, no services.
- Idle cost is one blocked thread: 0.15 MB working set, 1.6 MB private memory, 0 CPU. The mouse hook rejects every non-gesture event after two integer compares and never calls into another process.
- Hung apps are skipped so the hook can never stall.
- Plain right-click, Shift+right-click and Ctrl+left-click on the taskbar are untouched.

### Verification

- Unit tests cover the pure logic (button id and label parsing, window matching, placement math) at 100% line coverage; four deliberate mutants are caught.
- Clean under MSVC /W4 and /analyze, cppcheck, and clang-tidy.
- Independent code review; findings around hook safety, tray recovery, DPI, and gesture pairing are fixed in this release.

### Known limits

- Windows of elevated (administrator) apps cannot be moved by a per-user process.
- Single-monitor systems: the gesture does nothing.
