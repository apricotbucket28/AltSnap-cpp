#ifndef ALTSNAP_HOOKS_WORKER_H_
#define ALTSNAP_HOOKS_WORKER_H_

#include <windows.h>

#include <cstddef>
#include <deque>
#include <functional>
#include <utility>

#include "hooks/worker_command.h"

namespace altsnap::hooks {

// Start/Close/Stop belong to the creating (UI) thread. Post is thread safe.
// Accepted work owns its arguments and is drained before Stop returns. Tasks
// must finish; forcibly terminating a task would invalidate shared hook state.
// Stop must be called explicitly before destruction.
class Worker final {
 public:
  using Executor = std::function<void(WorkerCommand)>;
  static constexpr size_t kMaxPendingTasks = 64;
  // Narrow startup seam for testing allocation failures with the real worker.
  struct Startup {
    decltype(&CreateEventW) create_event = CreateEventW;
    decltype(&CreateThread) create_thread = CreateThread;
  };

  explicit Worker(Executor execute) : execute_(std::move(execute)) {}
  ~Worker();
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  [[nodiscard]] bool Start(const Startup& startup);
  [[nodiscard]] bool Start() { return Start(Startup{}); }
  // Control tasks bypass capacity, but are still rejected after Close().
  [[nodiscard]] bool Post(WorkerCommand command);
  [[nodiscard]] bool PostMouseMove(POINT point);
  void Close();  // Reject new work, including submissions from running tasks.
  void Stop();   // Close, drain, join, then release synchronization resources.

 private:
  static DWORD WINAPI ThreadMain(void* context);
  void Run();

  SRWLOCK lock_ = SRWLOCK_INIT;
  std::deque<WorkerCommand> queue_;
  size_t pending_tasks_ = 0;
  HANDLE wake_ = nullptr;
  HANDLE thread_ = nullptr;
  bool accepting_ = false;
  bool stopping_ = false;
  Executor execute_;
};

// Owner-thread admission. Command values are the only worker-facing interface.
[[nodiscard]] bool PostWorkerCommand(WorkerCommand command);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_HOOKS_WORKER_H_
