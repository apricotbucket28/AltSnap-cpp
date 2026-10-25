// Host-side unit tests for strutil.h and IniSection (run under Wine).
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "blacklist.h"
#include "ini_section.h"
#include "strutil.h"

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
  CHECK(lstrcpy_s(buf, 8, L"abc") == 3 && std::wstring(buf) == L"abc");
  CHECK(lstrcpy_s(buf, 4, L"abcdef") == 3 &&
        std::wstring(buf) == L"abc");  // truncates
  CHECK(lstrcat_s(buf, 8, L"de") == 5 && std::wstring(buf) == L"abcde");
  CHECK(lstrcat_s(buf, 8, L"fghijk") == 7 &&
        std::wstring(buf) == L"abcdefg");  // truncates
  CHECK(lstrcpy_s(buf, 0, L"x") == 0);
  CHECK(strtoi(L"  -42xyz") == -42 && strtoi(L"+7") == 7 && strtoi(L"") == 0);
  CHECK(lstrhex2u(L"A4 A5") == 0xA4 && lstrhex2u(L"ff") == 0xFF);
  wchar_t num[kIntDigits + 1];
  CHECK(std::wstring(Int2lStr(num, -2147483647 - 1)) == L"-2147483648");
  CHECK(std::wstring(Int2lStr(num, 0)) == L"0");
  wchar_t unum[kUintDigits + 1];
  CHECK(std::wstring(Uint2lStr(unum, 4294967295u)) == L"4294967295");
  wchar_t hex[kPtrHexDigits + 1];
  CHECK(std::wstring(LPTR2Hex(hex, 0xBEEF)) == L"BEEF");
  CHECK(AsciiToLower(L'A') == L'a' && AsciiToLower(L'Z') == L'z' &&
        AsciiToLower(L'[') == L'[');
  CHECK(lstrcmpi_samestart(L"Alwaysontop=1", L"alwaysontop=") == 0);
  CHECK(lstrcmpi_samestart(L"Alwaysontop=1", L"zzz=") != 0);
  CHECK(lstrcmp_star(L"notepad.exe", L"note*") == 0);
  CHECK(lstrcmp_star(L"notepad.exe", L"*.exe") == 0);
  CHECK(lstrcmp_star(L"notepad.exe", L"*.dll") != 0);
  CHECK(lstrcmp_star(L"anything", static_cast<const wchar_t*>(nullptr)) == 0);

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

  std::printf(g_failures ? "%d FAILURE(S)\n" : "all tests passed\n",
              g_failures);
  return g_failures != 0;
}
