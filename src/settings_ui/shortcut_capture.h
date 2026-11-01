#ifndef ALTSNAP_SETTINGS_UI_SHORTCUT_CAPTURE_H_
#define ALTSNAP_SETTINGS_UI_SHORTCUT_CAPTURE_H_
#include <windows.h>

#include <functional>
#include <utility>

#include "settings/keyboard_model.h"

namespace altsnap::settings_ui {
inline std::wstring ShortcutDisplayName(uint16_t binding) {
  std::wstring result;
  for (const auto [bit, label] :
       {std::pair{2, L"Ctrl"}, {1, L"Alt"}, {4, L"Shift"}, {8, L"Windows"}}) {
    if (!((binding >> 8) & bit)) continue;
    if (!result.empty()) result += L" + ";
    result += label;
  }
  const unsigned key = binding & 255;
  if (!key) return result;
  wchar_t name[128]{};
  LONG scan = static_cast<LONG>(MapVirtualKeyW(key, MAPVK_VK_TO_VSC) << 16);
  if ((key >= 0x21 && key <= 0x2e) || key == VK_DIVIDE) scan |= 1 << 24;
  GetKeyNameTextW(scan, name, 128);
  if (!result.empty()) result += L" + ";
  result += *name ? name : L"VK " + std::to_wstring(key);
  return result;
}
// Lives on the window STA. Installed last, this hook consumes capture input
// before the resident AltSnap hook, RegisterHotKey, and XAML accelerators.
class CaptureSession {
 public:
  CaptureSession(HWND owner, std::function<void()> changed)
      : owner_(owner), changed_(std::move(changed)) {
    if (active_) return;
    // Starting with held keys would hide releases from their original owner.
    for (int key = 8; key < 256; ++key)
      if (GetAsyncKeyState(key) & 0x8000) {
        if (key != VK_RETURN && key != VK_SPACE) return;
        initial_[key] = true;
      }
    active_ = this;
    hook_ =
        SetWindowsHookExW(WH_KEYBOARD_LL, Hook, GetModuleHandleW(nullptr), 0);
    if (!hook_) active_ = nullptr;
  }
  ~CaptureSession() {
    if (hook_) UnhookWindowsHookEx(hook_);
    if (active_ == this) active_ = nullptr;
  }
  CaptureSession(const CaptureSession&) = delete;
  CaptureSession& operator=(const CaptureSession&) = delete;
  bool Started() const { return hook_ != nullptr; }
  void Cancel() {
    state.Cancel();
    changed_();
  }
  settings::ShortcutCapture state;

 private:
  static LRESULT CALLBACK Hook(int code, WPARAM message, LPARAM data) {
    CaptureSession* self = active_;
    if (code < 0 || !self) return CallNextHookEx(nullptr, code, message, data);
    const auto& event = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
    const unsigned key = event.vkCode;
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    if (key < 256 && self->initial_[key]) {
      if (!down) self->initial_[key] = false;
      return down ? 1 : CallNextHookEx(nullptr, code, message, data);
    }
    const bool foreground = GetForegroundWindow() == self->owner_;
    if (!foreground) self->state.Cancel();
    // After focus loss, swallow only releases/repeats of keys captured here.
    const bool consume = foreground || (key < 256 && self->pressed_[key]);
    if (consume) {
      if (key < 256) self->pressed_[key] = down;
      self->state.Key(key, down);
    }
    self->changed_();  // Adapter only enqueues UI work; never runs XAML here.
    return consume ? 1 : CallNextHookEx(nullptr, code, message, data);
  }
  inline static thread_local CaptureSession* active_ = nullptr;
  HWND owner_;
  HHOOK hook_ = nullptr;
  std::array<bool, 256> pressed_{}, initial_{};
  std::function<void()> changed_;
};
}  // namespace altsnap::settings_ui
#endif
