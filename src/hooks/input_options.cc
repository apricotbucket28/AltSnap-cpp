#include "hooks/input_options.h"

#include <algorithm>
#include <string>
#include <utility>

namespace altsnap::hooks {
namespace {

size_t ReadHexBytes(const wchar_t* text, std::span<UCHAR> bytes) {
  size_t count = 0;
  if (!text) return count;
  while (*text && count < bytes.size()) {
    bytes[count++] = HexToUnsigned(text);
    // Preserve the upstream token/separator rules and low-byte conversion.
    while (*text && *text >= L'0') ++text;
    while (*text && *text < L'0') ++text;
  }
  return count;
}

ActionBindings ReadBindings(const IniSection& section, const char* name) {
  ActionBindings result;
  struct ContextDescriptor {
    const char* suffix;
    std::array<action_t, 2> ActionBindings::* member;
  };
  constexpr std::array<ContextDescriptor, 4> contexts = {{
      {"", &ActionBindings::normal},
      {"T", &ActionBindings::titlebar},
      {"M", &ActionBindings::moving},
      {"R", &ActionBindings::resizing},
  }};
  for (const auto& context : contexts) {
    auto& pair = result.*context.member;
    const std::string key = std::string(name) + context.suffix;
    pair[0] = MapActionW(section.GetString(key.c_str(), L"Nothing"));
    pair[1] = MapActionW(section.GetString((key + "B").c_str(), L"Nothing"));
  }
  return result;
}

}  // namespace

action_t ActionBindings::Get(ActionContext context, bool secondary) const {
  switch (context) {
    case ActionContext::kNormal:
      return normal[secondary];
    case ActionContext::kTitlebar:
      return titlebar[secondary];
    case ActionContext::kMoving:
      return moving[secondary];
    case ActionContext::kResizing:
      return resizing[secondary];
  }
  return k_action_none;
}

action_t InputActions::GetMouse(int button, ActionContext context,
                                bool secondary) const {
  if (button < 2 || button - 2 >= static_cast<int>(mouse.size())) {
    return k_action_none;
  }
  return mouse[button - 2].Get(context, secondary);
}

// Upstream: readbuttonactions. Names and suffixes are the INI interface.
InputActions ReadButtonActions(const IniSection& section) {
  constexpr std::array names = {
      "LMB",  "RMB",  "MMB",    "MB4",      "MB5",     "MB6",
      "MB7",  "MB8",  "MB9",    "MB10",     "MB11",    "MB12",
      "MB13", "MB14", "MB15",   "MB16",     "MB17",    "MB18",
      "MB19", "MB20", "Scroll", "ScrollUp", "HScroll", "HScrollUp"};
  InputActions result;
  static_assert(names.size() == result.mouse.size());
  for (size_t i = 0; i < names.size(); ++i) {
    result.mouse[i] = ReadBindings(section, names[i]);
  }
  // An unspecified (or Nothing) upward wheel action inherits the downward one.
  for (const auto& [down, up] : {std::pair{20, 21}, std::pair{22, 23}}) {
    for (auto member : {&ActionBindings::normal, &ActionBindings::titlebar,
                        &ActionBindings::moving, &ActionBindings::resizing}) {
      for (size_t secondary = 0; secondary < 2; ++secondary) {
        auto& action = (result.mouse[up].*member)[secondary];
        if (action.ac == AC_NONE) {
          action = (result.mouse[down].*member)[secondary];
        }
      }
    }
  }
  result.grab_with_alt = ReadBindings(section, "GrabWithAlt");
  result.move_up = ReadBindings(section, "MoveUp");
  result.resize_up = ReadBindings(section, "ResizeUp");
  return result;
}

size_t ReadHotkeys(const IniSection& section, const char* key,
                   const wchar_t* def, std::span<UCHAR> keys) {
  std::ranges::fill(keys, 0);
  if (keys.empty()) return 0;
  return ReadHexBytes(section.GetString(key, def), keys.first(keys.size() - 1));
}

uint32_t ReadColor(const IniSection& section, const char* key,
                   const wchar_t* def, ColorChannels channels) {
  std::array<UCHAR, 4> bytes{};
  const size_t count = channels == ColorChannels::kRgba ? 4 : 3;
  ReadHexBytes(section.GetString(key, def), std::span(bytes).first(count));
  uint32_t value = 0;
  for (size_t i = 0; i < count; ++i) value |= uint32_t{bytes[i]} << (8 * i);
  return value;
}

}  // namespace altsnap::hooks
