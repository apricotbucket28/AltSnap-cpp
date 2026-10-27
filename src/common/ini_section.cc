#include "common/ini_section.h"

#include <windows.h>

#include <cstring>

#include "common/strutil.h"

namespace altsnap {

IniSection IniSection::Read(const wchar_t* ini_path,
                            const wchar_t* section_name) {
  IniSection section;
  // GetPrivateProfileSection() returns size - 2 when it had to truncate, so
  // keep doubling the buffer until the whole section fits.
  size_t size = 4096;
  DWORD copied;
  do {
    section.buffer_.assign(size, L'\0');
    copied = GetPrivateProfileSection(section_name, section.buffer_.data(),
                                      static_cast<DWORD>(size), ini_path);
    if (copied != size - 2) break;
    size *= 2;
  } while (true);

  for (const wchar_t* line = section.buffer_.data(); *line;
       line += std::wcslen(line) + 1) {
    section.lines_.push_back(line);
  }
  return section;
}

const wchar_t* IniSection::GetString(const char* key,
                                     const wchar_t* default_value) const {
  const std::wstring prefix = std::wstring(key, key + std::strlen(key)) + L'=';
  for (const wchar_t* line : lines_) {
    if (!lstrcmpi_samestart(line, prefix.c_str())) {
      return line + prefix.size();
    }
  }
  return default_value;
}

int IniSection::GetInt(const char* key, int default_value) const {
  const wchar_t* value = GetString(key, nullptr);
  return value ? strtoi(value) : default_value;
}

std::wstring ReadIniString(const wchar_t* section, const wchar_t* key,
                           const wchar_t* default_value,
                           const wchar_t* ini_path) {
  std::wstring value(1024, L'\0');
  for (;;) {
    const DWORD copied =
        GetPrivateProfileString(section, key, default_value, value.data(),
                                static_cast<DWORD>(value.size()), ini_path);
    if (copied + 1 < value.size()) {  // Not truncated.
      value.resize(copied);
      return value;
    }
    value.assign(value.size() * 2, L'\0');
  }
}

}  // namespace altsnap
