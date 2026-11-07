#include "sendtoscreen.h"
#include "rules.h"
#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

// Stub for Log — avoids pulling in main.cpp (which has wWinMain)
void Log(const std::wstring&) {}

static bool Same(RECT a, LONG l, LONG t, LONG r, LONG b) { return a.left == l && a.top == t && a.right == r && a.bottom == b; }
static HWND H(int n) { return (HWND)(LONG_PTR)n; }

// ---------- logic.cpp: TranslateRect ----------

static void TestTranslateRect() {
    RECT a{ 0, 0, 1920, 1040 }, b{ 1920, 0, 3840, 1040 }, big{ 0, 0, 2560, 1400 }, sm{ 2560, 0, 3840, 720 };
    CHECK(Same(TranslateRect({ 100, 50, 900, 650 }, a, b), 2020, 50, 2820, 650));          // same size: keep offset
    CHECK(Same(TranslateRect({ 1920, 0, 2020, 100 }, b, a), 0, 0, 100, 100));              // right to left
    CHECK(Same(TranslateRect({ 2000, 900, 2400, 1300 }, big, sm), 3440, 320, 3840, 720));  // pushed inside smaller target
    CHECK(Same(TranslateRect({ 0, 0, 3000, 1500 }, big, sm), 2560, 0, 3840, 720));         // larger than target: shrink
    CHECK(Same(TranslateRect({ -10, -10, 90, 90 }, a, b), 1920, 0, 2020, 100));            // partly off-screen: clamp to origin
    CHECK(Same(TranslateRect({ 0, 0, 1920, 1040 }, a, b), 1920, 0, 3840, 1040));           // exactly work-area sized
    CHECK(Same(TranslateRect({ 2000, 0, 4000, 800 }, sm, sm), 2560, 0, 3840, 720));        // src == dst: pure clamp after a DPI rescale
    // Zero-size window
    CHECK(Same(TranslateRect({ 100, 100, 100, 100 }, a, b), 2020, 100, 2020, 100));
    // Identical src/dst with in-bounds rect: no change
    CHECK(Same(TranslateRect({ 50, 50, 200, 200 }, a, a), 50, 50, 200, 200));
}

// ---------- logic.cpp: ButtonAumid ----------

static void TestButtonAumid() {
    CHECK(ButtonAumid(L"Appid: Chrome") == L"Chrome");
    CHECK(ButtonAumid(L"Chrome") == L"Chrome");
    CHECK(ButtonAumid(L"").empty());
    CHECK(ButtonAumid(L"Appid: ") == L"");
    CHECK(ButtonAumid(L"Appid: C:\\Path\\To\\App.exe") == L"C:\\Path\\To\\App.exe");
}

// ---------- logic.cpp: ButtonLabel ----------

static void TestButtonLabel() {
    CHECK(ButtonLabel(L"Notepad - 1 running window pinned") == L"Notepad");
    CHECK(ButtonLabel(L"Google Chrome - 3 running windows") == L"Google Chrome");
    CHECK(ButtonLabel(L"Firefox pinned") == L"Firefox");
    CHECK(ButtonLabel(L"a - b - 2 running windows") == L"a - b");
    CHECK(ButtonLabel(L"Notes - Draft") == L"Notes - Draft");
    CHECK(ButtonLabel(L"").empty());
    // Only " pinned" suffix, no " - N running window" pattern
    CHECK(ButtonLabel(L"App pinned") == L"App");
    // Multiple " - " but last one has "running window"
    CHECK(ButtonLabel(L"X - Y - Z - 1 running window pinned") == L"X - Y - Z");
}

// ---------- logic.cpp: MatchWindows ----------

static void TestMatchWindows() {
    std::vector<WinInfo> w = {
        { H(1), L"Chrome", L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", L"Tab A - Google Chrome", L"Google Chrome" },
        { H(2), L"chrome", L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", L"Tab B - Google Chrome", L"Google Chrome" },
        { H(3), L"", L"C:\\Program Files\\Elgato\\StreamDeck\\StreamDeck.exe", L"Stream Deck", L"Stream Deck" },
        { H(4), L"", L"C:\\Users\\johnb\\AppData\\Local\\obs-studio\\bin\\64bit\\obs64.exe", L"OBS 31", L"OBS Studio" },
        { H(5), L"", L"C:\\Program Files\\Microsoft Office\\root\\Office16\\WINWORD.EXE", L"Report.docx - Word", L"Microsoft Word" },
        { H(6), L"", L"C:\\Program Files\\Microsoft Office\\root\\Office16\\WINWORD.EXE", L"Notes.docx - Word", L"Microsoft Word" },
        { H(7), L"", L"C:\\Tools\\thing.exe", L"Untitled", L"Thing Tool" },
        { H(8), L"", L"", L"Locked", L"" },
    };
    auto m = [&](const wchar_t* id, const wchar_t* name) { return MatchWindows(id, name, w); };
    auto is = [](const std::vector<HWND>& v, std::initializer_list<int> ids) {
        if (v.size() != ids.size()) return false;
        size_t i = 0; for (int n : ids) if (v[i++] != H(n)) return false; return true;
    };

    CHECK(is(m(L"Appid: Chrome", L"Google Chrome - 2 running windows"), { 1, 2 }));
    CHECK(is(m(L"Appid: {6D809377-6AF0-444B-8957-A3773F02200E}\\Elgato\\StreamDeck\\StreamDeck.exe", L"Stream Deck - 1 running window"), { 3 }));
    CHECK(is(m(L"Appid: C:\\Users\\johnb\\AppData\\Local\\obs-studio\\bin\\64bit\\obs64.exe", L"OBS Studio - 1 running window"), { 4 }));
    CHECK(is(m(L"Appid: c:\\users\\JOHNB\\appdata\\local\\OBS-STUDIO\\bin\\64bit\\OBS64.EXE", L"x"), { 4 }));
    CHECK(is(m(L"Appid: Microsoft.Office.WINWORD.EXE.15", L"Word - 2 running windows"), { 5, 6 }));
    CHECK(is(m(L"Appid: Something.Unknown", L"Untitled - 1 running window"), { 7 }));
    CHECK(is(m(L"Appid: Something.Unknown", L"Thing Tool - 1 running window"), { 7 }));
    CHECK(is(m(L"", L"Thing Tool pinned"), { 7 }));
    CHECK(is(m(L"", L"untitled - 1 running window"), { 7 }));
    CHECK(is(m(L"", L"Locked - 1 running window"), { 8 }));
    CHECK(m(L"Appid: Nope", L"Nope - 1 running window").empty());
    CHECK(m(L"Appid: Nope\\Path", L"").empty());
    CHECK(m(L"Appid: {BAD-GUID}", L"").empty());
    // Empty windows list
    CHECK(MatchWindows(L"Appid: Chrome", L"Chrome", {}).empty());
}

// ---------- rules.cpp: SaveRules / LoadRules round-trip ----------

// Backup and restore the real rules file so tests don't clobber user data
static std::wstring RealRulesPath() {
    wchar_t dir[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH)) return L"";
    return std::wstring(dir) + L"\\SendToScreen\\rules.json";
}

struct RulesGuard {
    std::wstring path, backup;
    RulesGuard() : path(RealRulesPath()), backup(path + L".test-bak") {
        if (!path.empty()) MoveFileW(path.c_str(), backup.c_str()); // ok if missing
    }
    ~RulesGuard() {
        if (path.empty()) return;
        DeleteFileW(path.c_str());
        MoveFileW(backup.c_str(), path.c_str()); // restore original
    }
};

static void TestRulesRoundTrip() {
    RulesGuard guard;
    // Save a known set of rules, load them back, verify they match
    std::vector<PlacementRule> original;

    PlacementRule r1;
    r1.exe = L"Firefox.exe";
    r1.monitor = 2;
    r1.state = PlacementRule::Maximized;
    r1.bringToFront = true;
    original.push_back(r1);

    PlacementRule r2;
    r2.exe = L"obs64.exe";
    r2.monitor = 1;
    r2.state = PlacementRule::Windowed;
    r2.x = 100;
    r2.y = 50;
    r2.w = 1280;
    r2.h = 720;
    r2.bringToFront = false;
    original.push_back(r2);

    PlacementRule r3;
    r3.exe = L"notepad.exe";
    r3.monitor = 3;
    r3.state = PlacementRule::Minimized;
    r3.bringToFront = true;
    original.push_back(r3);

    SaveRules(original);
    std::vector<PlacementRule> loaded = LoadRules();

    CHECK(loaded.size() == 3);
    if (loaded.size() >= 3) {
        CHECK(loaded[0].exe == L"Firefox.exe");
        CHECK(loaded[0].monitor == 2);
        CHECK(loaded[0].state == PlacementRule::Maximized);
        CHECK(loaded[0].bringToFront == true);

        CHECK(loaded[1].exe == L"obs64.exe");
        CHECK(loaded[1].monitor == 1);
        CHECK(loaded[1].state == PlacementRule::Windowed);
        CHECK(loaded[1].x == 100);
        CHECK(loaded[1].y == 50);
        CHECK(loaded[1].w == 1280);
        CHECK(loaded[1].h == 720);
        CHECK(loaded[1].bringToFront == false);

        CHECK(loaded[2].exe == L"notepad.exe");
        CHECK(loaded[2].monitor == 3);
        CHECK(loaded[2].state == PlacementRule::Minimized);
        CHECK(loaded[2].bringToFront == true);
    }
}

static void TestRulesEmpty() {
    RulesGuard guard;
    SaveRules({});
    std::vector<PlacementRule> loaded = LoadRules();
    CHECK(loaded.empty());
}

static void TestRulesSingleWindowed() {
    RulesGuard guard;
    PlacementRule r;
    r.exe = L"test.exe";
    r.monitor = 1;
    r.state = PlacementRule::Windowed;
    r.x = 0;
    r.y = 0;
    r.w = 800;
    r.h = 600;
    r.bringToFront = true;
    SaveRules({ r });
    auto loaded = LoadRules();
    CHECK(loaded.size() == 1);
    if (!loaded.empty()) {
        CHECK(loaded[0].exe == L"test.exe");
        CHECK(loaded[0].w == 800);
        CHECK(loaded[0].h == 600);
    }
}

static void TestRulesBringToFrontVariants() {
    RulesGuard guard;
    PlacementRule front, back;
    front.exe = L"front.exe";
    front.monitor = 1;
    front.state = PlacementRule::Maximized;
    front.bringToFront = true;
    back.exe = L"back.exe";
    back.monitor = 2;
    back.state = PlacementRule::Maximized;
    back.bringToFront = false;

    SaveRules({ front, back });
    auto loaded = LoadRules();
    CHECK(loaded.size() == 2);
    if (loaded.size() >= 2) {
        CHECK(loaded[0].bringToFront == true);
        CHECK(loaded[1].bringToFront == false);
    }
}

// ---------- mover.cpp: Monitors ----------

static void TestMonitors() {
    // On any Windows machine with a display, Monitors() should return at least one
    std::vector<MonitorInfo> mons = Monitors();
    CHECK(!mons.empty());
    if (!mons.empty()) {
        // First monitor should have non-zero dimensions
        CHECK(mons[0].full.right > mons[0].full.left);
        CHECK(mons[0].full.bottom > mons[0].full.top);
        CHECK(mons[0].work.right > mons[0].work.left);
        CHECK(mons[0].work.bottom > mons[0].work.top);
        // Exactly one primary
        int primaries = 0;
        for (const auto& m : mons) if (m.primary) ++primaries;
        CHECK(primaries == 1);
        // Sorted left to right
        for (size_t i = 1; i < mons.size(); ++i)
            CHECK(mons[i].full.left >= mons[i - 1].full.left);
    }
}

// ---------- runner ----------

int main() {
    std::printf("--- logic.cpp ---\n");
    TestTranslateRect();
    TestButtonAumid();
    TestButtonLabel();
    TestMatchWindows();

    std::printf("--- rules.cpp ---\n");
    TestRulesRoundTrip();
    TestRulesEmpty();
    TestRulesSingleWindowed();
    TestRulesBringToFrontVariants();

    std::printf("--- mover.cpp ---\n");
    TestMonitors();

    if (g_fail) { std::printf("\n%d failure(s)\n", g_fail); return 1; }
    std::printf("\nall tests passed\n");
    return 0;
}
