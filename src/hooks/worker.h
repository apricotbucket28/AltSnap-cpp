#ifndef ALTSNAP_HOOKS_WORKER_H_
#define ALTSNAP_HOOKS_WORKER_H_

#include <windows.h>

#include <cstddef>
#include <deque>
#include <functional>
#include <variant>

namespace altsnap::hooks {

// Start/Close/Stop belong to the creating (UI) thread. Post is thread safe.
// Accepted work owns its arguments and is drained before Stop returns. Tasks
// must finish; forcibly terminating a task would invalidate shared hook state.
// Stop must be called explicitly before destruction.
class Worker final {
 public:
  using Task = std::function<void()>;
  enum class Importance { kDroppable, kControl };
  static constexpr size_t kMaxPendingTasks = 64;
  // Narrow startup seam for testing allocation failures with the real worker.
  struct Startup {
    decltype(&CreateEventW) create_event = CreateEventW;
    decltype(&CreateThread) create_thread = CreateThread;
  };

  explicit Worker(void (*mouse_move)(POINT) = nullptr)
      : mouse_move_(mouse_move) {}
  ~Worker();
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  [[nodiscard]] bool Start(const Startup& startup);
  [[nodiscard]] bool Start() { return Start(Startup{}); }
  // Control tasks bypass capacity, but are still rejected after Close().
  [[nodiscard]] bool Post(Task task,
                          Importance importance = Importance::kDroppable);
  [[nodiscard]] bool PostMouseMove(POINT point);
  void Close();  // Reject new work, including submissions from running tasks.
  void Stop();   // Close, drain, join, then release synchronization resources.

 private:
  using Item = std::variant<Task, POINT>;
  static DWORD WINAPI ThreadMain(void* context);
  void Run();

  SRWLOCK lock_ = SRWLOCK_INIT;
  std::deque<Item> queue_;
  size_t pending_tasks_ = 0;
  HANDLE wake_ = nullptr;
  HANDLE thread_ = nullptr;
  bool accepting_ = false;
  bool stopping_ = false;
  void (*mouse_move_)(POINT);
};

// The session in hooks.cc owns the worker; modules submit owning closures.
[[nodiscard]] bool PostWorkerTask(
    Worker::Task task,
    Worker::Importance importance = Worker::Importance::kControl);
[[nodiscard]] bool PostWorkerMouseMove(POINT point);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_HOOKS_WORKER_H_
