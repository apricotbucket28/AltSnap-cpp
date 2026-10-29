// Small, allocation-free string and number helpers.
//
// This header replaces the old C-style "nanolibc.h". The design rules are:
//   * No macros: no function-like or attribute macros, and nothing that
//     redefines a standard or Win32 name (the old header redefined `strchr`,
//     `wcslen`, `tolower`, `inline`, ... which broke any STL header included
//     after it).
//   * One template/overload set per helper instead of A/W suffixes plus a
//     `#ifdef UNICODE` switch. The character type is deduced from arguments.
//   * Buffer sizes are checked at compile time (references to arrays) instead
//     of the C99 `static` array-parameter hint, which C++ ignores.

#ifndef ALTSNAP_STRUTIL_H_
#define ALTSNAP_STRUTIL_H_

#include <windows.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string_view>
#include <type_traits>

// Upstream names (nanolibc.h) of the helpers below:
//   lstrcpy_s -> CopyString
//   lstrcat_s -> AppendString
//   lstrstr -> FindSubstring
//   lstrchr -> FindChar
//   lstrcmp_star -> WildcardMatch (inverted: true = match)
//   lstrcmpi_samestart -> CompareStartIgnoreCase
//   lstrhex2u -> HexToUnsigned
//   strtoi -> ParseInt
//   str2tchar -> AsciiToWide
//   str2tchar_s -> AsciiToWideBounded

// -----------------------------------------------------------------------------
// Bit helpers
// -----------------------------------------------------------------------------

[[nodiscard]] constexpr int SetBit(int n, int k) {
  return n | (1 << k);
}
[[nodiscard]] constexpr int ClearBit(int n, int k) {
  return n & ~(1 << k);
}

// -----------------------------------------------------------------------------
// Zeroing
// -----------------------------------------------------------------------------

// Zeroes a trivially copyable object or array in place. Replaces
// `mem00(&x, sizeof x)`: the size can no longer be wrong, and non-trivial types
// (which must never be memset) are rejected at compile time.
template <typename T>
void ZeroObject(T& object) {
  static_assert(std::is_trivially_copyable_v<T>,
                "ZeroObject() requires a trivially copyable type");
  std::memset(&object, 0, sizeof(object));
}

// Zeroes `count` consecutive trivially copyable objects.
template <typename T>
void ZeroRange(T* first, size_t count) {
  static_assert(std::is_trivially_copyable_v<T>,
                "ZeroRange() requires a trivially copyable type");
  std::memset(first, 0, sizeof(T) * count);
}

// -----------------------------------------------------------------------------
// Character helpers
// -----------------------------------------------------------------------------

// ASCII-only lower-casing. Unlike the old `tolower` macro this handles the
// whole 'A'..'Z' range (the old one skipped both 'A' and 'Z').
template <typename CharT>
[[nodiscard]] constexpr CharT AsciiToLower(CharT c) {
  return (c >= CharT{'A'} && c <= CharT{'Z'}) ? static_cast<CharT>(c + 0x20)
                                              : c;
}

// Value of a hexadecimal digit, or 0 if `c` is not one.
template <typename CharT>
[[nodiscard]] constexpr unsigned HexDigitValue(CharT c) {
  if (c >= CharT{'0'} && c <= CharT{'9'}) return c - CharT{'0'};
  if (c >= CharT{'A'} && c <= CharT{'F'}) return c - CharT{'A'} + 10;
  if (c >= CharT{'a'} && c <= CharT{'f'}) return c - CharT{'a'} + 10;
  return 0;
}

// -----------------------------------------------------------------------------
// Narrow -> wchar_t widening (for ASCII-only identifiers such as ini key names)
// -----------------------------------------------------------------------------

// Copies the ASCII string `src` into `dest`, widening each char. `dest` must be
// large enough for the string and its terminator.
template <typename CharT>
void AsciiToWide(CharT* dest, const char* src) {
  while ((*dest++ = static_cast<CharT>(*src++))) {
  }
}

// Same, but writes at most `n - 1` characters followed by a terminator.
template <typename CharT>
void AsciiToWideBounded(CharT* dest, size_t n, const char* src) {
  if (n == 0) return;
  size_t i = 0;
  for (; i + 1 < n && src[i]; ++i) dest[i] = static_cast<CharT>(src[i]);
  dest[i] = CharT{'\0'};
}

// -----------------------------------------------------------------------------
// Bounded copy / append (always NUL-terminating, truncating on overflow)
// -----------------------------------------------------------------------------

// Copies `src` into `dest` (capacity `n` characters). Returns the resulting
// length.
template <typename CharT>
size_t CopyString(CharT* dest, size_t n, const CharT* src) {
  if (n == 0) return 0;
  size_t i = 0;
  for (; i + 1 < n && src[i]; ++i) dest[i] = src[i];  // keep room for the NUL
  dest[i] = CharT{'\0'};
  return i;
}

// Appends `src` to the NUL-terminated string in `dest` (capacity `n`
// characters). Truncates instead of overflowing. Returns the resulting length.
template <typename CharT>
size_t AppendString(CharT* dest, size_t n, const CharT* src) {
  if (n == 0) return 0;
  size_t len = 0;
  while (len < n && dest[len]) ++len;
  if (len == n) {  // `dest` was not terminated inside the buffer.
    dest[n - 1] = CharT{'\0'};
    return n - 1;
  }
  return len + CopyString(dest + len, n - len, src);
}

// -----------------------------------------------------------------------------
// Searching
// -----------------------------------------------------------------------------

[[nodiscard]] inline const char* FindSubstring(const char* haystack,
                                               const char* needle) {
  return std::strstr(haystack, needle);
}
[[nodiscard]] inline const wchar_t* FindSubstring(const wchar_t* haystack,
                                                  const wchar_t* needle) {
  return std::wcsstr(haystack, needle);
}

[[nodiscard]] inline const char* FindChar(const char* s, char c) {
  return std::strchr(s, c);
}
[[nodiscard]] inline char* FindChar(char* s, char c) {
  return std::strchr(s, c);
}
[[nodiscard]] inline const wchar_t* FindChar(const wchar_t* s, wchar_t c) {
  return std::wcschr(s, c);
}
[[nodiscard]] inline wchar_t* FindChar(wchar_t* s, wchar_t c) {
  return std::wcschr(s, c);
}

// -----------------------------------------------------------------------------
// Matching
// -----------------------------------------------------------------------------

// Returns true if `text` matches `pattern`. A leading '*' matches a suffix,
// a trailing '*' a prefix, and both ("*foo*") a substring. A '*' anywhere
// else is a literal character. "*" and "**" match everything; an empty
// pattern matches only the empty string. Case-sensitive.
[[nodiscard]] inline bool WildcardMatch(std::wstring_view text,
                                        std::wstring_view pattern) {
  const bool lead = pattern.starts_with(L'*');
  if (lead) pattern.remove_prefix(1);
  const bool trail = pattern.ends_with(L'*');
  if (trail) pattern.remove_suffix(1);

  if (lead && trail) return text.contains(pattern);
  if (lead) return text.ends_with(pattern);
  if (trail) return text.starts_with(pattern);
  return text == pattern;
}

// Returns 0 if `a` starts with `b`, ignoring ASCII case.
template <typename CharT>
[[nodiscard]] int CompareStartIgnoreCase(const CharT* a, const CharT* b) {
  while (*a && AsciiToLower(*a) == AsciiToLower(*b)) {
    ++a;
    ++b;
  }
  return (*a != *b) && (*b != CharT{'\0'});
}

// -----------------------------------------------------------------------------
// Number <-> string
// -----------------------------------------------------------------------------

// Parses an optionally signed decimal integer (leading spaces allowed). Stops
// at the first non-digit. Does not detect overflow.
template <typename CharT>
[[nodiscard]] int ParseInt(const CharT* s) {
  while (*s == CharT{' '}) ++s;
  int sign = 1;
  if (*s == CharT{'-'}) {
    sign = -1;
    ++s;
  } else if (*s == CharT{'+'}) {
    ++s;
  }
  long value = 0;
  while (*s >= CharT{'0'} && *s <= CharT{'9'}) {
    value = value * 10 + (*s - CharT{'0'});
    ++s;
  }
  return static_cast<int>(sign * value);
}

// Parses hexadecimal digits up to the end of the string or the first character
// below '0' (so a space, comma or NUL terminates the number).
template <typename CharT>
[[nodiscard]] unsigned HexToUnsigned(const CharT* s) {
  unsigned ret = 0;
  while (*s >= CharT{'0'}) ret = ret << 4 | HexDigitValue(*s++);
  return ret;
}

// Digits needed by the largest values, excluding the terminator.
inline constexpr size_t kIntDigits = 11;   // "-2147483648"
inline constexpr size_t kUintDigits = 10;  // "4294967295"
inline constexpr size_t kPtrHexDigits = sizeof(UINT_PTR) * 2;

// The functions below format into the *end* of a caller-supplied buffer and
// return a pointer to the first character, so no allocation or reversal is
// needed. The buffer size is checked at compile time.

template <typename CharT, size_t N>
[[nodiscard]] const CharT* Int2lStr(CharT (&buf)[N], int n) {
  static_assert(N >= kIntDigits + 1, "buffer too small for an int");
  buf[kIntDigits] = CharT{'\0'};
  size_t i = kIntDigits;
  const bool negative = n < 0;
  long long v = n;  // 64-bit so that INT_MIN can be negated.
  if (negative) v = -v;
  do {
    buf[--i] = static_cast<CharT>(CharT{'0'} + v % 10);
    v /= 10;
  } while (v != 0);
  if (negative) buf[--i] = CharT{'-'};
  return buf + i;
}

template <typename CharT, size_t N>
[[nodiscard]] const CharT* Uint2lStr(CharT (&buf)[N], unsigned n) {
  static_assert(N >= kUintDigits + 1, "buffer too small for an unsigned int");
  buf[kUintDigits] = CharT{'\0'};
  size_t i = kUintDigits;
  do {
    buf[--i] = static_cast<CharT>(CharT{'0'} + n % 10);
    n /= 10;
  } while (n != 0);
  return buf + i;
}

// Upper-case hexadecimal, no prefix, no leading zeros.
template <typename CharT, size_t N>
[[nodiscard]] const CharT* LPTR2Hex(CharT (&buf)[N], UINT_PTR n) {
  static_assert(N >= kPtrHexDigits + 1, "buffer too small for a pointer");
  constexpr char kDigits[] = "0123456789ABCDEF";
  buf[kPtrHexDigits] = CharT{'\0'};
  size_t i = kPtrHexDigits;
  do {
    buf[--i] = static_cast<CharT>(kDigits[n & 15]);
    n >>= 4;
  } while (n != 0);
  return buf + i;
}

#endif  // ALTSNAP_STRUTIL_H_
