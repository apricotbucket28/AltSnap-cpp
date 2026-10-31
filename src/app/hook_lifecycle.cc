#include "app/hook_lifecycle.h"

#include <cstdlib>
#include <type_traits>

#include "app/hook_shutdown.h"

namespace altsnap::app {

HookLifecycle::~HookLifecycle() {
  Detach();
}
void HookLifecycle::CheckThread() const {
  if (owner_thread_ && owner_thread_ != GetCurrentThreadId()) std::abort();
}
void HookLifecycle::Attach(HWND owner) {
  Detach();
  if (!owner) return;
  if (GetWindowThreadProcessId(owner, nullptr) != GetCurrentThreadId())
    std::abort();
  owner_ = owner;
  owner_thread_ = GetCurrentThreadId();
}
void HookLifecycle::Detach() {
  CheckThread();
  if (owner_) KillTimer(owner_, kRetryTimer);
  owner_ = nullptr;
  owner_thread_ = 0;
  requests_.clear();
  deferred_.clear();
}
void HookLifecycle::ArmRetry() {
  if (owner_ && !SetTimer(owner_, kRetryTimer, 25, nullptr))
    PostMessageW(owner_, kRetryMessage, 0, 0);
}
void HookLifecycle::SessionStopped() {
  CheckThread();
  // Menu HWNDs belong to the stopped DLL session. Drop their requests even if
  // Windows has already reused an identifier for a newly created window.
  std::erase_if(deferred_, [](const Deferred& value) {
    return std::holds_alternative<ActionMenu>(value) ||
           std::holds_alternative<UniKeyMenu>(value);
  });
  if (pending()) ArmRetry();
}
void HookLifecycle::Request(Change change, int layout) {
  CheckThread();
  requests_.push_back({change, layout});
  Service();
}
bool HookLifecycle::DeferMessage(UINT message, WPARAM wp, LPARAM lp) {
  CheckThread();
  if (!DeferDuringHookShutdown(backend_.Stopping(), message)) return false;
  // These protocols carry scalar values/borrowed HWND identifiers, never
  // pointers into the sender's stack. Expired menu requests are not replayed.
  switch (message) {
    case WM_TRAY:
      deferred_.emplace_back(Tray{wp, lp});
      break;
    case WM_OPENCONFIG:
      deferred_.emplace_back(OpenConfig{wp, lp});
      break;
    case WM_UPDATESETTINGS:
      deferred_.emplace_back(Reload{});
      break;
    case WM_COMMAND:
      deferred_.emplace_back(Command{wp, reinterpret_cast<HWND>(lp)});
      break;
    case WM_SCLICK:
      deferred_.emplace_back(ActionMenu{reinterpret_cast<HWND>(wp), lp});
      break;
    case WM_UNIKEYMENU:
      deferred_.emplace_back(UniKeyMenu{reinterpret_cast<HWND>(wp), lp});
      break;
    default:
      return false;
  }
  ArmRetry();
  return true;
}
bool HookLifecycle::HandleMessage(UINT message, WPARAM wp) {
  CheckThread();
  if (message != kRetryMessage && (message != WM_TIMER || wp != kRetryTimer))
    return false;
  Service();
  return true;
}
bool HookLifecycle::Replay(const Deferred& message) {
  return std::visit(
      [this](const auto& value) -> bool {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Tray>) {
          return PostMessageW(owner_, WM_TRAY, value.icon, value.event);
        } else if constexpr (std::is_same_v<T, OpenConfig>) {
          return PostMessageW(owner_, WM_OPENCONFIG, value.page, value.flags);
        } else if constexpr (std::is_same_v<T, Reload>) {
          return PostMessageW(owner_, WM_UPDATESETTINGS, 0, 0);
        } else if constexpr (std::is_same_v<T, Command>) {
          if (value.control && !IsWindow(value.control)) return true;
          return PostMessageW(owner_, WM_COMMAND, value.command,
                              reinterpret_cast<LPARAM>(value.control));
        } else {
          static_assert(std::is_same_v<T, ActionMenu> ||
                        std::is_same_v<T, UniKeyMenu>);
          if (!IsWindow(value.window) ||
              GetWindowThreadProcessId(value.window, nullptr) != owner_thread_)
            return true;
          constexpr UINT message_id =
              std::is_same_v<T, ActionMenu> ? WM_SCLICK : WM_UNIKEYMENU;
          if constexpr (std::is_same_v<T, ActionMenu>)
            return PostMessageW(owner_, message_id,
                                reinterpret_cast<WPARAM>(value.window),
                                value.flags);
          else
            return PostMessageW(owner_, message_id,
                                reinterpret_cast<WPARAM>(value.window),
                                value.key);
        }
      },
      message);
}
void HookLifecycle::Service() {
  CheckThread();
  if (!owner_ || !IsWindow(owner_)) {
    Detach();
    return;
  }
  if (servicing_ || backend_.Stopping()) {
    ArmRetry();
    return;
  }
  struct Scope {
    bool& active;
    explicit Scope(bool& flag) : active(flag) { active = true; }
    ~Scope() { active = false; }
  } scope(servicing_);
  while (!requests_.empty()) {
    const RequestValue request = requests_.front();
    const bool enabled = backend_.Enabled();
    const bool need_stop = enabled || request.change == Change::kSaveZones;
    if (need_stop && !backend_.TryStop()) {
      ArmRetry();
      return;
    }
    if (need_stop) SessionStopped();
    if (!IsWindow(owner_)) {
      Detach();
      return;
    }
    if (request.change == Change::kSaveZones)
      backend_.SaveZones(request.layout);
    if (!IsWindow(owner_)) {
      Detach();
      return;
    }
    if ((request.change == Change::kToggle && !enabled) ||
        (request.change == Change::kReload && enabled) ||
        request.change == Change::kSaveZones)
      backend_.Start();
    requests_.pop_front();
  }
  while (!deferred_.empty()) {
    if (!Replay(deferred_.front())) {
      ArmRetry();
      return;
    }
    deferred_.pop_front();
  }
  KillTimer(owner_, kRetryTimer);
}

}  // namespace altsnap::app
