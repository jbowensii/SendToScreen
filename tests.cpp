#include "sendtoscreen.h"
#include <cstdio>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static bool Same(RECT a, LONG l, LONG t, LONG r, LONG b) { return a.left == l && a.top == t && a.right == r && a.bottom == b; }
static HWND H(int n) { return (HWND)(LONG_PTR)n; }

static void TestTranslateRect() {
    RECT a{ 0, 0, 1920, 1040 }, b{ 1920, 0, 3840, 1040 }, big{ 0, 0, 2560, 1400 }, sm{ 2560, 0, 3840, 720 };
    CHECK(Same(TranslateRect({ 100, 50, 900, 650 }, a, b), 2020, 50, 2820, 650));          // same size: keep offset
    CHECK(Same(TranslateRect({ 1920, 0, 2020, 100 }, b, a), 0, 0, 100, 100));              // right to left
    CHECK(Same(TranslateRect({ 2000, 900, 2400, 1300 }, big, sm), 3440, 320, 3840, 720));  // pushed inside smaller target
    CHECK(Same(TranslateRect({ 0, 0, 3000, 1500 }, big, sm), 2560, 0, 3840, 720));         // larger than target: shrink
    CHECK(Same(TranslateRect({ -10, -10, 90, 90 }, a, b), 1920, 0, 2020, 100));            // partly off-screen: clamp to origin
    CHECK(Same(TranslateRect({ 0, 0, 1920, 1040 }, a, b), 1920, 0, 3840, 1040));           // exactly work-area sized
    CHECK(Same(TranslateRect({ 2000, 0, 4000, 800 }, sm, sm), 2560, 0, 3840, 720));        // src == dst: pure clamp after a DPI rescale
}

static void TestButtonAumid() {
    CHECK(ButtonAumid(L"Appid: Chrome") == L"Chrome");
    CHECK(ButtonAumid(L"Chrome") == L"Chrome");
    CHECK(ButtonAumid(L"").empty());
}

static void TestButtonLabel() {
    CHECK(ButtonLabel(L"Notepad - 1 running window pinned") == L"Notepad");
    CHECK(ButtonLabel(L"Google Chrome - 3 running windows") == L"Google Chrome");
    CHECK(ButtonLabel(L"Firefox pinned") == L"Firefox");
    CHECK(ButtonLabel(L"a - b - 2 running windows") == L"a - b");
    CHECK(ButtonLabel(L"Notes - Draft") == L"Notes - Draft");
    CHECK(ButtonLabel(L"").empty());
}

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

    CHECK(is(m(L"Appid: Chrome", L"Google Chrome - 2 running windows"), { 1, 2 }));                  // AUMID, case-insensitive, whole group
    CHECK(is(m(L"Appid: {6D809377-6AF0-444B-8957-A3773F02200E}\\Elgato\\StreamDeck\\StreamDeck.exe", L"Stream Deck - 1 running window"), { 3 }));  // known-folder path id
    CHECK(is(m(L"Appid: C:\\Users\\johnb\\AppData\\Local\\obs-studio\\bin\\64bit\\obs64.exe", L"OBS Studio - 1 running window"), { 4 }));            // absolute path id
    CHECK(is(m(L"Appid: c:\\users\\JOHNB\\appdata\\local\\OBS-STUDIO\\bin\\64bit\\OBS64.EXE", L"x"), { 4 }));                                       // path compare ignores case
    CHECK(is(m(L"Appid: Microsoft.Office.WINWORD.EXE.15", L"Word - 2 running windows"), { 5, 6 }));   // registry AUMID: title suffix, whole exe group
    CHECK(is(m(L"Appid: Something.Unknown", L"Untitled - 1 running window"), { 7 }));                 // exact title
    CHECK(is(m(L"Appid: Something.Unknown", L"Thing Tool - 1 running window"), { 7 }));               // FileDescription
    CHECK(is(m(L"", L"Thing Tool pinned"), { 7 }));                                                   // no id at all
    CHECK(is(m(L"", L"untitled - 1 running window"), { 7 }));                                         // title compare ignores case
    CHECK(is(m(L"", L"Locked - 1 running window"), { 8 }));                                           // exe unknown (access denied): keep the matched window
    CHECK(m(L"Appid: Nope", L"Nope - 1 running window").empty());                                     // nothing matches
    CHECK(m(L"Appid: Nope\\Path", L"").empty());                                                      // path id with no tail match, empty name
    CHECK(m(L"Appid: {BAD-GUID}", L"").empty());                                                      // guid without a path tail
}

int main() {
    TestTranslateRect();
    TestButtonAumid();
    TestButtonLabel();
    TestMatchWindows();
    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
