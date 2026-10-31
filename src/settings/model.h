#ifndef ALTSNAP_SETTINGS_MODEL_H_
#define ALTSNAP_SETTINGS_MODEL_H_

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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
     {ValueKind::kNumber, L"MoveTrans", 255, 0, 255, L"General"}}};
inline constexpr size_t Index(Option option) {
  return static_cast<size_t>(option);
}
static_assert(kOptionCount < 32);  // Bounded wire change mask.
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
using Values = std::array<Value, kOptionCount>;
struct Patch {
  uint32_t changed = 0;
  std::array<uint32_t, kOptionCount> values{};
  bool Valid() const {
    if (changed >> kOptionCount) return false;
    for (size_t i = 0; i < kOptionCount; ++i)
      if ((changed & (1u << i)) && !ValidValue(i, values[i])) return false;
    return true;
  }
};
// UI-thread-owned draft. Unknown persisted values remain valid until edited.
class Draft {
 public:
  void Load(const Values& values) {
    for (size_t i = 0; i < kOptionCount; ++i)
      base_[i] = edited_[i] = values[i].effective;
  }
  void Set(Option option, uint32_t value) { edited_[Index(option)] = value; }
  uint32_t Get(Option option) const { return edited_[Index(option)]; }
  Patch Changes() const {
    Patch patch;
    patch.values = edited_;
    for (size_t i = 0; i < kOptionCount; ++i)
      if (edited_[i] != base_[i]) patch.changed |= 1u << i;
    return patch;
  }

 private:
  std::array<uint32_t, kOptionCount> base_{}, edited_{};
};
inline Patch SingleChange(Option option, uint32_t value) {
  Patch patch;
  patch.changed = 1u << Index(option);
  patch.values[Index(option)] = value;
  return patch;
}
}  // namespace altsnap::settings
#endif
