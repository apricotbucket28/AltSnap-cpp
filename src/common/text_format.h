// Pure text-building helpers for the user interface. They replace the old
// fixed-buffer code (AppendString() chains, Int2lStr(), LPTR2Hex(), ...) and do
// not depend on any global state, so they are covered by the unit tests.

#ifndef ALTSNAP_COMMON_TEXT_FORMAT_H_
#define ALTSNAP_COMMON_TEXT_FORMAT_H_

#include <windows.h>

#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/strutil.h"

namespace altsnap {

// "left,top,right,bottom," -- the format of a Zone<N> entry in the ini file.
// Upstream: RectToStr
[[nodiscard]] inline std::wstring FormatRect(const RECT& rc) {
  return std::format(L"{},{},{},{},", rc.left, rc.top, rc.right, rc.bottom);
}

// Parses "left,top,right,bottom," (the format written by FormatRect(); each of
// the four numbers must be followed by a comma). Returns nothing if the text is
// malformed.
[[nodiscard]] inline std::optional<RECT> ParseRect(std::wstring_view text) {
  long values[4];
  for (long& value : values) {
    const size_t comma = text.find(L',');
    if (comma == std::wstring_view::npos || comma == 0) return std::nullopt;
    value = ParseInt(std::wstring(text.substr(0, comma)).c_str());
    text.remove_prefix(comma + 1);
  }
  return RECT{values[0], values[1], values[2], values[3]};
}

// Upper-case hexadecimal without prefix or leading zeros ("1F", "0").
// Upstream: LPTR2Hex
[[nodiscard]] inline std::wstring ToHex(UINT_PTR value) {
  return std::format(L"{:X}", value);
}

// Lists of keys in the ini file are space-separated hexadecimal virtual-key
// codes, e.g. "A4 A5". These helpers edit such a list.
[[nodiscard]] inline std::vector<std::pair<size_t, size_t>> KeyListTokens(
    std::wstring_view keys) {
  // (offset, length) of each token including the spaces that follow it.
  std::vector<std::pair<size_t, size_t>> tokens;
  size_t pos = 0;
  while (pos < keys.size()) {
    size_t end = keys.find(L' ', pos);
    if (end == std::wstring_view::npos) end = keys.size();
    size_t next = end;
    while (next < keys.size() && keys[next] == L' ') ++next;
    tokens.emplace_back(pos, next - pos);
    pos = next;
  }
  return tokens;
}

[[nodiscard]] inline unsigned KeyListValue(std::wstring_view keys,
                                           std::pair<size_t, size_t> token) {
  return HexToUnsigned(
      std::wstring(keys.substr(token.first, token.second)).c_str());
}

[[nodiscard]] inline bool IsKeyInList(std::wstring_view keys, unsigned vkey) {
  for (const auto& token : KeyListTokens(keys)) {
    if (KeyListValue(keys, token) == vkey) return true;
  }
  return false;
}

// Upstream: AddvKeytoList
[[nodiscard]] inline std::wstring AddKeyToList(std::wstring keys,
                                               unsigned vkey) {
  if (IsKeyInList(keys, vkey)) return keys;
  if (!keys.empty()) keys += L' ';
  return keys + ToHex(vkey);
}

// Removes the first occurrence of `vkey`.
[[nodiscard]] inline std::wstring RemoveKeyFromList(std::wstring keys,
                                                    unsigned vkey) {
  for (const auto& token : KeyListTokens(keys)) {
    if (KeyListValue(keys, token) == vkey) {
      keys.erase(token.first, token.second);
      break;
    }
  }
  while (!keys.empty() && keys.back() == L' ') keys.pop_back();
  return keys;
}

// Removes the keyboard accelerator marker ('&') from a menu/dialog string. An
// accelerator written in parentheses, as CJK translations do, is removed
// together with the space that follows it: "Test &window" -> "Test window",
// "Window(&W)" -> "Window", "Window(&W) now" -> "Windownow".
// Upstream: lstrcpy_noaccel (which read past the end of the string when a '('
// had no closing ')').
[[nodiscard]] inline std::wstring RemoveAccelerator(std::wstring_view text) {
  std::wstring result;
  result.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == L'(') {
      const size_t close = text.find(L')', i);
      if (close == std::wstring_view::npos) break;  // Drop the unclosed tail.
      i = close + 1;
      if (i < text.size() && text[i] == L' ') ++i;  // Space after ')'.
    } else {
      if (text[i] != L'&') result += text[i];
      ++i;
    }
  }
  return result;
}

// One line of the "test window" key log: "vK=1B D sC=01, LP=1B0001, Esc".
// `key_name` is the localized name of the key, if Windows knows one.
[[nodiscard]] inline std::wstring FormatKeyEvent(WPARAM wparam, LPARAM lparam,
                                                 std::wstring_view key_name) {
  const wchar_t* phase = (lparam & (1u << 31))   ? L" U"
                         : (lparam & (1u << 30)) ? L" R"
                                                 : L" D";
  std::wstring text = std::format(
      L"vK={}{} sC={}, LP={}", ToHex(static_cast<BYTE>(wparam)), phase,
      ToHex(HIWORD(lparam) & 0x00FF), ToHex(static_cast<UINT_PTR>(lparam)));
  if (!key_name.empty()) {
    text += L", ";
    text += key_name;
  }
  return text;
}

// One line of the "test window" mouse log, e.g. "Wheel +120 (10, 20),
// WP=780000". `wheel` selects the wheel format, which includes the scroll
// amount.
[[nodiscard]] inline std::wstring FormatMouseEvent(std::wstring_view button,
                                                   bool wheel, WPARAM wparam,
                                                   LPARAM lparam) {
  std::wstring text(button);
  if (wheel) {
    const short delta = static_cast<short>(HIWORD(wparam));
    text += delta >= 0 ? L" +" : L" -";
    text += std::to_wstring(std::abs(static_cast<int>(delta)));
  }
  text += std::format(L" ({}, {}), WP={}", static_cast<short>(LOWORD(lparam)),
                      static_cast<short>(HIWORD(lparam)),
                      ToHex(static_cast<UINT_PTR>(wparam)));
  return text;
}

}  // namespace altsnap

#endif  // ALTSNAP_COMMON_TEXT_FORMAT_H_
