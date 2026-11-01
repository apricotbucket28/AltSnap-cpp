#ifndef ALTSNAP_SETTINGS_MODEL_H_
#define ALTSNAP_SETTINGS_MODEL_H_

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "settings/keyboard_model.h"
#include "settings/mouse_model.h"

namespace altsnap::settings {
enum class Option : size_t {
  kAutoFocus,
  kAero,
  kSmartSizing,
  kSmartCandidates,
  kKeepSnapped,
  kStickyResize,
  kAutoSnap,
  kAutoRemaximize,
  kAeroTopMaximizes,
  kAeroDoubleClickShift,
  kMultipleInstances,
  kFullScreen,
  kBLMaximized,
  kFancyZone,
  kMaximizeOnClick,
  kRestoreOnClick,
  kTopmostIndicator,
  kCenterFraction,
  kSidesFraction,
  kAeroHoffset,
  kAeroVoffset,
  kSnapThreshold,
  kAeroThreshold,
  kSnapGap,
  kAeroMaxSpeed,
  kAeroSpeedTau,
  kMoveTrans,
  kTitlebarWithoutAlt,
  kTitlebarWithAlt,
  kLongClickMove,
  kKeyCombo,
  kScrollLockEnabled,
  kScrollLockInvert,
  kUniKeyHoldMenu,
  kUsePtWindow,
  kCount
};
inline constexpr size_t kOptionCount = static_cast<size_t>(Option::kCount);
enum class ValueKind { kToggle, kChoice, kNumber, kSignedNumber };
enum class Storage { kByte, kWord, kSignedByte, kDword };
struct OptionDefinition {
  ValueKind kind;
  const wchar_t* key;
  uint32_t engine_default;
  // Zero replaces the scalar; a mask preserves other bits.
  uint32_t mask;
  uint32_t maximum;
  const wchar_t* section = L"General";
  Storage storage = Storage::kByte;
};
// Engine defaults, not the legacy dialog or shipped template defaults.
inline constexpr std::array<OptionDefinition, kOptionCount> kOptions{
    {{ValueKind::kToggle, L"AutoFocus", 0, 0, 1},
     {ValueKind::kToggle, L"Aero", 1, 0, 1},
     {ValueKind::kToggle, L"SmartAero", 1, 1, 1},
     {ValueKind::kToggle, L"SmartAero", 1, 2, 1},
     {ValueKind::kToggle, L"SmartAero", 1, 4, 1},
     {ValueKind::kChoice, L"StickyResize", 0, 3, 2},
     {ValueKind::kChoice, L"AutoSnap", 0, 0, 3},
     {ValueKind::kToggle, L"AutoRemaximize", 0, 0, 1, L"Advanced"},
     {ValueKind::kToggle, L"AeroTopMaximizes", 1, 1, 1, L"Advanced"},
     {ValueKind::kToggle, L"AeroTopMaximizes", 1, 2, 1, L"Advanced"},
     {ValueKind::kToggle, L"MultipleInstances", 0, 0, 1, L"Advanced",
      Storage::kDword},
     {ValueKind::kToggle, L"FullScreen", 1, 0, 1, L"Advanced"},
     {ValueKind::kToggle, L"BLMaximized", 0, 0, 1, L"Advanced"},
     {ValueKind::kToggle, L"FancyZone", 0, 0, 1, L"Zones"},
     {ValueKind::kToggle, L"MMMaximize", 1, 1, 1, L"General"},
     {ValueKind::kToggle, L"MMMaximize", 1, 2, 1, L"General"},
     {ValueKind::kToggle, L"TopmostIndicator", 0, 0, 1, L"Advanced"},
     {ValueKind::kNumber, L"CenterFraction", 24, 0, 100, L"General"},
     {ValueKind::kNumber, L"SidesFraction", 255, 0, 255, L"General"},
     {ValueKind::kNumber, L"AeroHoffset", 50, 0, 100, L"General"},
     {ValueKind::kNumber, L"AeroVoffset", 50, 0, 100, L"General"},
     {ValueKind::kNumber, L"SnapThreshold", 20, 0, 255, L"Advanced"},
     {ValueKind::kNumber, L"AeroThreshold", 5, 0, 255, L"Advanced"},
     {ValueKind::kSignedNumber, L"SnapGap", 0, 0, 127, L"Advanced",
      Storage::kSignedByte},
     {ValueKind::kNumber, L"AeroMaxSpeed", 65535, 0, 65535, L"Advanced",
      Storage::kWord},
     {ValueKind::kNumber, L"AeroSpeedTau", 64, 0, 255, L"Advanced"},
     {ValueKind::kNumber, L"MoveTrans", 255, 0, 255, L"General"},
     {ValueKind::kToggle, L"TTBActions", 0, 1, 1, L"Input"},
     {ValueKind::kToggle, L"TTBActions", 0, 2, 1, L"Input"},
     {ValueKind::kToggle, L"LongClickMove", 0, 0, 1, L"Input"},
     {ValueKind::kToggle, L"KeyCombo", 0, 0, 1, L"Input"},
     {ValueKind::kToggle, L"ScrollLockState", 0, 1, 1, L"Input"},
     {ValueKind::kToggle, L"ScrollLockState", 0, 2, 1, L"Input"},
     {ValueKind::kToggle, L"UniKeyHoldMenu", 0, 0, 1, L"Input"},
     {ValueKind::kToggle, L"UsePtWindow", 0, 0, 1, L"KBShortcuts"}}};
inline constexpr size_t Index(Option option) {
  return static_cast<size_t>(option);
}
static_assert(kOptionCount < 64);  // Bounded wire change mask.
inline int32_t SignedNumber(uint32_t value) {
  return std::bit_cast<int32_t>(value);
}
inline bool ValidValue(size_t index, uint32_t value) {
  return kOptions[index].kind == ValueKind::kSignedNumber
             ? SignedNumber(value) >= -128 && SignedNumber(value) <= 127
             : value <= kOptions[index].maximum;
}
// Strict decimal editing, independent of the permissive legacy INI reader.
inline std::optional<uint32_t> ParseNumber(Option option,
                                           std::wstring_view text) {
  const size_t index = Index(option);
  const bool negative = !text.empty() && text.front() == L'-';
  if (negative) text.remove_prefix(1);
  if (text.empty() || text.size() > 5) return std::nullopt;
  uint32_t number = 0;
  for (wchar_t c : text) {
    if (c < L'0' || c > L'9') return std::nullopt;
    number = number * 10 + static_cast<uint32_t>(c - L'0');
  }
  if (negative) {
    if (kOptions[index].kind != ValueKind::kSignedNumber || number > 128)
      return std::nullopt;
    number = std::bit_cast<uint32_t>(-static_cast<int32_t>(number));
  }
  return ValidValue(index, number) ? std::optional{number} : std::nullopt;
}
struct Value {
  uint32_t effective = 0;
  bool present = false;
  std::wstring raw;
};
struct Values : std::array<Value, kOptionCount> {
  TextValues text;
  std::vector<ShortcutEntry> shortcuts;
};
struct Patch {
  uint64_t changed = 0;
  std::array<uint32_t, kOptionCount> values{};
  uint64_t text_changed = 0;
  std::array<std::wstring, kTextCount> text{};
  std::vector<ShortcutEdit> shortcuts;
  bool Any() const { return changed || text_changed || !shortcuts.empty(); }
  bool Valid() const {
    if (!ValidShortcutEdits(shortcuts)) return false;
    if (text_changed >> kTextCount) return false;
    for (size_t i = 0; i < kTextCount; ++i)
      if ((text_changed & (uint64_t{1} << i)) && !ValidTextOption(i, text[i]))
        return false;
    if (changed >> kOptionCount) return false;
    for (size_t i = 0; i < kOptionCount; ++i)
      if ((changed & (uint64_t{1} << i)) && !ValidValue(i, values[i]))
        return false;
    return true;
  }
};
// UI-thread-owned draft. Unknown persisted values remain valid until edited.
class Draft {
 public:
  void Load(const Values& values) {
    shortcuts_base_ = shortcuts_edited_ = values.shortcuts;
    for (size_t i = 0; i < kOptionCount; ++i)
      base_[i] = edited_[i] = values[i].effective;
    for (size_t i = 0; i < kTextCount; ++i)
      text_base_[i] = text_edited_[i] = values.text[i].effective;
  }
  void Set(Option option, uint32_t value) { edited_[Index(option)] = value; }
  uint32_t Get(Option option) const { return edited_[Index(option)]; }
  void SetText(size_t index, std::wstring value) {
    text_edited_[index] = std::move(value);
  }
  const std::wstring& GetText(size_t index) const {
    return text_edited_[index];
  }
  const std::vector<ShortcutEntry>& Shortcuts() const {
    return shortcuts_edited_;
  }
  void SetShortcut(std::wstring action, std::optional<uint16_t> binding) {
    SetShortcutEntry(shortcuts_edited_, action, binding);
    // Returning to the original binding restores its exact legacy spelling.
    if (!binding) return;
    for (const auto& original : shortcuts_base_) {
      if (!SameAction(original.action, action) ||
          ShortcutNumber(original.raw) != *binding)
        continue;
      for (auto& edited : shortcuts_edited_)
        if (SameAction(edited.action, action)) edited.raw = original.raw;
      break;
    }
  }
  Patch Changes() const {
    Patch patch;
    patch.shortcuts = ShortcutChanges(shortcuts_base_, shortcuts_edited_);
    patch.values = edited_;
    for (size_t i = 0; i < kOptionCount; ++i)
      if (edited_[i] != base_[i]) patch.changed |= uint64_t{1} << i;
    patch.text = text_edited_;
    for (size_t i = 0; i < kTextCount; ++i)
      if (text_edited_[i] != text_base_[i])
        patch.text_changed |= uint64_t{1} << i;
    return patch;
  }

 private:
  std::vector<ShortcutEntry> shortcuts_base_, shortcuts_edited_;
  std::array<uint32_t, kOptionCount> base_{}, edited_{};
  std::array<std::wstring, kTextCount> text_base_{}, text_edited_{};
};
inline Patch SingleChange(Option option, uint32_t value) {
  Patch patch;
  patch.changed = uint64_t{1} << Index(option);
  patch.values[Index(option)] = value;
  return patch;
}
}  // namespace altsnap::settings
#endif
