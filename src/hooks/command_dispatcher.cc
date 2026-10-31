#include "hooks/command_dispatcher.h"

#include <cassert>
#include <cstdlib>
#include <utility>

namespace altsnap::hooks {
namespace {
constexpr wchar_t kClassName[] = L"AltSnap-EngineCommands";
constexpr UINT kExecute = WM_APP + 1;
}  // namespace

CommandDispatcher::~CommandDispatcher() {
  assert(!window_);
}

bool CommandDispatcher::Start(HINSTANCE instance) {
  if (window_) return false;
  WNDCLASSW wc{};
  wc.hInstance = instance;
  wc.lpfnWndProc = WindowProc;
  wc.lpszClassName = kClassName;
  if (!RegisterClassW(&wc)) return false;
  owner_ = GetCurrentThreadId();
  instance_ = instance;
  window_ = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                            nullptr, instance, this);
  if (!window_) {
    UnregisterClassW(kClassName, instance);
    owner_ = 0;
    instance_ = nullptr;
    return false;
  }
  return true;
}

void CommandDispatcher::Dispatch(WorkerCommand command) {
  if (!window_ || GetCurrentThreadId() == owner_) std::abort();
  {
    std::lock_guard lock(mutex_);
    // Exactly one consumer thread dispatches commands, synchronously.
    if (pending_) std::abort();
    pending_ = std::move(command);
  }
  // Messages are wakeups only, never pointers, packed actions or enum casts.
  SendMessageW(window_, kExecute, 0, 0);
}

void CommandDispatcher::Stop() {
  if (!window_) return;
  if (GetCurrentThreadId() != owner_) std::abort();
  DestroyWindow(window_);
  window_ = nullptr;
  UnregisterClassW(kClassName, instance_);
  instance_ = nullptr;
  owner_ = 0;
}

LRESULT CALLBACK CommandDispatcher::WindowProc(HWND window, UINT message,
                                               WPARAM wp, LPARAM lp) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* self = reinterpret_cast<CommandDispatcher*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (self && message == kExecute) {
    std::optional<WorkerCommand> command;
    {
      std::lock_guard lock(self->mutex_);
      command = std::exchange(self->pending_, std::nullopt);
    }
    if (command) self->execute_(std::move(*command));
    return 0;
  }
  return DefWindowProcW(window, message, wp, lp);
}

}  // namespace altsnap::hooks
