#ifndef ALTSNAP_SETTINGS_KEYBOARD_MODEL_H_
#define ALTSNAP_SETTINGS_KEYBOARD_MODEL_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/strutil.h"

namespace altsnap::settings {
// Keyboard parity inventory (engine defaults): Input/Hotkeys=A4 A5, six
// activation checkboxes plus hidden keys; ModKey=empty (shared with Mouse);
// KeyCombo=0; GrabWithAlt and GrabWithAltB=Nothing (15 catalog actions, custom
// strings/parameters retained); ScrollLockState=0, enable bit 0/invert bit 1;
// UniKeyHoldMenu=0. KBShortcuts/UsePtWindow=0 selects foreground vs pointed
// target. Every other KBShortcuts entry is an action ID (including parameters)
// with decimal low-WORD VK | MOD_* << 8; absent=unbound. Catalog below matches
// all 32 legacy shortcut actions. Selection only displays, capture/manual edits
// only edit, Clear prepares deletion, Set immediately applies the whole draft.
inline bool SameAction(std::wstring_view a, std::wstring_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const auto lower = [](wchar_t c) {
      return c >= L'A' && c <= L'Z' ? c + 32 : c;
    };
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}
struct ShortcutEntry {
  std::wstring action;
  std::wstring raw;
  bool operator==(const ShortcutEntry&) const = default;
};
struct ShortcutEdit {
  std::wstring action;
  std::optional<uint16_t>
      binding;  // Absence deletes the INI key (Clear + Set).
};
inline constexpr size_t kMaxShortcutEntries = 256;
inline bool IsModifier(unsigned key) {
  return key == 0x10 || key == 0x11 || key == 0x12 || key == 0x5b ||
         key == 0x5c || (key >= 0xa0 && key <= 0xa5);
}
inline unsigned ModifierBit(unsigned key) {
  if (key == 0x10 || key == 0xa0 || key == 0xa1) return 4;
  if (key == 0x11 || key == 0xa2 || key == 0xa3) return 2;
  if (key == 0x12 || key == 0xa4 || key == 0xa5) return 1;
  if (key == 0x5b || key == 0x5c) return 8;
  return 0;
}
inline std::optional<unsigned> ParseVirtualKey(std::wstring_view text) {
  if (text.empty() || text.size() > 2) return std::nullopt;
  unsigned result = 0;
  for (wchar_t c : text) {
    const unsigned digit = c >= L'0' && c <= L'9'   ? c - L'0'
                           : c >= L'A' && c <= L'F' ? c - L'A' + 10
                           : c >= L'a' && c <= L'f' ? c - L'a' + 10
                                                    : 256;
    if (digit > 15) return std::nullopt;
    result = result * 16 + digit;
  }
  return result;
}
// Same low-WORD decimal semantics as the runtime; custom raw text is retained.
inline uint16_t ShortcutNumber(std::wstring_view text) {
  return static_cast<uint16_t>(ParseInt(std::wstring(text).c_str()));
}
inline bool SupportedShortcut(uint16_t binding) {
  const unsigned key = binding & 255, mods = binding >> 8;
  if (key < 8 || key == 0xff || key == 0xe7 || key == 0x7b || IsModifier(key) ||
      !mods || mods > 15)
    return false;
  // Reserved/system combinations and capture cancellation are not assignable.
  return key != 0x1b && !(key == 0x2e && (mods & 3) == 3) &&
         !(key == 9 && (mods & 1)) && !(key == 0x20 && (mods & 1)) &&
         !(key == 0x4c && (mods & 8));
}
inline bool ValidShortcutAction(std::wstring_view action) {
  return !action.empty() && action.size() <= 255 &&
         action.find_first_of(L"=\r\n[]\0", 0, 6) == action.npos &&
         action.front() != L';' && action.front() != L'#' &&
         action.front() != L' ' && action.back() != L' ' &&
         action.front() != L'\t' && action.back() != L'\t' &&
         !SameAction(action, L"UsePtWindow");
}
inline bool ValidShortcutEdits(const std::vector<ShortcutEdit>& edits) {
  if (edits.size() > kMaxShortcutEntries) return false;
  for (size_t i = 0; i < edits.size(); ++i) {
    if (!ValidShortcutAction(edits[i].action) ||
        (edits[i].binding && !SupportedShortcut(*edits[i].binding)))
      return false;
    for (size_t j = 0; j < i; ++j)
      if (SameAction(edits[i].action, edits[j].action) ||
          (edits[i].binding && edits[i].binding == edits[j].binding))
        return false;
  }
  return true;
}
inline std::wstring ShortcutConflict(const std::vector<ShortcutEntry>& entries,
                                     std::wstring_view action,
                                     uint16_t binding) {
  for (const auto& entry : entries)
    if (!SameAction(entry.action, action) &&
        ShortcutNumber(entry.raw) == binding)
      return entry.action;
  return {};
}
inline void SetShortcutEntry(std::vector<ShortcutEntry>& entries,
                             std::wstring action,
                             std::optional<uint16_t> binding) {
  const auto found =
      std::find_if(entries.begin(), entries.end(),
                   [&](const auto& e) { return SameAction(e.action, action); });
  if (!binding) {
    if (found != entries.end()) entries.erase(found);
  } else if (found != entries.end()) {
    if (ShortcutNumber(found->raw) != *binding)
      found->raw = std::to_wstring(*binding);
  } else
    entries.push_back({std::move(action), std::to_wstring(*binding)});
}
inline std::vector<ShortcutEdit> ShortcutChanges(
    const std::vector<ShortcutEntry>& base,
    const std::vector<ShortcutEntry>& edited) {
  std::vector<ShortcutEdit> result;
  for (const auto& entry : base)
    if (std::none_of(edited.begin(), edited.end(), [&](const auto& e) {
          return SameAction(e.action, entry.action);
        }))
      result.push_back({entry.action, std::nullopt});
  for (const auto& entry : edited) {
    const auto old = std::find_if(base.begin(), base.end(), [&](const auto& e) {
      return SameAction(e.action, entry.action);
    });
    if (old == base.end() || old->raw != entry.raw)
      result.push_back({entry.action, ShortcutNumber(entry.raw)});
  }
  return result;
}
// UI-thread-owned pure capture state. The adapter swallows every event until
// all pressed keys are released, including after Escape or focus cancellation.
class ShortcutCapture {
 public:
  void Key(unsigned key, bool down) {
    if (key >= pressed_.size()) return;
    const bool repeated = pressed_[key];
    pressed_[key] = down;
    if (down && !repeated && !finished_) {
      rejected_ = false;
      if (key == 0x1b)
        Cancel();
      else if (!IsModifier(key)) {
        const unsigned mods = Modifiers();
        const uint16_t binding = static_cast<uint16_t>(key | (mods << 8));
        if (SupportedShortcut(binding)) {
          value_ = binding;
          finished_ = true;
        } else
          rejected_ = true;
      }
    }
  }
  void Cancel() {
    value_.reset();
    finished_ = true;
  }
  bool Ready() const {
    return finished_ &&
           !std::ranges::any_of(pressed_, [](bool p) { return p; });
  }
  bool Rejected() const { return rejected_; }
  std::optional<uint16_t> Value() const { return value_; }
  unsigned Modifiers() const {
    unsigned result = 0;
    for (unsigned i = 0; i < pressed_.size(); ++i)
      if (pressed_[i]) result |= ModifierBit(i);
    return result;
  }

 private:
  std::array<bool, 256> pressed_{};
  bool finished_ = false, rejected_ = false;
  std::optional<uint16_t> value_;
};
inline constexpr std::array kGrabActions{
    L"Move",    L"Resize",     L"Close",       L"Minimize",   L"Maximize",
    L"Lower",   L"Roll",       L"AlwaysOnTop", L"Borderless", L"Center",
    L"Center2", L"MaximizeHV", L"MinAllOther", L"Menu",       L"Nothing"};
inline constexpr std::array kShortcutActions{
    L"Kill",         L"Pause",        L"Resume",      L"ASOnOff",
    L"Close",        L"Minimize",     L"Maximize",    L"Lower",
    L"Roll",         L"Transparency", L"AlwaysOnTop", L"Borderless",
    L"Center",       L"Center2",      L"Mute",        L"Menu",
    L"NStacked",     L"NStacked2",    L"PStacked",    L"PStacked2",
    L"StackList",    L"StackList2",   L"AltTabList",  L"AltTabFullList",
    L"ExtendTNEdge", L"MoveTNEdge",   L"MZone",       L"XZone",
    L"Step",         L"Focus",        L"NLayout",     L"PLayout"};
}  // namespace altsnap::settings
#endif
