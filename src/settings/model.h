#ifndef ALTSNAP_SETTINGS_MODEL_H_
#define ALTSNAP_SETTINGS_MODEL_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace altsnap::settings {
enum class Option : size_t {
  kAutoFocus,
  kAero,
  kSmartSizing,
  kSmartCandidates,
  kKeepSnapped,
  kStickyResize,
  kAutoSnap,
  kCount
};
inline constexpr size_t kOptionCount = static_cast<size_t>(Option::kCount);
enum class ValueKind { kToggle, kChoice };
struct OptionDefinition {
  ValueKind kind;
  const wchar_t* key;
  uint32_t engine_default;
  // Zero replaces the scalar; a mask preserves other bits.
  uint32_t mask;
  uint32_t maximum;
};
// Engine defaults, not the legacy dialog or shipped template defaults.
inline constexpr std::array<OptionDefinition, kOptionCount> kOptions{
    {{ValueKind::kToggle, L"AutoFocus", 0, 0, 1},
     {ValueKind::kToggle, L"Aero", 1, 0, 1},
     {ValueKind::kToggle, L"SmartAero", 1, 1, 1},
     {ValueKind::kToggle, L"SmartAero", 1, 2, 1},
     {ValueKind::kToggle, L"SmartAero", 1, 4, 1},
     {ValueKind::kChoice, L"StickyResize", 0, 3, 2},
     {ValueKind::kChoice, L"AutoSnap", 0, 0, 3}}};
inline constexpr size_t Index(Option option) {
  return static_cast<size_t>(option);
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
      if ((changed & (1u << i)) && values[i] > kOptions[i].maximum)
        return false;
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
