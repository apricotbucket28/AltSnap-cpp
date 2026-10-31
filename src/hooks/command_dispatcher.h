#ifndef ALTSNAP_HOOKS_COMMAND_DISPATCHER_H_
#define ALTSNAP_HOOKS_COMMAND_DISPATCHER_H_

#include <windows.h>

#include <functional>
#include <mutex>
#include <optional>
#include <utility>

#include "hooks/worker_command.h"

namespace altsnap::hooks {

// A presentation-independent Win32 adapter for the engine owner. Start/Stop and
// execution belong to one thread. Dispatch belongs to the single worker and
// waits for completion, preserving the worker FIFO across owner-thread work.
// Stop only after joining that worker. No lock is held during USER calls.
class CommandDispatcher final {
 public:
  using Executor = std::function<void(WorkerCommand)>;
  explicit CommandDispatcher(Executor execute) : execute_(std::move(execute)) {}
  ~CommandDispatcher();
  CommandDispatcher(const CommandDispatcher&) = delete;
  CommandDispatcher& operator=(const CommandDispatcher&) = delete;

  [[nodiscard]] bool Start(HINSTANCE instance);
  void Dispatch(WorkerCommand command);
  void Stop();

 private:
  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp,
                                     LPARAM lp);
  HWND window_ = nullptr;
  DWORD owner_ = 0;
  HINSTANCE instance_ = nullptr;
  Executor execute_;
  std::mutex mutex_;
  std::optional<WorkerCommand> pending_;
};

}  // namespace altsnap::hooks

#endif  // ALTSNAP_HOOKS_COMMAND_DISPATCHER_H_
