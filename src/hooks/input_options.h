#ifndef ALTSNAP_HOOKS_INPUT_OPTIONS_H_
#define ALTSNAP_HOOKS_INPUT_OPTIONS_H_

#include <array>
#include <cstdint>
#include <span>

#include "common/ini_section.h"
#include "hooks/hooks.h"

namespace altsnap::hooks {

enum class ActionContext { kNormal, kTitlebar, kMoving, kResizing };

// Each context has a primary action and an action with the modifier held.
struct ActionBindings {
  std::array<action_t, 2> normal{};
  std::array<action_t, 2> titlebar{};
  std::array<action_t, 2> moving{};
  std::array<action_t, 2> resizing{};

  [[nodiscard]] action_t Get(ActionContext context, bool secondary) const;
};

struct InputActions {
  std::array<ActionBindings, 24> mouse{};
  ActionBindings grab_with_alt;
  ActionBindings move_up;
  ActionBindings resize_up;

  // Win32 button codes run from 2 (LMB) through 25 (horizontal wheel up).
  [[nodiscard]] action_t GetMouse(int button, ActionContext context,
                                  bool secondary) const;
};

[[nodiscard]] InputActions ReadButtonActions(const IniSection& section);

// Clears the destination and reads at most size()-1 bytes, reserving a trailing
// zero. Empty destinations are allowed. Upstream: readhotkeys.
size_t ReadHotkeys(const IniSection& section, const char* key,
                   const wchar_t* def, std::span<UCHAR> keys);

enum class ColorChannels { kRgb, kRgba };
// Missing components are zero; excess components are ignored.
[[nodiscard]] uint32_t ReadColor(const IniSection& section, const char* key,
                                 const wchar_t* def, ColorChannels channels);

}  // namespace altsnap::hooks
#endif  // ALTSNAP_HOOKS_INPUT_OPTIONS_H_
