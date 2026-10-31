#include <array>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

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
  worker.Close();
  CHECK(!worker.PostMouseMove({6, 0}));
  CHECK(!worker.Post(ClickCommand{BT_LMB}));
  SetEvent(release);
  worker.Stop();
  CHECK(nested_rejected);
  CHECK((moves == std::vector<LONG>{2, 3, 5}));
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
    } else if (const auto* move = std::get_if<MouseMoveCommand>(&command)) {
      CHECK(ran == 64 && move->point.x == 2);
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
  // Mouse moves bypass task capacity and still coalesce at saturation.
  CHECK(worker.PostMouseMove({1, 0}));
  CHECK(worker.PostMouseMove({2, 0}));
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

void TestDirectExecutionAndOwnership() {
  const DWORD owner = GetCurrentThreadId();
  DWORD executor_thread = 0;
  HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(entered && release);
  std::vector<size_t> executed;
  windowRR observed{};
  const HWND target = GetDesktopWindow();
  Worker worker([&](WorkerCommand command) {
    CHECK(GetCurrentThreadId() != owner);
    if (!executor_thread) executor_thread = GetCurrentThreadId();
    CHECK(GetCurrentThreadId() == executor_thread);
    executed.push_back(command.index());
    if (std::holds_alternative<AltCtrlCommand>(command)) {
      SetEvent(entered);
      CHECK(WaitForSingleObject(release, 5000) == WAIT_OBJECT_0);
    } else if (const auto* move = std::get_if<MoveWindowCommand>(&command)) {
      observed = move->request;
    } else if (const auto* click = std::get_if<ClickCommand>(&command)) {
      CHECK(click->value == BT_MB5);
    } else if (const auto* kill = std::get_if<KillWindowCommand>(&command)) {
      CHECK(kill->window == target);
    }
  });
  CHECK(worker.Start());
  CHECK(worker.Post(AltCtrlCommand{}));
  CHECK(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
  windowRR request{};
  request.hwnd = target;
  request.x = -17;
  request.y = 23;
  request.width = 640;
  request.height = 480;
  request.odpi = 144;
  request.resize = {RZ_LEFT, RZ_BOTTOM};
  request.start = request.end = 1;
  request.moveonly = request.maximize = request.snap = 1;
  CHECK(worker.Post(MouseMoveCommand{{1, 2}}));
  CHECK(worker.Post(MoveWindowCommand{request}));
  request = {};  // The queued request owns all fields, including geometry.
  CHECK(worker.Post(ClickCommand{BT_MB5}));
  CHECK(worker.Post(MoveResizeComboCommand{}));
  CHECK(worker.Post(KillWindowCommand{target}));
  CHECK(worker.Post(FinishMovementCommand{}));
  CHECK(worker.Post(MouseMoveCommand{{3, 4}}));
  worker.Close();
  SetEvent(release);
  worker.Stop();
  CHECK((executed == std::vector<size_t>{4, 0, 2, 3, 5, 6, 1, 0}));
  CHECK(observed.hwnd == target && observed.x == -17 && observed.y == 23);
  CHECK(observed.width == 640 && observed.height == 480 &&
        observed.odpi == 144);
  CHECK(observed.resize.x == RZ_LEFT && observed.resize.y == RZ_BOTTOM);
  CHECK(observed.start == 1 && observed.end == 1 && observed.moveonly == 1 &&
        observed.maximize == 1 && observed.snap == 1);
  CloseHandle(release);
  CloseHandle(entered);
}

constexpr UINT kSend = WM_APP + 100;
constexpr UINT kPosted = WM_APP + 101;
int posted_messages = 0;
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
  if (message == kSend) return 123;
  if (message == kPosted) ++posted_messages;
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
  CHECK(PostMessageW(window, kPosted, 0, 0));
  CHECK(PostThreadMessageW(GetCurrentThreadId(), WM_QUIT, 42, 0));
  CHECK(worker.Post(ClickCommand{BT_LMB}));
  worker.Stop();
  CHECK(result == 123);
  // Joining services synchronous sends without consuming queued UI work or
  // losing the application's quit request.
  CHECK(posted_messages == 0);
  MSG message{};
  CHECK(PeekMessageW(&message, window, kPosted, kPosted, PM_REMOVE));
  CHECK(message.message == kPosted);
  DispatchMessageW(&message);
  CHECK(posted_messages == 1);
  CHECK(PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
  CHECK(message.message == WM_QUIT && message.wParam == 42);
  CHECK(DestroyWindow(window));
  CHECK(UnregisterClassW(wc.lpszClassName, wc.hInstance));
}
}  // namespace

int main() {
  TestStartup();
  TestQueue();
  TestOverflow();
  TestConcurrentClose();
  TestDirectExecutionAndOwnership();
  TestSynchronousSend();
  std::printf("%d worker test failure(s)\n", failures);
  return failures != 0;
}
