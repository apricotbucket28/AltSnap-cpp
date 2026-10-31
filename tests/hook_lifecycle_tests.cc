#include <windows.h>

#include <cstdio>
#include <functional>
#include <vector>

#include "app/hook_lifecycle.h"
#include "hooks/hooks.h"

namespace {
using namespace altsnap::app;
int failures = 0;
#define CHECK(value)                                               \
  do {                                                             \
    if (!(value)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); \
      ++failures;                                                  \
    }                                                              \
  } while (false)
struct Backend final : HookLifecycleBackend {
  bool live = true;
  bool stopping = false;
  int rejects = 0;
  int attempts = 0;
  int starts = 0;
  int depth = 0;
  std::vector<int> saves;
  std::function<void()> during_stop;
  bool Enabled() const override { return live; }
  bool Stopping() const override { return stopping; }
  bool TryStop() override {
    CHECK(++depth == 1);
    stopping = true;
    ++attempts;
    if (during_stop) during_stop();
    stopping = false;
    --depth;
    if (attempts <= rejects) return false;
    live = false;
    return true;
  }
  void Start() override {
    CHECK(!live && !stopping);
    live = true;
    ++starts;
  }
  void SaveZones(int layout) override {
    CHECK(!live && !stopping);
    saves.push_back(layout);
  }
};
struct Context {
  Backend backend;
  HookLifecycle lifecycle{backend};
  std::vector<UINT> replay;
};
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* context =
      reinterpret_cast<Context*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (context) {
    if (context->lifecycle.DeferMessage(message, wp, lp)) return 0;
    if (context->lifecycle.HandleMessage(message, wp)) return 0;
    if (message == WM_UPDATESETTINGS) {
      CHECK(!context->backend.stopping);
      context->replay.push_back(message);
      context->lifecycle.Request(HookLifecycle::Change::kReload);
      return 0;
    }
    if (message == WM_COMMAND || message == WM_OPENCONFIG ||
        message == WM_SCLICK || message == WM_UNIKEYMENU) {
      CHECK(!context->backend.stopping && context->backend.starts > 0);
      context->replay.push_back(message);
      return 0;
    }
  }
  return DefWindowProcW(window, message, wp, lp);
}
HWND CreateOwner(Context& context) {
  HWND window = CreateWindowExW(0, L"AltSnapHookLifecycleTests", L"", 0, 0, 0,
                                0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), &context);
  CHECK(window);
  context.lifecycle.Attach(window);
  return window;
}
template <typename Predicate>
void PumpUntil(Predicate done) {
  const ULONGLONG deadline = GetTickCount64() + 3000;
  while (!done() && GetTickCount64() < deadline) {
    MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT,
                                MWMO_INPUTAVAILABLE);
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
      DispatchMessageW(&message);
  }
  CHECK(done());
}
void TestRetryAndReplay() {
  Context context;
  HWND owner = CreateOwner(context);
  context.backend.rejects = 3;
  HWND expired = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
  DestroyWindow(expired);
  context.backend.during_stop = [&] {
    if (context.backend.attempts == 1) {
      SendMessageW(owner, WM_COMMAND, 123, 0);
      SendMessageW(owner, WM_OPENCONFIG, 2, 1);
      SendMessageW(owner, WM_SCLICK, reinterpret_cast<WPARAM>(expired), 0);
    }
    CHECK(context.backend.starts == 0 && context.replay.empty());
  };
  context.lifecycle.Request(HookLifecycle::Change::kReload);
  CHECK(context.lifecycle.pending() && context.backend.live &&
        context.backend.starts == 0);
  PumpUntil([&] {
    return !context.lifecycle.pending() && context.replay.size() == 2;
  });
  CHECK(context.backend.attempts == 4 && context.backend.starts == 1);
  CHECK((context.replay == std::vector<UINT>{WM_COMMAND, WM_OPENCONFIG}));
  context.lifecycle.Detach();
  DestroyWindow(owner);
}
void TestQueuedToggleAndSave() {
  Context context;
  HWND owner = CreateOwner(context);
  context.backend.rejects = 3;
  context.lifecycle.Request(HookLifecycle::Change::kToggle);
  context.lifecycle.Request(HookLifecycle::Change::kSaveZones, 7);
  CHECK(context.backend.saves.empty() && context.backend.starts == 0);
  PumpUntil([&] { return !context.lifecycle.pending(); });
  CHECK(context.backend.live && context.backend.starts == 1);
  CHECK((context.backend.saves == std::vector<int>{7}));
  context.backend.rejects = context.backend.attempts + 2;
  context.lifecycle.Request(HookLifecycle::Change::kToggle);
  context.lifecycle.Request(HookLifecycle::Change::kToggle);
  PumpUntil([&] { return !context.lifecycle.pending(); });
  CHECK(context.backend.live && context.backend.starts == 2);
  context.lifecycle.Detach();
  DestroyWindow(owner);
}
void TestDeferredReload() {
  Context context;
  HWND owner = CreateOwner(context);
  context.backend.rejects = 1;
  context.backend.during_stop = [&] {
    if (context.backend.attempts == 1)
      SendMessageW(owner, WM_UPDATESETTINGS, 0, 0);
  };
  context.lifecycle.Request(HookLifecycle::Change::kReload);
  PumpUntil([&] {
    return !context.lifecycle.pending() && context.backend.starts == 2;
  });
  CHECK((context.replay == std::vector<UINT>{WM_UPDATESETTINGS}));
  context.lifecycle.Detach();
  DestroyWindow(owner);
}
void TestReentrantRequest() {
  Context context;
  HWND owner = CreateOwner(context);
  context.backend.during_stop = [&] {
    if (context.backend.attempts == 1)
      context.lifecycle.Request(HookLifecycle::Change::kReload);
  };
  context.lifecycle.Request(HookLifecycle::Change::kToggle);
  CHECK(!context.lifecycle.pending() && !context.backend.live);
  CHECK(context.backend.attempts == 1 && context.backend.starts == 0);
  context.lifecycle.Detach();
  DestroyWindow(owner);
}
}  // namespace
int main() {
  WNDCLASSW wc{};
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpfnWndProc = WindowProc;
  wc.lpszClassName = L"AltSnapHookLifecycleTests";
  CHECK(RegisterClassW(&wc));
  TestRetryAndReplay();
  TestQueuedToggleAndSave();
  TestReentrantRequest();
  TestDeferredReload();
  CHECK(UnregisterClassW(wc.lpszClassName, wc.hInstance));
  std::printf("%d lifecycle test failure(s)\n", failures);
  return failures != 0;
}
