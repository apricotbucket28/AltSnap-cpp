#include <array>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#include "hooks/command_dispatcher.h"
#include "hooks/worker.h"

namespace {
using namespace altsnap::hooks;
int failures = 0;
#define CHECK(value)                                               \
  do {                                                             \
    if (!(value)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); \
      ++failures;                                                  \
    }                                                              \
  } while (false)

HANDLE WINAPI FailEvent(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) {
  return nullptr;
}
HANDLE WINAPI FailThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE,
                         LPVOID, DWORD, LPDWORD) {
  return nullptr;
}

void TestStartup() {
  int ran = 0;
  Worker worker([&](WorkerCommand) { ++ran; });
  CHECK(!worker.Post(ClickCommand{BT_LMB}));
  CHECK(!worker.Start({FailEvent, CreateThread}));
  CHECK(!worker.Start({CreateEventW, FailThread}));
  CHECK(!worker.Post(ClickCommand{BT_LMB}));
  worker.Stop();
  // A failed start and repeated stops leave the same object restartable.
  for (int cycle = 0; cycle < 40; ++cycle) {
    ran = 0;
    CHECK(worker.Start());
    CHECK(!worker.Start());
    CHECK(worker.Post(ClickCommand{BT_LMB}));  // No startup delay/handshake.
    worker.Stop();
    CHECK(ran == 1);
    CHECK(!worker.Post(ClickCommand{BT_LMB}));
    worker.Stop();
  }
  int drained = 0;
  {
    Worker scoped([&](WorkerCommand) { ++drained; });
    CHECK(scoped.Start());
    CHECK(scoped.Post(ClickCommand{BT_LMB}));
    scoped.Stop();  // Destruction must never join, including under loader lock.
  }
  CHECK(drained == 1);
}

void TestQueue() {
  HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(entered && release);
  std::vector<LONG> moves;
  bool nested_rejected = false;
  Worker worker([&](WorkerCommand command) {
    if (std::holds_alternative<AltCtrlCommand>(command)) {
      SetEvent(entered);
      WaitForSingleObject(release, INFINITE);
    } else if (const auto* move = std::get_if<MouseMoveCommand>(&command)) {
      moves.push_back(move->point.x);
    } else if (std::holds_alternative<MoveResizeComboCommand>(command)) {
      moves.push_back(3);
    } else if (std::holds_alternative<FinishMovementCommand>(command)) {
      nested_rejected = !worker.Post(ClickCommand{BT_LMB});
    }
  });
  CHECK(worker.Start());
  CHECK(worker.Post(AltCtrlCommand{}));
  CHECK(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
  CHECK(worker.PostMouseMove({1, 0}));
  CHECK(worker.PostMouseMove({2, 0}));
  CHECK(worker.Post(MoveResizeComboCommand{}));
  CHECK(worker.PostMouseMove({4, 0}));
  CHECK(worker.PostMouseMove({5, 0}));
  CHECK(worker.Post(FinishMovementCommand{}));
  // Adjacent points from different gestures must never replace one another.
  CHECK(worker.Post(MouseMoveCommand{{7, 0}, 1}));
  CHECK(worker.Post(MouseMoveCommand{{8, 0}, 2}));
  worker.Close();
  CHECK(!worker.PostMouseMove({6, 0}));
  CHECK(!worker.Post(ClickCommand{BT_LMB}));
  SetEvent(release);
  worker.Stop();
  CHECK(nested_rejected);
  CHECK((moves == std::vector<LONG>{2, 3, 5, 7, 8}));
  CloseHandle(release);
  CloseHandle(entered);
}

void TestOverflow() {
  HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  int ran = 0;
  Worker worker([&](WorkerCommand command) {
    if (std::holds_alternative<AltCtrlCommand>(command)) {
      SetEvent(entered);
      WaitForSingleObject(release, INFINITE);
    } else {
      if (std::holds_alternative<ClickCommand>(command)) CHECK(ran == 64);
      ++ran;
    }
  });
  CHECK(worker.Start());
  CHECK(worker.Post(AltCtrlCommand{}));
  CHECK(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
  for (size_t i = 0; i < Worker::kMaxPendingTasks; ++i)
    CHECK(worker.Post(MoveWindowCommand{}));
  CHECK(!worker.Post(MoveWindowCommand{}));
  // Control work bypasses capacity but stays FIFO; close rejects everything.
  CHECK(worker.Post(ClickCommand{BT_LMB}));
  worker.Close();
  CHECK(!worker.Post(ClickCommand{BT_LMB}));
  SetEvent(release);
  worker.Stop();
  CHECK(ran == 65);
  CHECK(worker.Start());
  CHECK(worker.Post(MoveWindowCommand{}));
  worker.Stop();
  CHECK(ran == 66);
  CloseHandle(release);
  CloseHandle(entered);
}

void TestConcurrentClose() {
  for (int cycle = 0; cycle < 20; ++cycle) {
    std::atomic<int> accepted{0};
    std::atomic<int> ran{0};
    Worker worker([&](WorkerCommand) { ++ran; });
    std::atomic<bool> go{false};
    CHECK(worker.Start());
    std::array<std::thread, 4> producers;
    for (std::thread& producer : producers) {
      producer = std::thread([&] {
        while (!go.load()) std::this_thread::yield();
        for (int i = 0; i < 250; ++i) {
          if (worker.Post(MoveWindowCommand{})) ++accepted;
        }
      });
    }
    go = true;
    while (accepted == 0) std::this_thread::yield();
    // Race submissions with admission closure, draining and handle release.
    worker.Stop();
    for (std::thread& producer : producers) producer.join();
    CHECK(ran == accepted);
  }
}

constexpr UINT kSend = WM_APP + 100;
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
  if (message == kSend) return 123;
  return DefWindowProcW(window, message, wp, lp);
}

void TestSynchronousSend() {
  WNDCLASSW wc{};
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpfnWndProc = WindowProc;
  wc.lpszClassName = L"AltSnapWorkerTest";
  CHECK(RegisterClassW(&wc));
  HWND window = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
  CHECK(window);
  LRESULT result = 0;
  Worker worker(
      [&](WorkerCommand) { result = SendMessageW(window, kSend, 0, 0); });
  CHECK(worker.Start());
  CHECK(worker.Post(ClickCommand{BT_LMB}));
  worker.Stop();
  CHECK(result == 123);
  CHECK(DestroyWindow(window));
  CHECK(UnregisterClassW(wc.lpszClassName, wc.hInstance));
}
void TestOwnerDispatch() {
  const DWORD owner = GetCurrentThreadId();
  for (int cycle = 0; cycle < 8; ++cycle) {
    std::vector<LONG> executed;
    CommandDispatcher dispatcher([&](WorkerCommand command) {
      CHECK(GetCurrentThreadId() == owner);
      if (const auto* move = std::get_if<MouseMoveCommand>(&command)) {
        executed.push_back(move->point.x);
      } else if (const auto* position =
                     std::get_if<MoveWindowCommand>(&command)) {
        executed.push_back(position->request.x);
      } else {
        CHECK(std::holds_alternative<FinishMovementCommand>(command));
        executed.push_back(4);
      }
    });
    CHECK(dispatcher.Start(GetModuleHandleW(nullptr)));
    CHECK(!dispatcher.Start(GetModuleHandleW(nullptr)));
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Worker worker([&](WorkerCommand command) {
      CHECK(GetCurrentThreadId() != owner);
      if (std::holds_alternative<AltCtrlCommand>(command)) {
        SetEvent(entered);
        WaitForSingleObject(release, INFINITE);
      } else {
        dispatcher.Dispatch(std::move(command));
      }
    });
    CHECK(worker.Start());
    CHECK(worker.Post(AltCtrlCommand{}));
    CHECK(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
    CHECK(worker.PostMouseMove({1, 0}));
    CHECK(worker.PostMouseMove({2, 0}));
    windowRR request{};
    request.x = 3;
    CHECK(worker.Post(MoveWindowCommand{request}));
    request.x = 999;  // Queue and mailbox each retain an owning value.
    CHECK(worker.Post(FinishMovementCommand{}));
    worker.Close();
    SetEvent(release);
    worker.Stop();  // Services synchronous owner dispatch while joining.
    CHECK((executed == std::vector<LONG>{2, 3, 4}));
    dispatcher.Stop();
    dispatcher.Stop();
    CloseHandle(entered);
    CloseHandle(release);
  }
}

}  // namespace

int main() {
  TestStartup();
  TestQueue();
  TestOverflow();
  TestConcurrentClose();
  TestSynchronousSend();
  TestOwnerDispatch();
  std::printf("%d worker test failure(s)\n", failures);
  return failures != 0;
}
