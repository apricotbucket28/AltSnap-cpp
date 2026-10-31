// Host-side unit tests for strutil.h and IniSection (run under Wine).
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "common/ini_section.h"
#include "common/text_format.h"
#include "hooks/blacklist.h"
#include "hooks/hooks.h"

using altsnap::AddKeyToList;
using altsnap::Blacklist;
using altsnap::Blacklists;
using altsnap::FormatKeyEvent;
using altsnap::FormatMouseEvent;
using altsnap::FormatRect;
using altsnap::IniSection;
using altsnap::IsKeyInList;
using altsnap::ParseRect;
using altsnap::ReadIniString;
using altsnap::RemoveAccelerator;
using altsnap::RemoveKeyFromList;
using altsnap::ToHex;
#include "common/strutil.h"

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

int main() {
  // ---- strutil ----
  wchar_t buf[8];
  CHECK(CopyString(buf, 8, L"abc") == 3 && std::wstring(buf) == L"abc");
  CHECK(CopyString(buf, 4, L"abcdef") == 3 &&
        std::wstring(buf) == L"abc");  // truncates

  CHECK(AppendString(buf, 8, L"de") == 5 && std::wstring(buf) == L"abcde");
  CHECK(AppendString(buf, 8, L"fghijk") == 7 &&
        std::wstring(buf) == L"abcdefg");  // truncates

  CHECK(CopyString(buf, 0, L"x") == 0);

  CHECK(ParseInt(L"  -42xyz") == -42 && ParseInt(L"+7") == 7 &&
        ParseInt(L"") == 0);

  CHECK(HexToUnsigned(L"A4 A5") == 0xA4 && HexToUnsigned(L"ff") == 0xFF);

  wchar_t num[kIntDigits + 1];
  CHECK(std::wstring(Int2lStr(num, -2147483647 - 1)) == L"-2147483648");
  CHECK(std::wstring(Int2lStr(num, 0)) == L"0");

  wchar_t unum[kUintDigits + 1];
  CHECK(std::wstring(Uint2lStr(unum, 4294967295u)) == L"4294967295");

  CHECK(AsciiToLower(L'A') == L'a' && AsciiToLower(L'Z') == L'z' &&
        AsciiToLower(L'[') == L'[');

  CHECK(CompareStartIgnoreCase(L"Alwaysontop=1", L"alwaysontop=") == 0);
  CHECK(CompareStartIgnoreCase(L"Alwaysontop=1", L"zzz=") != 0);

  CHECK(WildcardMatch(L"notepad.exe", L"notepad.exe"));
  CHECK(!WildcardMatch(L"notepad.exe", L"notepad.dll"));
  CHECK(WildcardMatch(L"", L""));
  CHECK(!WildcardMatch(L"notepad.exe", L""));
  CHECK(WildcardMatch(L"notepad.exe", L"note*"));
  CHECK(!WildcardMatch(L"notepad.exe", L"epad*"));
  CHECK(!WildcardMatch(L"no", L"note*"));
  CHECK(WildcardMatch(L"notepad.exe", L"*.exe"));
  CHECK(!WildcardMatch(L"notepad.exe", L"*.dll"));
  CHECK(!WildcardMatch(L"exe", L"*.exe"));
  CHECK(WildcardMatch(L"notepad.exe", L"*pad*"));
  CHECK(!WildcardMatch(L"notepad.exe", L"*xyz*"));
  CHECK(WildcardMatch(L"", L"*"));
  CHECK(WildcardMatch(L"anything", L"**"));
  CHECK(WildcardMatch(L"a*b", L"a*b"));
  CHECK(!WildcardMatch(L"axb", L"a*b"));

  // ---- IniSection ----
  wchar_t path[MAX_PATH];
  GetCurrentDirectoryW(MAX_PATH, path);
  std::wstring ini = std::wstring(path) + L"\\unit_test.ini";
  DeleteFileW(ini.c_str());
  WritePrivateProfileStringW(L"General", L"AutoFocus", L"1", ini.c_str());
  WritePrivateProfileStringW(L"General", L"Hotkeys", L"A4 A5", ini.c_str());
  // Real translation files are UTF-16LE with a BOM; create one that way.
  {
    std::wstring file =
        L"\uFEFF[Unicode]\r\nName=h\u00e9llo "
        L"\u4e2d\u6587\r\nRu=\u041f\u0440\u0438\u0432\u0435\u0442\r\n";
    std::wstring uini = std::wstring(path) + L"\\unit_test_u.ini";
    HANDLE h = CreateFileW(uini.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    WriteFile(h, file.data(), static_cast<DWORD>(file.size() * 2), &written,
              nullptr);
    CloseHandle(h);
    IniSection u = IniSection::Read(uini.c_str(), L"Unicode");
    CHECK(std::wstring(u.GetString("Name", L"")) == L"h\u00e9llo \u4e2d\u6587");
    CHECK(std::wstring(u.GetString("Ru", L"")) ==
          L"\u041f\u0440\u0438\u0432\u0435\u0442");
    DeleteFileW(uini.c_str());
  }
  // A long section, bigger than the initial buffer, to exercise growth.
  for (int i = 0; i < 400; ++i) {
    std::wstring key = L"Key" + std::to_wstring(i);
    WritePrivateProfileStringW(L"Big", key.c_str(),
                               L"0123456789012345678901234567890123456789",
                               ini.c_str());
  }
  WritePrivateProfileStringW(L"Big", L"Last", L"found", ini.c_str());

  IniSection general = IniSection::Read(ini.c_str(), L"General");
  CHECK(general.GetInt("AutoFocus", 9) == 1);
  CHECK(general.GetInt("autofocus", 9) == 1);  // case-insensitive
  CHECK(general.GetInt("Missing", 9) == 9);
  CHECK(std::wstring(general.GetString("Hotkeys", L"")) == L"A4 A5");
  CHECK(general.GetString("Missing", nullptr) == nullptr);
  CHECK(general.lines().size() == 2);
  IniSection moved = std::move(general);  // pointers must survive a move
  CHECK(std::wstring(moved.GetString("Hotkeys", L"")) == L"A4 A5");
  IniSection big = IniSection::Read(ini.c_str(), L"Big");
  CHECK(big.lines().size() == 401);
  CHECK(std::wstring(big.GetString("Last", L"")) == L"found");
  CHECK(IniSection::Read(ini.c_str(), L"NoSuchSection").lines().empty());
  std::wstring long_value(5000, L'x');
  WritePrivateProfileStringW(L"Long", L"V", long_value.c_str(), ini.c_str());
  CHECK(ReadIniString(L"Long", L"V", L"", ini.c_str()).size() >= 4000);
  CHECK(ReadIniString(L"Long", L"Nope", L"dflt", ini.c_str()) == L"dflt");
  DeleteFileW(ini.c_str());

  // ---- Blacklist ----
  {
    const Blacklist exe_only(L"notepad.exe");
    CHECK(exe_only.Contains(L"notepad.exe", L"t", L"c"));
    CHECK(exe_only.Contains(L"NOTEPAD.EXE", L"t", L"c"));  // case-insensitive
    CHECK(!exe_only.Contains(L"calc.exe", L"t", L"c"));
    CHECK(!Blacklist().Contains(L"a", L"b", L"c"));  // empty list

    const Blacklist title_class(L"Untitled*|Notepad, *|Chrome_WidgetWin_1");
    CHECK(title_class.Contains(L"x.exe", L"Untitled - Notepad", L"Notepad"));
    CHECK(!title_class.Contains(L"x.exe", L"Untitled", L"Other"));
    CHECK(title_class.Contains(L"x.exe", L"anything", L"Chrome_WidgetWin_1"));

    const Blacklist full(L"calc.exe:Calculator|CalcFrame");
    CHECK(full.Contains(L"calc.exe", L"Calculator", L"CalcFrame"));
    CHECK(!full.Contains(L"other.exe", L"Calculator", L"CalcFrame"));
    CHECK(!full.Contains(L"calc.exe", L"Calculator", L"Other"));

    // "exename:title" without a class: any class matches.
    const Blacklist no_class(L"calc.exe:Calculator");
    CHECK(no_class.Contains(L"calc.exe", L"Calculator", L"whatever"));
    CHECK(!no_class.Contains(L"calc.exe", L"Other", L"whatever"));

    // A ':' after the '|' belongs to the class, not to an exename split.
    const Blacklist colon_in_class(L"Title|Cls:x");
    CHECK(colon_in_class.Contains(L"any.exe", L"Title", L"Cls:x"));

    // Separators: spaces after commas are skipped, empty items ignored.
    const Blacklist spaced(L"a.exe,   b.exe,,c.exe,");
    CHECK(spaced.Contains(L"a.exe", L"", L"") &&
          spaced.Contains(L"b.exe", L"", L"") &&
          spaced.Contains(L"c.exe", L"", L""));
    CHECK(
        !spaced.Contains(L"", L"", L""));  // a trailing comma is not a wildcard

    // "*foo*" is a substring match on titles and classes.
    const Blacklist contains(L"*|*Frame*, *pad*|*");
    CHECK(contains.Contains(L"x", L"t", L"MyFrameWnd"));
    CHECK(contains.Contains(L"x", L"Notepad", L"c"));
    CHECK(!contains.Contains(L"x", L"Calc", L"Button"));

    // Empty or all-star parts mean "any".
    const Blacklist any_title(L"|MyClass");
    CHECK(any_title.Contains(L"x.exe", L"whatever", L"MyClass"));
    CHECK(!any_title.Contains(L"x.exe", L"whatever", L"Other"));
    const Blacklist empty_title(L"calc.exe:|Cls");
    CHECK(empty_title.Contains(L"calc.exe", L"anything", L"Cls"));
    CHECK(!empty_title.Contains(L"other.exe", L"anything", L"Cls"));

    // "**|**" counts as "anything", so it enables whitelist mode like "*|*".
    const Blacklist whitelist2(L"**|**, notepad.exe");
    CHECK(!whitelist2.Contains(L"notepad.exe", L"t", L"c"));
    CHECK(whitelist2.Contains(L"calc.exe", L"t", L"c"));

    // Whitespace-only items must not become match-anything.
    const Blacklist blanks(L"calc.exe, ,   ");
    CHECK(blanks.Contains(L"calc.exe", L"", L""));
    CHECK(!blanks.Contains(L"other.exe", L"t", L"c"));

    // Whitelist mode: a leading `*|*` blacklists everything that is NOT listed.
    const Blacklist whitelist(L"*|*, notepad.exe");
    CHECK(!whitelist.Contains(L"notepad.exe", L"t", L"c"));
    CHECK(whitelist.Contains(L"calc.exe", L"t", L"c"));

    const Blacklist star_end(L"*|Edit*, *|*Frame");
    CHECK(star_end.Contains(L"x", L"t", L"EditBox"));
    CHECK(star_end.Contains(L"x", L"t", L"CalcFrame"));
    CHECK(!star_end.Contains(L"x", L"t", L"Button"));

    wchar_t dir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, dir);
    const std::wstring bl_ini = std::wstring(dir) + L"\\unit_test_bl.ini";
    DeleteFileW(bl_ini.c_str());
    WritePrivateProfileStringW(L"Blacklist", L"Processes", L"notepad.exe",
                               bl_ini.c_str());
    WritePrivateProfileStringW(L"Blacklist", L"Pause", L"*|Game",
                               bl_ini.c_str());
    const Blacklists lists =
        Blacklists::Read(IniSection::Read(bl_ini.c_str(), L"Blacklist"));
    CHECK(lists.Processes.Contains(L"notepad.exe", L"", L""));
    CHECK(lists.Pause.Contains(L"x", L"t", L"Game"));
    CHECK(lists.Windows.empty());
    DeleteFileW(bl_ini.c_str());
  }

  // ---- text_format ----
  CHECK(FormatRect({10, -20, 300, 400}) ==
        L"10,-20,300,400,");  // Zone<N> format
  CHECK(ToHex(0) == L"0" && ToHex(0x1F) == L"1F" &&
        ToHex(0xDEADBEEFull) == L"DEADBEEF");
  CHECK(RemoveAccelerator(L"Test &window") == L"Test window");
  CHECK(RemoveAccelerator(L"Test window(&W)") == L"Test window");
  CHECK(RemoveAccelerator(L"Test window(&W) now") ==
        L"Test windownow");  // space after ')' is dropped, as before
  CHECK(RemoveAccelerator(L"\u6d4b\u8bd5\u7a97\u53e3(&W)") ==
        L"\u6d4b\u8bd5\u7a97\u53e3");
  CHECK(RemoveAccelerator(L"Foo (bar") ==
        L"Foo ");  // unclosed '(': old code read past the end
  CHECK(RemoveAccelerator(L"") == L"" && RemoveAccelerator(L"&&") == L"");
  CHECK(FormatKeyEvent(0x1B, 0x00010001, L"Esc") ==
        L"vK=1B D sC=1, LP=10001, Esc");
  CHECK(FormatKeyEvent(0x41, 0x40000001, L"") == L"vK=41 R sC=0, LP=40000001");
  CHECK(FormatKeyEvent(0x41, static_cast<LPARAM>(0xC0000001), L"A") ==
        L"vK=41 U sC=0, LP=C0000001, A");
  CHECK(FormatMouseEvent(L"LClick D", false, 1, MAKELPARAM(10, 20)) ==
        L"LClick D (10, 20), WP=1");
  CHECK(FormatMouseEvent(L"Wheel", true, MAKEWPARAM(0, 120),
                         MAKELPARAM(5, 6)) == L"Wheel +120 (5, 6), WP=780000");
  CHECK(FormatMouseEvent(L"Wheel", true, MAKEWPARAM(0, static_cast<WORD>(-120)),
                         MAKELPARAM(-3, 4)) ==
        L"Wheel -120 (-3, 4), WP=FF880000");

  // ---- key lists ("A4 A5") ----
  CHECK(IsKeyInList(L"A4 A5", 0xA5) && IsKeyInList(L"A4 A5", 0xA4) &&
        !IsKeyInList(L"A4 A5", 0x5B));
  CHECK(!IsKeyInList(L"", 0x5B) &&
        IsKeyInList(L"a4  a5", 0xA5));              // case/spacing tolerant
  CHECK(AddKeyToList(L"A4 A5", 0xA4) == L"A4 A5");  // already there
  CHECK(AddKeyToList(L"A4 A5", 0x5B) == L"A4 A5 5B");
  CHECK(AddKeyToList(L"", 0x5B) == L"5B");
  CHECK(RemoveKeyFromList(L"A4 A5", 0xA4) == L"A5");
  CHECK(RemoveKeyFromList(L"A4 A5", 0xA5) == L"A4");  // trailing space stripped
  CHECK(RemoveKeyFromList(L"A4 5B A5", 0x5B) == L"A4 A5");
  CHECK(RemoveKeyFromList(L"A4 A5", 0x11) == L"A4 A5");  // not present
  CHECK(RemoveKeyFromList(L"A4", 0xA4) == L"");

  // ---- zone rectangles ----
  {
    const auto rc = ParseRect(L"10,-20,300,400,");
    CHECK(rc && rc->left == 10 && rc->top == -20 && rc->right == 300 &&
          rc->bottom == 400);
    const auto round_trip = ParseRect(FormatRect({1, 2, 3, 4}));
    CHECK(round_trip && round_trip->left == 1 && round_trip->bottom == 4);
    CHECK(!ParseRect(L"1,2,3,4"));  // each number needs its trailing comma
    CHECK(!ParseRect(L"1,2,3,"));   // too few numbers
    CHECK(!ParseRect(L"1,,3,4,"));  // empty field
    CHECK(!ParseRect(L""));
    const auto spaced =
        ParseRect(L" 5, -6,7,8,");  // leading spaces are accepted
    CHECK(spaced && spaced->left == 5 && spaced->top == -6);
  }
  CHECK(ZoneKey(0, 5) == "Zone5" && ZoneKey(1, 12) == "AZone12" &&
        ZoneKey(9, 0) == "IZone0");
  CHECK(ZoneKey(10, 0).empty() && ZoneKey(-1, 0).empty());
  CHECK(ZoneKeyWide(2, 3) == L"BZone3");

  std::printf(g_failures ? "%d FAILURE(S)\n" : "all tests passed\n",
              g_failures);
  return g_failures != 0;
}
