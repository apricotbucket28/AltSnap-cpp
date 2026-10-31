#include "hooks/worker.h"

#include <cassert>
#include <cstdlib>
#include <optional>
#include <utility>

#include "common/winutil.h"

namespace altsnap::hooks {

namespace {
class Lock final {
 public:
  explicit Lock(SRWLOCK& lock) : lock_(lock) {
    AcquireSRWLockExclusive(&lock_);
  }
  ~Lock() { ReleaseSRWLockExclusive(&lock_); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;

 private:
  SRWLOCK& lock_;
};
}  // namespace

Worker::~Worker() {
  // Joining is explicit: a DLL's static destructors run under loader lock.
  assert(thread_ == nullptr);
}

bool Worker::Start(const Startup& startup) {
  Lock lock(lock_);
  if (thread_) return false;
  wake_ = startup.create_event(nullptr, TRUE, FALSE, nullptr);
  if (!wake_) return false;
  // The queue and event are usable before thread creation; no USER message
  // queue readiness handshake or thread ID publication is necessary.
  thread_ = startup.create_thread(nullptr, 0, ThreadMain, this, 0, nullptr);
  if (!thread_) {
    CloseHandle(wake_);
    wake_ = nullptr;
    return false;
  }
  stopping_ = false;
  accepting_ = true;
  return true;
}

bool Worker::Post(WorkerCommand command) {
  if (!execute_) return false;
  Lock lock(lock_);
  if (!accepting_) return false;
  if (IsMouseMove(command)) {
    // Only adjacent moves coalesce; finish/action commands remain barriers.
    if (!queue_.empty() && CanCoalesce(queue_.back(), command)) {
      queue_.back() = std::move(command);
    } else {
      queue_.emplace_back(std::move(command));
    }
    SetEvent(wake_);
    return true;
  }
  if (IsDroppable(command) && pending_tasks_ >= kMaxPendingTasks) return false;
  queue_.emplace_back(std::move(command));
  ++pending_tasks_;
  SetEvent(wake_);
  return true;
}

bool Worker::PostMouseMove(POINT point) {
  return Post(MouseMoveCommand{point});
}

void Worker::Close() {
  Lock lock(lock_);
  accepting_ = false;
}

void Worker::Stop() {
  // Tasks touching foreign windows can block on a hung target, delaying Unload.
  // The join is deliberately unbounded: abandoning it would free shared state
  // while the worker is still using it.
  {
    Lock lock(lock_);
    accepting_ = false;
    if (!thread_) return;
    stopping_ = true;
    SetEvent(wake_);
  }
  // A task can synchronously send to a UI-owned window. Service sent messages
  // while joining, but do not dispatch queued timers/hotkeys or other input.
  for (;;) {
    const DWORD result = MsgWaitForMultipleObjectsEx(
        1, &thread_, INFINITE, QS_SENDMESSAGE, MWMO_INPUTAVAILABLE);
    if (result == WAIT_OBJECT_0) break;
    if (result == WAIT_OBJECT_0 + 1) {
      MSG message;
      PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    } else {
      // Owned handles stay valid until the join. Do not release live worker
      // resources if this invariant has been broken.
      LOG("Worker join failed: %lu", GetLastError());
      std::abort();
    }
  }
  Lock lock(lock_);
  CloseHandle(thread_);
  CloseHandle(wake_);
  thread_ = nullptr;
  wake_ = nullptr;
  stopping_ = false;
}

DWORD WINAPI Worker::ThreadMain(void* context) {
  static_cast<Worker*>(context)->Run();
  return 0;
}

void Worker::Run() {
  for (;;) {
    std::optional<WorkerCommand> item;
    {
      Lock lock(lock_);
      if (queue_.empty()) {
        if (stopping_) return;
        ResetEvent(wake_);
      } else {
        item = std::move(queue_.front());
        queue_.pop_front();
        if (!IsMouseMove(*item)) --pending_tasks_;
      }
    }
    if (!item) {
      if (WaitForSingleObject(wake_, INFINITE) != WAIT_OBJECT_0) {
        LOG("Worker wait failed: %lu", GetLastError());
        std::abort();
      }
    } else {
      execute_(std::move(*item));
    }
  }
}

}  // namespace altsnap::hooks
