#ifndef ALTSNAP_SETTINGS_MOUSE_MODEL_H_
#define ALTSNAP_SETTINGS_MOUSE_MODEL_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace altsnap::settings {
// Legacy Mouse surface: five contexts, seven inputs, plus release actions in
// the first three contexts. Extra engine bindings remain untouched INI options.
enum class MouseContext { kPrimary, kAlternate, kTitlebar, kMoving, kResizing };
enum class MouseInput {
  kLeft,
  kMiddle,
  kRight,
  kButton4,
  kButton5,
  kWheel,
  kHorizontalWheel,
  kMoveUp,
  kResizeUp
};
struct BindingDefinition {
  const wchar_t* key;
  const wchar_t* label;
  MouseContext context;
  MouseInput input;
};
inline constexpr std::array<BindingDefinition, 41> kBindings{{
    {L"LMB", L"Left button", MouseContext::kPrimary, MouseInput::kLeft},
    {L"MMB", L"Middle button", MouseContext::kPrimary, MouseInput::kMiddle},
    {L"RMB", L"Right button", MouseContext::kPrimary, MouseInput::kRight},
    {L"MB4", L"Button 4 (back)", MouseContext::kPrimary, MouseInput::kButton4},
    {L"MB5", L"Button 5 (forward)", MouseContext::kPrimary,
     MouseInput::kButton5},
    {L"Scroll", L"Vertical wheel", MouseContext::kPrimary, MouseInput::kWheel},
    {L"HScroll", L"Horizontal wheel", MouseContext::kPrimary,
     MouseInput::kHorizontalWheel},
    {L"MoveUp", L"After releasing move", MouseContext::kPrimary,
     MouseInput::kMoveUp},
    {L"ResizeUp", L"After releasing resize", MouseContext::kPrimary,
     MouseInput::kResizeUp},
    {L"LMBB", L"Left button", MouseContext::kAlternate, MouseInput::kLeft},
    {L"MMBB", L"Middle button", MouseContext::kAlternate, MouseInput::kMiddle},
    {L"RMBB", L"Right button", MouseContext::kAlternate, MouseInput::kRight},
    {L"MB4B", L"Button 4 (back)", MouseContext::kAlternate,
     MouseInput::kButton4},
    {L"MB5B", L"Button 5 (forward)", MouseContext::kAlternate,
     MouseInput::kButton5},
    {L"ScrollB", L"Vertical wheel", MouseContext::kAlternate,
     MouseInput::kWheel},
    {L"HScrollB", L"Horizontal wheel", MouseContext::kAlternate,
     MouseInput::kHorizontalWheel},
    {L"MoveUpB", L"After releasing move", MouseContext::kAlternate,
     MouseInput::kMoveUp},
    {L"ResizeUpB", L"After releasing resize", MouseContext::kAlternate,
     MouseInput::kResizeUp},
    {L"LMBT", L"Left button", MouseContext::kTitlebar, MouseInput::kLeft},
    {L"MMBT", L"Middle button", MouseContext::kTitlebar, MouseInput::kMiddle},
    {L"RMBT", L"Right button", MouseContext::kTitlebar, MouseInput::kRight},
    {L"MB4T", L"Button 4 (back)", MouseContext::kTitlebar,
     MouseInput::kButton4},
    {L"MB5T", L"Button 5 (forward)", MouseContext::kTitlebar,
     MouseInput::kButton5},
    {L"ScrollT", L"Vertical wheel", MouseContext::kTitlebar,
     MouseInput::kWheel},
    {L"HScrollT", L"Horizontal wheel", MouseContext::kTitlebar,
     MouseInput::kHorizontalWheel},
    {L"MoveUpT", L"After releasing move", MouseContext::kTitlebar,
     MouseInput::kMoveUp},
    {L"ResizeUpT", L"After releasing resize", MouseContext::kTitlebar,
     MouseInput::kResizeUp},
    {L"LMBM", L"Left button", MouseContext::kMoving, MouseInput::kLeft},
    {L"MMBM", L"Middle button", MouseContext::kMoving, MouseInput::kMiddle},
    {L"RMBM", L"Right button", MouseContext::kMoving, MouseInput::kRight},
    {L"MB4M", L"Button 4 (back)", MouseContext::kMoving, MouseInput::kButton4},
    {L"MB5M", L"Button 5 (forward)", MouseContext::kMoving,
     MouseInput::kButton5},
    {L"ScrollM", L"Vertical wheel", MouseContext::kMoving, MouseInput::kWheel},
    {L"HScrollM", L"Horizontal wheel", MouseContext::kMoving,
     MouseInput::kHorizontalWheel},
    {L"LMBR", L"Left button", MouseContext::kResizing, MouseInput::kLeft},
    {L"MMBR", L"Middle button", MouseContext::kResizing, MouseInput::kMiddle},
    {L"RMBR", L"Right button", MouseContext::kResizing, MouseInput::kRight},
    {L"MB4R", L"Button 4 (back)", MouseContext::kResizing,
     MouseInput::kButton4},
    {L"MB5R", L"Button 5 (forward)", MouseContext::kResizing,
     MouseInput::kButton5},
    {L"ScrollR", L"Vertical wheel", MouseContext::kResizing,
     MouseInput::kWheel},
    {L"HScrollR", L"Horizontal wheel", MouseContext::kResizing,
     MouseInput::kHorizontalWheel},
}};
inline constexpr size_t kHotclicks = kBindings.size();
inline constexpr size_t kModKey = kHotclicks + 1;
inline constexpr size_t kHotkeys = kModKey + 1;
inline constexpr size_t kGrabWithAlt = kHotkeys + 1;
inline constexpr size_t kGrabWithAltB = kGrabWithAlt + 1;
inline constexpr size_t kTextCount = kGrabWithAltB + 1;
inline const wchar_t* TextKey(size_t index) {
  return index < kBindings.size() ? kBindings[index].key
         : index == kHotclicks    ? L"Hotclicks"
         : index == kModKey       ? L"ModKey"
         : index == kHotkeys      ? L"Hotkeys"
         : index == kGrabWithAlt  ? L"GrabWithAlt"
                                  : L"GrabWithAltB";
}
inline std::wstring TextDefault(size_t index) {
  return index < kBindings.size() || index == kGrabWithAlt ||
                 index == kGrabWithAltB
             ? L"Nothing"
         : index == kHotkeys ? L"A4 A5"
                             : L"";
}
// Do not restrict action IDs to the catalog: custom IDs and parameterized
// actions are a supported INI interface. Reject profile-API transformations.
inline bool ValidText(std::wstring_view text) {
  return text.size() <= 4095 && text.find_first_of(L"\r\n") == text.npos &&
         text.find(L'\0') == text.npos &&
         (text.empty() || (text.front() != L' ' && text.back() != L' ' &&
                           text.front() != L'\t' && text.back() != L'\t' &&
                           text.front() != L'\"' && text.front() != L'\''));
}
// Explicit builder; merely viewing a parameterized/custom action never rewrites
// it.
inline std::optional<std::wstring> BuildAction(std::wstring_view action,
                                               int flag,
                                               std::wstring_view amount) {
  if (flag < 0 || flag > 6) return std::nullopt;
  if (!amount.empty()) {
    auto digits = amount;
    const bool negative = digits.front() == L'-';
    if (negative) digits.remove_prefix(1);
    if (digits.empty() || digits.size() > 5) return std::nullopt;
    uint32_t number = 0;
    for (wchar_t c : digits) {
      if (c < L'0' || c > L'9') return std::nullopt;
      number = number * 10 + static_cast<uint32_t>(c - L'0');
    }
    if (number > (negative ? 32768u : 32767u)) return std::nullopt;
  }
  std::wstring result(action.substr(0, action.find_first_of(L"_=")));
  if (result.empty()) return std::nullopt;
  if (flag || !amount.empty()) {
    result += L'_';
    result += std::wstring_view(L"0LURDST")[flag];
  }
  if (!amount.empty()) {
    result += L'_';
    result += amount;
  }
  return ValidText(result) ? std::optional{result} : std::nullopt;
}
inline bool ValidModifier(std::wstring_view text) {
  size_t count = 0;
  while (!text.empty()) {
    const size_t separator = text.find(L' ');
    const auto token = text.substr(0, separator);
    if (token.empty() || token.size() > 2 || ++count > 15) return false;
    unsigned value = 0;
    for (wchar_t c : token) {
      unsigned digit = c >= L'0' && c <= L'9'   ? c - L'0'
                       : c >= L'A' && c <= L'F' ? c - L'A' + 10
                       : c >= L'a' && c <= L'f' ? c - L'a' + 10
                                                : 256;
      if (digit > 15) return false;
      value = value * 16 + digit;
    }
    if (!value) return false;
    if (separator == text.npos) break;
    text.remove_prefix(separator + 1);
    while (!text.empty() && text.front() == L' ') text.remove_prefix(1);
  }
  return true;
}
inline bool ValidTextOption(size_t index, std::wstring_view text) {
  return index < kTextCount && ValidText(text) &&
         ((index != kModKey && index != kHotkeys) || ValidModifier(text));
}
struct TextValue {
  bool present = false;
  std::wstring raw;
  std::wstring effective;
};
using TextValues = std::array<TextValue, kTextCount>;
struct ActionChoice {
  const wchar_t* id;
  const wchar_t* label;
  bool idle_button_only = false;
};
inline constexpr std::array kButtonActions{
    ActionChoice{L"Move", L"Move window", true},
    ActionChoice{L"Resize", L"Resize window", true},
    ActionChoice{L"Restore", L"Restore window", true},
    ActionChoice{L"Close", L"Close window"},
    ActionChoice{L"Kill", L"End process"},
    ActionChoice{L"Minimize", L"Minimize"},
    ActionChoice{L"Maximize", L"Maximize / restore"},
    ActionChoice{L"Lower", L"Send to back"},
    ActionChoice{L"Focus", L"Focus window"},
    ActionChoice{L"NStacked", L"Next stacked window"},
    ActionChoice{L"PStacked", L"Previous stacked window"},
    ActionChoice{L"StackList", L"Stacked window list"},
    ActionChoice{L"StackList2", L"Stacked list (laser)"},
    ActionChoice{L"AltTabList", L"Window switcher"},
    ActionChoice{L"AltTabFullList", L"Window switcher (all)"},
    ActionChoice{L"Roll", L"Roll / unroll"},
    ActionChoice{L"AlwaysOnTop", L"Toggle always on top"},
    ActionChoice{L"Borderless", L"Toggle borders"},
    ActionChoice{L"Center", L"Center on desktop"},
    ActionChoice{L"Center2", L"Center on monitor"},
    ActionChoice{L"MaximizeHV", L"Maximize horizontally / vertically"},
    ActionChoice{L"SideSnap", L"Snap to side"},
    ActionChoice{L"ExtendSnap", L"Extend to edge"},
    ActionChoice{L"ExtendTNEdge", L"Extend to next edge"},
    ActionChoice{L"MoveTNEdge", L"Move to next edge"},
    ActionChoice{L"NLayout", L"Next zone layout"},
    ActionChoice{L"PLayout", L"Previous zone layout"},
    ActionChoice{L"MinAllOther", L"Minimize other windows"},
    ActionChoice{L"Mute", L"Mute sound"},
    ActionChoice{L"Menu", L"Action menu"},
    ActionChoice{L"Nothing", L"No action / default gesture"}};
inline constexpr std::array kWheelActions{
    ActionChoice{L"AltTab", L"Switch windows"},
    ActionChoice{L"Volume", L"Adjust volume"},
    ActionChoice{L"Transparency", L"Adjust transparency"},
    ActionChoice{L"Zoom", L"Zoom window"},
    ActionChoice{L"Zoom2", L"Zoom window (alternate)"},
    ActionChoice{L"Lower", L"Change stacking order"},
    ActionChoice{L"Roll", L"Roll / unroll"},
    ActionChoice{L"Maximize", L"Maximize / restore"},
    ActionChoice{L"NPStacked", L"Cycle stacked windows"},
    ActionChoice{L"NPStacked2", L"Cycle stacked windows (laser)"},
    ActionChoice{L"HScroll", L"Scroll horizontally"},
    ActionChoice{L"NPLayout", L"Cycle zone layouts"},
    ActionChoice{L"Nothing", L"No wheel action"}};
}  // namespace altsnap::settings
#endif
