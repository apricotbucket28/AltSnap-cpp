#ifndef ALTSNAP_HOOKS_HOTKEY_MESSAGE_H_
#define ALTSNAP_HOOKS_HOTKEY_MESSAGE_H_

#include <optional>
#include <span>

#include "hooks/hooks.h"

namespace altsnap::hooks {

inline constexpr WPARAM kFirstRegisteredHotkey = 0xC001;
inline constexpr size_t kMaxRegisteredHotkeys = 64;

struct HotkeyAction {
  action_t action;
  bool point_window;
};

// Validate message values at full pointer width before indexing or narrowing.
[[nodiscard]] inline std::optional<HotkeyAction> DecodeHotkeyMessage(
    WPARAM id, LPARAM payload, std::span<const action_t> registered,
    bool use_point_window) {
  if (id >= kFirstRegisteredHotkey) {
    const auto index = id - kFirstRegisteredHotkey;
    if (index >= registered.size()) return std::nullopt;
    const auto action = registered[index];
    if (action.ac == AC_NONE || action.ac >= AC_MAXVALUE) return std::nullopt;
    return HotkeyAction{action, use_point_window};
  }
  const bool point_window = id > 0x1000 && id < 0x2000;
  if (!(id > 0 && id < 0x1000) && !point_window) return std::nullopt;
  const auto action = UnpackAction(payload);
  const auto expected = point_window ? id - 0x1000 : id;
  if (action.ac == AC_NONE || action.ac >= AC_MAXVALUE ||
      expected != action.ac) {
    return std::nullopt;
  }
  return HotkeyAction{action, point_window};
}

}  // namespace altsnap::hooks
#endif  // ALTSNAP_HOOKS_HOTKEY_MESSAGE_H_
