/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright (C) 2015    Stefan Sundin                                   *
 * This program is free software: you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation, either version 3 or later.              *
 * Modified By Raymond Gillibert in 2022                                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "app/languages.h"

#include "app/altsnap.h"
#include "app/tray.h"
#include "common/ini_section.h"
#include "common/winutil.h"
#include "hooks/hooks.h"
#include "ui/config.h"

namespace altsnap::app {

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
// string, in L10nId order; the slots are never moved after construction).
static std::array<std::wstring, std::size(kL10nMembers)> l10n_storage;
static strings l10n_ini_strings = {};
static strings* l10n_ini = nullptr;  // &l10n_ini_strings once loaded.
const strings* l10n = &en_US;

/////////////////////////////////////////////////////////////////////////////
// Copies and remove the accelerators & sign. and txt between ( ).
// Upstream: lstrcpy_noaccel
size_t CopyWithoutAccelerator(wchar_t* dest, size_t destlen,
                              const wchar_t* source) {
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

// Upstream: lstrcpy_resolve
static void CopyResolvingEscapes(wchar_t* dest, size_t dstlen,
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

/////////////////////////////////////////////////////////////////////////////
//
// Loads the strings (offset 0, [Translation]) or the tooltips (offset 1,
// [ToolTips]) of a translation file; strings it lacks stay English.
static void LoadTranslationOrTT(const wchar_t* ini, const wchar_t* section_name,
                                int offset) {
  const IniSection section = IniSection::Read(ini, section_name);
  if (section.lines().empty()) return;

  if (!l10n_ini) l10n_ini = &l10n_ini_strings;

  for (size_t i = 0; i < std::size(l10n_inimapping); i++) {
    const size_t slot = i * 2 + offset;
    // The English string is used if the ini entry does not exist.
    const wchar_t* txt = section.GetString(l10n_inimapping[i],
                                           en_US.Get(static_cast<short>(slot)));
    if (!txt) continue;  // The English tooltip may be null.

    wchar_t with_version[128];
    if (slot == L10nId::AboutVersion) {
      // Append the version number to the about text.
      CopyString(with_version, std::size(with_version), txt);
      AppendString(with_version, std::size(with_version), L" " APP_VERSION);
      txt = with_version;
    }
    std::wstring& owned = l10n_storage[slot];
    owned.assign(lstrlen(txt) + 1, L'\0');
    CopyResolvingEscapes(owned.data(), owned.size(), txt);
    owned.resize(lstrlen(owned.c_str()));
    (l10n_ini->*kL10nMembers[slot]) = owned.c_str();
  }
  l10n = l10n_ini;
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

  const std::wstring directory = GetExecutableDirectory() + L"\\Lang\\";
  const std::wstring pattern = directory + L"*.ini";

  WIN32_FIND_DATA ffd;
  const HANDLE hFind = FindFirstFile(pattern.c_str(), &ffd);
  if (hFind == INVALID_HANDLE_VALUE) return;
  do {
    const std::wstring path = directory + ffd.cFileName;

    const IniSection translation =
        IniSection::Read(path.c_str(), L"Translation");
    if (translation.lines().empty()) continue;
    langinfoitem item;
    item.code = translation.GetString("Code", L"");
    item.lang_english = translation.GetString("LangEnglish", L"");
    item.lang = translation.GetString("Lang", L"");
    item.author = translation.GetString("Author", L"");
    item.fn = path;
    langinfo.push_back(std::move(item));
  } while (FindNextFile(hFind, &ffd));
  FindClose(hFind);
}

#ifdef UNICODE
/////////////////////////////////////////////////////////////////////////////
// Helper function to get
// Upstream: GetCUserLanguage_xx_XX
int GetUserLanguageCode(wchar_t txt[16]) {
  txt[0] = L'\0';
  return LCIDToLocaleName(GetUserDefaultLCID(), txt, 16, 0);
}
#endif
/////////////////////////////////////////////////////////////////////////////
void UpdateLanguage() {
  wchar_t txt[16];
  GetPrivateProfileString(L"General", L"Language", L"Auto", txt, std::size(txt),
                          inipath.c_str());

// Determine which language should be used
// based on current user's LCID
#ifdef _UNICODE
  if (!lstrcmpi(txt, L"Auto")) {
    GetUserLanguageCode(txt);
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

}  // namespace altsnap::app
