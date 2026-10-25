// Reading of ini files.
//
// Replaces the old "GetPrivateProfileSection into a char buffer, then search
// the double-NUL-terminated list with GetSectionOption*()" convention, which
// every reader had to size and pass around by hand.

#ifndef ALTSNAP_INI_SECTION_H_
#define ALTSNAP_INI_SECTION_H_

#include <string>
#include <vector>

// A snapshot of one [section] of an ini file.
//
// Keys are ASCII and compared case-insensitively. Values are NUL-terminated
// strings that live inside this object: the pointers returned by GetString()
// and lines() stay valid until the IniSection is destroyed.
class IniSection {
 public:
  IniSection() = default;
  IniSection(IniSection&&) = default;
  IniSection& operator=(IniSection&&) = default;
  // Copying would leave the line pointers aimed at the source's buffer.
  IniSection(const IniSection&) = delete;
  IniSection& operator=(const IniSection&) = delete;

  // Reads [section_name] from `ini_path`. A missing section is empty.
  [[nodiscard]] static IniSection Read(const wchar_t* ini_path,
                                       const wchar_t* section_name);

  // Value of `key`, or `default_value` if the key is not present.
  // Upstream: GetSectionOptionCStr
  [[nodiscard]] const wchar_t* GetString(const char* key,
                                         const wchar_t* default_value) const;

  // Value of `key` as a decimal integer, or `default_value` if not present.
  // Upstream: GetSectionOptionInt
  [[nodiscard]] int GetInt(const char* key, int default_value) const;

  // The raw "key=value" lines in file order, comments (";...") included.
  [[nodiscard]] const std::vector<const wchar_t*>& lines() const {
    return lines_;
  }

 private:
  std::vector<wchar_t> buffer_;        // "key=value\0key=value\0\0"
  std::vector<const wchar_t*> lines_;  // Points into buffer_.
};

// Reads a string value of any length from an ini file (not limited to the
// size of a stack buffer).
[[nodiscard]] std::wstring ReadIniString(const wchar_t* section,
                                         const wchar_t* key,
                                         const wchar_t* default_value,
                                         const wchar_t* ini_path);

#endif  // ALTSNAP_INI_SECTION_H_
