/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright (C) 2015    Stefan Sundin                                   *
 * This program is free software: you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation, either version 3 or later.              *
 * Modified By Raymond Gillibert in 2022                                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "languages.h"

#include "altsnap.h"
#include "config.h"
#include "hooks.h"
#include "tray.h"

// Definitions of the tables declared in languages.h.
#define LNGVALUE(x, y, z) #x,
// Name of values ini entries
const char* const l10n_inimapping[] = {LANGUAGE_MAP};
#undef LNGVALUE

#define LNGVALUE(x, y, z) y, z,
// Default values en-US
const struct strings en_US = {LANGUAGE_MAP};
#undef LNGVALUE

// Strings of the currently loaded translation file. `l10n_ini_strings` holds
// pointers into `l10n_storage`, which owns the characters (one slot per
// struct member; the slots are never moved after construction).
static std::array<std::wstring, sizeof(struct strings) / sizeof(const wchar_t*)>
    l10n_storage;
static struct strings l10n_ini_strings = {};
static struct strings* l10n_ini = nullptr;  // &l10n_ini_strings once loaded.
const struct strings* l10n = &en_US;

/////////////////////////////////////////////////////////////////////////////
// Copies and remove the accelerators & sign. and txt between ( ).
size_t lstrcpy_noaccel(wchar_t* dest, size_t destlen, const wchar_t* source) {
  size_t i = 0, j = 0;
  while (i < destlen && source[i]) {
    dest[j] = source[i];
    if (source[i] == '(') {
      // If we found a '(' go to the closing one ')'.
      while (i < destlen && source[i] && source[i] != ')') i++;
      i++;
      i += source[i] == ' ';  // Remove space after ) if there is one.
    } else {
      j += source[i] != '&';
      i++;
    }
  }
  dest[j] = '\0';
  return j;
}
/////////////////////////////////////////////////////////////////////////////
[[nodiscard]] static size_t lstrlen_resolved(const wchar_t* str) {
  // Return the length of str, having resolved escape sequences
  const wchar_t* ptr = nullptr;
  int num_escape_sequences = 0;
  for (ptr = str; *ptr != '\0'; ptr++) {
    if (*ptr == '\\' && *(ptr + 1) != '\0') {
      ptr++;
      num_escape_sequences++;
    }
  }
  return ptr - str - num_escape_sequences;
}

static void lstrcpy_resolve(wchar_t* dest, size_t dstlen,
                            const wchar_t* source) {
  // Copy from source to dest, resolving \\n to \n
  for (; --dstlen && *source != '\0'; source++, dest++) {
    if (*source == '\\' && *(source + 1) == 'n') {
      *dest = '\n';
      source++;
    } else {
      *dest = *source;
    }
  }
  *dest = '\0';
}

static void lstrcpy_encode(wchar_t* dst, size_t dstlen, const wchar_t* src) {
  // Copy from source to dest, encoding '\n' to '\' 'n'
  for (; --dstlen && *src != '\0'; src++, dst++) {
    if (dstlen && *src == '\n') {
      *dst++ = '\\';
      *dst = 'n';
      --dstlen;
    } else {
      *dst = *src;
    }
  }
  *dst = '\0';
}
/////////////////////////////////////////////////////////////////////////////
//
static void LoadTranslationOrTT(const wchar_t* ini, const wchar_t* section_name,
                                int offset) {
  // if english is not seleced then we have to allocate l10_ini strings struct
  // and we have to read the ini file...
  // RGTICTAC tt; RGTic(&tt);
  const std::vector<wchar_t> section = ReadIniSection(section_name, ini, 32768);
  if (section.empty() || !section[0]) return;
  const wchar_t* const tsection = section.data();

  if (!l10n_ini) l10n_ini = &l10n_ini_strings;

  wchar_t const* ini_map[std::size(l10n_inimapping)];
  RGIniMapSection(tsection, ini_map, l10n_inimapping,
                  std::size(l10n_inimapping));

  for (size_t i = 0; i < std::size(l10n_inimapping); i++) {
    // Get pointer to default English string to be used if ini entry doesn't
    // exist
    const wchar_t* const def_val =
        (reinterpret_cast<const wchar_t* const*>(&en_US))[i * 2 + offset];
    const wchar_t* txt = ini_map[i] ? ini_map[i] : def_val;
    if (!txt) continue;  // default value may be NULL...

    wchar_t buf[128];
    const size_t slot = i * 2 + offset;
    const wchar_t** deststr =
        &reinterpret_cast<const wchar_t**>(l10n_ini)[slot];
    if (deststr == reinterpret_cast<const wchar_t**>(&l10n_ini->AboutVersion)) {
      // Append version number to version....
      lstrcpy_s(buf, std::size(buf), txt);
      lstrcat_s(buf, std::size(buf), L" " APP_VERSION);
      txt = static_cast<const wchar_t*>(buf);
    }
    const size_t destlen = lstrlen(txt) + 1;
    std::wstring& owned = l10n_storage[slot];
    owned.assign(destlen, L'\0');
    lstrcpy_resolve(owned.data(), destlen, txt);
    owned.resize(lstrlen(owned.c_str()));
    *deststr = owned.c_str();
  }
  l10n = l10n_ini;
  // LOGA("LoadTranslationOrTT in %u us", (unsigned)RGTac(&tt));
}

void LoadTranslation(const wchar_t* ini) {
  if (!ini) {
    l10n = &en_US;
    return;
  }
  if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(ini)) {
    return;
  }

  LoadTranslationOrTT(ini, L"Translation", 0);
  LoadTranslationOrTT(ini, L"ToolTips", 1);
}

std::vector<langinfoitem> langinfo;

/////////////////////////////////////////////////////////////////////////////
void ListAllTranslations() {
  if (!langinfo.empty()) return;

  // First element: the built-in English translation.
  langinfo.push_back({en_US.Code, en_US.LangEnglish, en_US.Lang, en_US.Author,
                      std::wstring()});

  wchar_t szDir[MAX_PATH];
  GetModuleFileName(nullptr, szDir, std::size(szDir));
  PathRemoveFileSpecL(szDir);
  lstrcat_s(szDir, std::size(szDir), L"\\Lang\\*.ini");
  const std::wstring directory(
      szDir, lstrlen(szDir) - lstrlen(L"*.ini"));  // Keep trailing '\\'.

  WIN32_FIND_DATA ffd;
  const HANDLE hFind = FindFirstFile(szDir, &ffd);
  if (hFind == INVALID_HANDLE_VALUE) return;
  do {
    const std::wstring path = directory + ffd.cFileName;

    // Preload the start of the Translation section.
    wchar_t tsection[512];
    if (!GetPrivateProfileSection(L"Translation", tsection, std::size(tsection),
                                  path.c_str())) {
      continue;
    }
    langinfoitem item;
    item.code = GetSectionOptionCStr(tsection, "Code", L"");
    item.lang_english = GetSectionOptionCStr(tsection, "LangEnglish", L"");
    item.lang = GetSectionOptionCStr(tsection, "Lang", L"");
    item.author = GetSectionOptionCStr(tsection, "Author", L"");
    item.fn = path;
    langinfo.push_back(std::move(item));
  } while (FindNextFile(hFind, &ffd));
  FindClose(hFind);
}

static void Generate_en_US_base_txt() {
  HANDLE h = CreateFileA("en_US.txt", GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;

  DWORD dummy = 0;
  wchar_t key[100], val[1000];
  wchar_t const* const* const def_strings =
      (reinterpret_cast<wchar_t const* const*>(&en_US));
  WriteFile(h, "\xFF\xFE", 2, &dummy, nullptr);  // UTF-16 LE BOM
  WriteFile(h, L"; Base AltSnap Translation\r\n[Translation]\r\n",
            43 * sizeof(wchar_t), &dummy, nullptr);

  for (size_t i = 0; i < std::size(l10n_inimapping); i++) {
    wchar_t* k = key;
    const char* p = l10n_inimapping[i];
    while ((*k++ = *p++));

    const wchar_t* vv = def_strings[i * 2];
    lstrcpy_encode(val, std::size(val), vv);

    WriteFile(h, key, lstrlen(key) * sizeof(wchar_t), &dummy, nullptr);
    WriteFile(h, L"=", sizeof(wchar_t), &dummy, nullptr);
    WriteFile(h, val, lstrlen(val) * sizeof(wchar_t), &dummy, nullptr);
    WriteFile(h, L"\r\n", 2 * sizeof(wchar_t), &dummy, nullptr);
  }
  CloseHandle(h);
}

#ifdef UNICODE
/////////////////////////////////////////////////////////////////////////////
// Helper function to get
int GetCUserLanguage_xx_XX(wchar_t txt[16]) {
  txt[0] = L'\0';
  return LCIDToLocaleName(GetUserDefaultLCID(), txt, 16, 0);
}
#endif
/////////////////////////////////////////////////////////////////////////////
void UpdateLanguage() {
  wchar_t txt[16];
  GetPrivateProfileString(L"General", L"Language", L"Auto", txt, std::size(txt),
                          inipath);

// Determine which language should be used
// based on current user's LCID
#ifdef _UNICODE
  if (!lstrcmpi(txt, L"Auto")) {
    GetCUserLanguage_xx_XX(txt);
  }
#endif  // _UNICODE

  if (!lstrcmpi(txt, L"en-US")) {
    return;  // Hardcoded language
  }

  ListAllTranslations();
  for (const langinfoitem& item : langinfo) {
    if (!lstrcmpi(txt, item.code.c_str())) {
      LoadTranslation(item.fn.empty() ? nullptr : item.fn.c_str());
      break;
    }
  }
}

void FreeAllLangRelated() {
  langinfo = {};

  if (l10n_ini) {
    for (auto& owned : l10n_storage) owned = {};
    l10n_ini_strings = {};
    l10n_ini = nullptr;
  }
  l10n = &en_US;
}
