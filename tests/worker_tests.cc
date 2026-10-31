#include <array>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#include "hooks/worker.h"

namespace {
using altsnap::hooks::Worker;
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
  Worker worker;
  CHECK(!worker.Post([] {}));
  CHECK(!worker.Start({FailEvent, CreateThread}));
  CHECK(!worker.Start({CreateEventW, FailThread}));
  CHECK(!worker.Post([] {}));
  worker.Stop();
  // A failed start and repeated stops leave the same object restartable.
  for (int cycle = 0; cycle < 40; ++cycle) {
    int ran = 0;
    CHECK(worker.Start());
    CHECK(!worker.Start());
    CHECK(worker.Post([&ran] { ++ran; }));  // No startup delay/handshake.
    worker.Stop();
    CHECK(ran == 1);
    CHECK(!worker.Post([] {}));
    worker.Stop();
  }
  int drained = 0;
  {
    Worker scoped;
    CHECK(scoped.Start());
    CHECK(scoped.Post([&drained] { ++drained; }));
    scoped.Stop();  // Destruction must never join, including under loader lock.
  }
  CHECK(drained == 1);
}

std::vector<LONG> moves;
void TestQueue() {
  Worker worker([](POINT point) { moves.push_back(point.x); });
  HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(entered && release);
  CHECK(worker.Start());
  CHECK(worker.Post([=] {
    SetEvent(entered);
    WaitForSingleObject(release, INFINITE);
  }));
  CHECK(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
  CHECK(worker.PostMouseMove({1, 0}));
  CHECK(worker.PostMouseMove({2, 0}));
  CHECK(worker.Post([] { moves.push_back(3); }));
  CHECK(worker.PostMouseMove({4, 0}));
  CHECK(worker.PostMouseMove({5, 0}));
  bool nested_rejected = false;
  CHECK(worker.Post(
      [&] { nested_rejected = !worker.Post([] { moves.push_back(999); }); }));
  worker.Close();
  CHECK(!worker.PostMouseMove({6, 0}));
  CHECK(!worker.Post([] {}));
  SetEvent(release);
  worker.Stop();
  CHECK(nested_rejected);
  CHECK((moves == std::vector<LONG>{2, 3, 5}));
  CloseHandle(release);
  CloseHandle(entered);
}

void TestOverflow() {
  Worker worker;
  HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(worker.Start());
  CHECK(worker.Post([=] {
    SetEvent(entered);
    WaitForSingleObject(release, INFINITE);
  }));
  CHECK(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
  int ran = 0;
  for (size_t i = 0; i < Worker::kMaxPendingTasks; ++i)
    CHECK(worker.Post([&] { ++ran; }));
  CHECK(!worker.Post([&] { ran += 10000; }));
  // Capacity limits droppable tasks; control work remains FIFO and lossless.
  CHECK(worker.Post(
      [&] {
        CHECK(ran == 64);
        ++ran;
      },
      Worker::Importance::kControl));
  worker.Close();
  CHECK(!worker.Post([] {}, Worker::Importance::kControl));
  SetEvent(release);
  worker.Stop();
  CHECK(ran == 65);
  CHECK(worker.Start());
  CHECK(worker.Post([] {}));  // The pending count was reset by draining.
  worker.Stop();
  CloseHandle(release);
  CloseHandle(entered);
}

void TestConcurrentClose() {
  for (int cycle = 0; cycle < 20; ++cycle) {
    Worker worker;
    std::atomic<int> accepted{0};
    std::atomic<int> ran{0};
    std::atomic<bool> go{false};
    CHECK(worker.Start());
    std::array<std::thread, 4> producers;
    for (std::thread& producer : producers) {
      producer = std::thread([&] {
        while (!go.load()) std::this_thread::yield();
        for (int i = 0; i < 250; ++i) {
          if (worker.Post([&ran] { ++ran; })) ++accepted;
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
  Worker worker;
  LRESULT result = 0;
  CHECK(worker.Start());
  CHECK(worker.Post([&] { result = SendMessageW(window, kSend, 0, 0); }));
  worker.Stop();
  CHECK(result == 123);
  CHECK(DestroyWindow(window));
  CHECK(UnregisterClassW(wc.lpszClassName, wc.hInstance));
}
}  // namespace

int main() {
  TestStartup();
  TestQueue();
  TestOverflow();
  TestConcurrentClose();
  TestSynchronousSend();
  std::printf("%d worker test failure(s)\n", failures);
  return failures != 0;
}
