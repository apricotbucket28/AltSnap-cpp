#include <array>
#include <atomic>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <thread>

#include "app/hook_lifecycle.h"
#include "app/hook_shutdown.h"
#include "hooks/actions/click_actions.h"
#include "hooks/hook_config.h"
#include "hooks/hook_startup.h"
#include "hooks/hotkey_message.h"
#include "hooks/input_options.h"
#include "hooks/keyboard_hook.h"
#include "hooks/mouse_hook.h"
#include "hooks/mouse_move.h"
#include "hooks/movement.h"
#include "hooks/overlay/pin_window.h"
#include "hooks/overlay/trans_window.h"
#include "hooks/overlay/window_menu.h"
#include "hooks/window/window_info.h"
#include "hooks/window/window_move.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {
LRESULT CALLBACK HooksWinProc(HWND hwnd, UINT msg, WPARAM wparam,
                              LPARAM lparam);
}

namespace {
using namespace altsnap::hooks;
using altsnap::IniSection;

int g_failures = 0;
#define CHECK(condition)                                               \
  do {                                                                 \
    if (!(condition)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      ++g_failures;                                                    \
    }                                                                  \
  } while (false)

void TestBindings(const std::wstring& path) {
  constexpr std::array names = {
      L"LMB",    L"RMB",      L"MMB",     L"MB4",       L"MB5",
      L"MB6",    L"MB7",      L"MB8",     L"MB9",       L"MB10",
      L"MB11",   L"MB12",     L"MB13",    L"MB14",      L"MB15",
      L"MB16",   L"MB17",     L"MB18",    L"MB19",      L"MB20",
      L"Scroll", L"ScrollUp", L"HScroll", L"HScrollUp", L"GrabWithAlt",
      L"MoveUp", L"ResizeUp"};
  constexpr std::array suffixes = {L"",  L"B",  L"T", L"TB",
                                   L"M", L"MB", L"R", L"RB"};
  constexpr std::array contexts = {
      ActionContext::kNormal, ActionContext::kTitlebar, ActionContext::kMoving,
      ActionContext::kResizing};
  // Distinct parameters catch transposed groups, contexts, and modifiers.
  for (size_t group = 0; group < names.size(); ++group) {
    for (size_t slot = 0; slot < suffixes.size(); ++slot) {
      const std::wstring key = std::wstring(names[group]) + suffixes[slot];
      const std::wstring value = L"Menu_" + std::to_wstring(group + 1) + L"_" +
                                 std::to_wstring(slot + 1);
      CHECK(WritePrivateProfileStringW(L"Bindings", key.c_str(), value.c_str(),
                                       path.c_str()));
    }
  }
  const auto actions =
      ReadButtonActions(IniSection::Read(path.c_str(), L"Bindings"));
  for (size_t group = 0; group < names.size(); ++group) {
    for (size_t slot = 0; slot < suffixes.size(); ++slot) {
      const auto context = contexts[slot / 2];
      const bool secondary = slot % 2;
      action_t action = k_action_none;
      if (group < 24)
        action =
            actions.GetMouse(static_cast<int>(group + 2), context, secondary);
      if (group == 24) action = actions.grab_with_alt.Get(context, secondary);
      if (group == 25) action = actions.move_up.Get(context, secondary);
      if (group == 26) action = actions.resize_up.Get(context, secondary);
      CHECK(action.ac == AC_MENU && action.fl == group + 1 &&
            action.wp == static_cast<short>(slot + 1));
    }
  }
  for (int button : {std::numeric_limits<int>::min(), -1, 0, 1, 26,
                     std::numeric_limits<int>::max()}) {
    CHECK(actions.GetMouse(button, ActionContext::kNormal, false).ac ==
          AC_NONE);
  }
  CHECK(WritePrivateProfileStringW(L"Wheels", L"ScrollMB", L"Move",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Wheels", L"ScrollUpMB", L"Nothing",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Wheels", L"HScrollRB", L"Resize",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Wheels", L"HScrollUpRB", L"Menu",
                                   path.c_str()));
  const auto wheels =
      ReadButtonActions(IniSection::Read(path.c_str(), L"Wheels"));
  CHECK(wheels.GetMouse(BT_WHEELU, ActionContext::kMoving, true).ac == AC_MOVE);
  CHECK(wheels.GetMouse(BT_HWHEELU, ActionContext::kResizing, true).ac ==
        AC_MENU);
  const auto empty = ReadButtonActions(IniSection{});
  CHECK(empty.GetMouse(BT_LMB, ActionContext::kNormal, false).ac == AC_NONE);
  CHECK(empty.resize_up.Get(ActionContext::kResizing, true).ac == AC_NONE);

  CHECK(WritePrivateProfileStringW(L"EmptyBinding", L"LMB", L"", path.c_str()));
  const auto empty_section = IniSection::Read(path.c_str(), L"EmptyBinding");
  const wchar_t* value = empty_section.GetString("LMB", nullptr);
  CHECK(value && *value == L'\0');
  const auto empty_binding = ReadButtonActions(empty_section);
  const auto action =
      empty_binding.GetMouse(BT_LMB, ActionContext::kNormal, false);
  CHECK(action.ac == AC_NONE && action.fl == 0 && action.wp == 0);
}

void TestByteReaders(const std::wstring& path) {
  const IniSection section;
  CHECK(ReadColor(section, "Color", nullptr, ColorChannels::kRgba) == 0);
  CHECK(ReadColor(section, "Color", L"", ColorChannels::kRgb) == 0);
  CHECK(ReadColor(section, "Color", L"12", ColorChannels::kRgba) == 0x12);
  CHECK(ReadColor(section, "Color", L"12 34", ColorChannels::kRgba) == 0x3412);
  CHECK(ReadColor(section, "Color", L"12 34 56", ColorChannels::kRgba) ==
        0x563412);
  CHECK(ReadColor(section, "Color", L"12 34 56 78 9A", ColorChannels::kRgba) ==
        0x78563412);
  CHECK(ReadColor(section, "Color", L"12 34 56 78", ColorChannels::kRgb) ==
        0x563412);
  // Retain legacy separators and low-byte conversion.
  CHECK(ReadColor(section, "Color", L"1FF, 02", ColorChannels::kRgb) == 0x02FF);
  std::array<UCHAR, 4> keys{9, 9, 9, 9};
  CHECK(ReadHotkeys(section, "Keys", L"A4 A5 A6 A7", keys) == 3);
  CHECK((keys == std::array<UCHAR, 4>{0xA4, 0xA5, 0xA6, 0}));
  CHECK(ReadHotkeys(section, "Keys", L"11", keys) == 1);
  CHECK((keys == std::array<UCHAR, 4>{0x11, 0, 0, 0}));
  CHECK(ReadHotkeys(section, "Keys", nullptr, keys) == 0);
  CHECK((keys == std::array<UCHAR, 4>{}));
  CHECK(ReadHotkeys(section, "Keys", L"A4", {}) == 0);
  std::array<UCHAR, 1> terminator{9};
  CHECK(ReadHotkeys(section, "Keys", L"A4", terminator) == 0 &&
        terminator[0] == 0);
  std::array<UCHAR, 5> guarded{7, 7, 7, 7, 7};
  CHECK(ReadHotkeys(section, "Keys", L"A4 A5 A6",
                    std::span(guarded).subspan(1, 3)) == 2);
  CHECK((guarded == std::array<UCHAR, 5>{7, 0xA4, 0xA5, 0, 7}));

  CHECK(WritePrivateProfileStringW(L"Bytes", L"Color", L"12 34", path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Bytes", L"Keys", L"", path.c_str()));
  const auto stored = IniSection::Read(path.c_str(), L"Bytes");
  CHECK(ReadColor(stored, "Color", L"FF FF FF FF", ColorChannels::kRgba) ==
        0x3412);
  CHECK(ReadColor(stored, "Missing", L"FF FF FF", ColorChannels::kRgb) ==
        0xFFFFFF);
  keys.fill(9);
  CHECK(ReadHotkeys(stored, "Keys", L"A4 A5", keys) == 0);
  CHECK((keys == std::array<UCHAR, 4>{}));
}

void TestMessages() {
  std::array<action_t, kMaxRegisteredHotkeys> registered{};
  registered.front() = {AC_MENU, 3, 42};
  registered.back() = k_action_move;
  const auto first =
      DecodeHotkeyMessage(kFirstRegisteredHotkey, 0, registered, true);
  CHECK(first && first->point_window && first->action.wp == 42);
  const auto last =
      DecodeHotkeyMessage(kFirstRegisteredHotkey + 63, 0, registered, false);
  CHECK(last && !last->point_window && last->action.ac == AC_MOVE);
  for (WPARAM id : {WPARAM{0}, WPARAM{0x1000}, WPARAM{0x2000}, WPARAM{0xC000},
                    kFirstRegisteredHotkey + 64, WPARAM{0xFFFFFFFF},
                    WPARAM{1} << 32, std::numeric_limits<WPARAM>::max()}) {
    CHECK(!DecodeHotkeyMessage(id, 0, registered, false));
    CHECK(HooksWinProc(nullptr, WM_HOTKEY, id, 0) == 0);
  }
  CHECK(!DecodeHotkeyMessage(kFirstRegisteredHotkey + 1, 0, registered, false));
  CHECK(!DecodeHotkeyMessage(kFirstRegisteredHotkey, 0, {}, false));
  registered.front().ac = AC_MAXVALUE;
  CHECK(!DecodeHotkeyMessage(kFirstRegisteredHotkey, 0, registered, false));
  const action_t menu{AC_MENU, 2, 123};
  for (bool point : {false, true}) {
    const WPARAM id = AC_MENU + (point ? 0x1000 : 0);
    const auto decoded = DecodeHotkeyMessage(id, PackAction(menu), {}, false);
    CHECK(decoded && decoded->point_window == point &&
          decoded->action.fl == 2 && decoded->action.wp == 123);
    CHECK(!DecodeHotkeyMessage(id + 1, PackAction(menu), {}, false));
    CHECK(HooksWinProc(nullptr, WM_HOTKEY, id + 1, PackAction(menu)) == 0);
  }
  CHECK(!DecodeHotkeyMessage(AC_MAXVALUE, PackAction({AC_MAXVALUE, 0, 0}), {},
                             false));

  Zones[0] = {{1, 2, 3, 4}};
  Zones[MAX_LAYOUTS - 1] = {{5, 6, 7, 8}, {9, 10, 11, 12}};
  for (WPARAM layout : {WPARAM{0}, WPARAM{MAX_LAYOUTS - 1}}) {
    RECT* output = nullptr;
    CHECK(HooksWinProc(nullptr, WM_GETZONES, layout,
                       reinterpret_cast<LPARAM>(&output)) ==
          static_cast<LRESULT>(Zones[layout].size()));
    CHECK(output == Zones[layout].data());
    CHECK(HooksWinProc(nullptr, WM_GETZONES, layout, 0) ==
          static_cast<LRESULT>(Zones[layout].size()));
  }
  for (WPARAM layout : {WPARAM{1}, WPARAM{MAX_LAYOUTS}, WPARAM{1} << 32,
                        (WPARAM{1} << 32) + MAX_LAYOUTS - 1,
                        std::numeric_limits<WPARAM>::max()}) {
    RECT sentinel{};
    RECT* output = &sentinel;
    CHECK(HooksWinProc(nullptr, WM_GETZONES, layout,
                       reinterpret_cast<LPARAM>(&output)) == 0);
    CHECK(output == nullptr);
    CHECK(HooksWinProc(nullptr, WM_GETZONES, layout, 0) == 0);
    if (layout >= MAX_LAYOUTS)
      CHECK(HooksWinProc(nullptr, WM_GETLAYOUTREZ, layout, 0) == 0);
  }
  FreeZones();
}

constexpr UINT kLifecycleSend = WM_APP + 101;
const wchar_t* lifecycle_path = nullptr;
bool lifecycle_probe = false;
int lifecycle_sends = 0;
bool cancel_on_position = false;
bool attempt_nested_start = false;
bool verify_cancelled_on_position = false;
UINT last_position_flags = 0;
altsnap::app::HookLifecycle* native_lifecycle = nullptr;
bool native_request_pending = false;
UINT native_trigger = WM_WINDOWPOSCHANGING;
altsnap::app::HookLifecycle::Change native_change;
LRESULT CALLBACK LifecycleWindowProc(HWND window, UINT message, WPARAM wp,
                                     LPARAM lp) {
  if (native_lifecycle) {
    if (native_lifecycle->DeferMessage(message, wp, lp)) return 0;
    if (native_lifecycle->HandleMessage(message, wp)) return 0;
    if (message == native_trigger && native_request_pending) {
      native_request_pending = false;
      native_lifecycle->Request(native_change, 7);
    }
  }
  if (message == WM_WINDOWPOSCHANGING) {
    last_position_flags = reinterpret_cast<WINDOWPOS*>(lp)->flags;
    if (verify_cancelled_on_position && !Session().move_running) {
      verify_cancelled_on_position = false;
      CHECK(Session().input.action.ac == AC_NONE);
      CHECK(Session().input.moving == 0 &&
            Session().last_window.hwnd == nullptr);
    }
    if (attempt_nested_start) {
      attempt_nested_start = false;
      const HWND target = Session().input.hwnd;
      CHECK(Session().move_running);
      CHECK(InitMovementAndActions({0, 0}, nullptr, k_action_move, BT_LMB) ==
            ActionStartResult::kBusy);
      CHECK(Session().input.hwnd == target);
    }
    if (cancel_on_position) {
      cancel_on_position = false;
      KBDLLHOOKSTRUCT escape{};
      escape.vkCode = VK_ESCAPE;
      LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN,
                           reinterpret_cast<LPARAM>(&escape));
      verify_cancelled_on_position = true;
    }
  }
  if (message == kLifecycleSend ||
      (message == WM_WINDOWPOSCHANGING && lifecycle_probe)) {
    ++lifecycle_sends;
    CHECK(GetCurrentThreadId() == GetWindowThreadProcessId(window, nullptr));
    // Stop pumps synchronous sends. Neither lifecycle entry point may reenter
    // teardown or replace resources while the old worker is still draining.
    CHECK(g_mainhwnd == window);
    CHECK(!Unload());
    CHECK(Load(window, lifecycle_path) == nullptr);
    CHECK(g_mainhwnd == window);
    return 123;
  }
  return DefWindowProcW(window, message, wp, lp);
}

HANDLE WINAPI FailWorkerEvent(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) {
  return nullptr;
}
HANDLE WINAPI FailWorkerThread(LPSECURITY_ATTRIBUTES, SIZE_T,
                               LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD) {
  return nullptr;
}
int keyboard_stage = 0;
int overlay_stage = 0;
HHOOK WINAPI FailKeyboardHook(int, HOOKPROC, HINSTANCE, DWORD) {
  ++keyboard_stage;
  CHECK(PostWorkerCommand(
      KillWindowCommand{nullptr, true}));  // The real worker has started.
  SetLastError(ERROR_ACCESS_DENIED);
  return nullptr;
}
bool FailOverlay(const IniSection&) {
  ++overlay_stage;
  CHECK(!PostWorkerCommand(
      KillWindowCommand{nullptr, true}));  // Failure precedes worker startup.
  return false;
}
void TestModalGuard() {
  for (UINT message :
       {UINT{WM_TRAY}, UINT{WM_SCLICK}, UINT{WM_UNIKEYMENU},
        UINT{WM_OPENCONFIG}, UINT{WM_UPDATESETTINGS}, UINT{WM_COMMAND}}) {
    CHECK(altsnap::app::DeferDuringHookShutdown(true, message));
    CHECK(!altsnap::app::DeferDuringHookShutdown(false, message));
  }
  CHECK(!altsnap::app::DeferDuringHookShutdown(true, WM_PAINT));
}

template <typename Predicate>
bool PumpUntil(Predicate done, DWORD timeout = 5000) {
  const ULONGLONG deadline = GetTickCount64() + timeout;
  while (!done() && GetTickCount64() < deadline) {
    MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT,
                                MWMO_INPUTAVAILABLE);
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  return done();
}

// Exercise the production move/finish path against an owned native window.
// No cursor movement or injected keyboard/mouse input is required.
void TestDragCommands(HWND window, const std::wstring& path) {
  CHECK(
      WritePrivateProfileStringW(L"Input", L"Hotclicks", L"02", path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Input", L"GrabWithAlt", L"Move",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Advanced", L"UseCursor", L"0",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"General", L"Aero", L"0", path.c_str()));
  CHECK(
      WritePrivateProfileStringW(L"General", L"AutoSnap", L"0", path.c_str()));
  CHECK(WritePrivateProfileStringW(L"General", L"StickyResize", L"0",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Zones", L"UseZones", L"0", path.c_str()));
  for (bool full_window : {false, true}) {
    CHECK(WritePrivateProfileStringW(L"Performance", L"FullWin",
                                     full_window ? L"1" : L"0", path.c_str()));
    for (bool resize : {false, true}) {
      CHECK(SetWindowPos(window, nullptr, 100, 100, 200, 100,
                         SWP_NOACTIVATE | SWP_NOZORDER));
      CHECK(Load(window, path.c_str()));
      HookSession& session = Session();
      CHECK(session.gesture == 0 && session.movement_rect.left == 0);
      const action_t action =
          resize ? action_t{AC_RESIZE, 0, 0} : k_action_move;
      const uint64_t old_gesture = session.gesture;
      CHECK(InitMovementAndActions({110, 110}, window, action, BT_LMB) ==
            ActionStartResult::kConsumed);
      CHECK(session.gesture == old_gesture + 1);
      CHECK(session.input.hwnd == window);
      // Make geometry deterministic after exercising the real drag start.
      session.input.moving = 1;
      session.input.resizable = 2;
      session.input.offset = resize ? POINT{0, 0} : POINT{10, 10};
      session.input.origin.dpi = GetDpiForWindow(window);
      session.input.mmi = {{1, 1}, {10000, 10000}};
      session.input.resize = {RZ_RIGHT, RZ_BOTTOM};
      CHECK(GetWindowRect(window, &session.movement_rect));
      // Recover an orphaned old-gesture finish, then start another real drag.
      session.finish_pending = old_gesture;
      CHECK(PostWorkerCommand(FinishMovementCommand{old_gesture}));
      CHECK(PumpUntil([&] { return !session.finish_pending; }));
      CHECK(session.input.action.ac == action.ac);
      session.input.clicktime = 0;  // The next start is not a double click.
      const uint64_t first_gesture = session.gesture;
      CHECK(InitMovementAndActions({110, 110}, window, action, BT_LMB) ==
            ActionStartResult::kConsumed);
      CHECK(session.gesture == first_gesture + 1);
      session.input.moving = 1;
      session.input.offset = resize ? POINT{0, 0} : POINT{10, 10};
      session.input.resize = {RZ_RIGHT, RZ_BOTTOM};
      CHECK(PostWorkerCommand(MouseMoveCommand{{900, 900}, first_gesture}));
      CHECK(PostWorkerCommand(MoveResizeComboCommand{first_gesture}));
      CHECK(PostWorkerCommand(FinishMovementCommand{first_gesture}));
      MouseMove(resize ? POINT{350, 250} : POINT{200, 200});
      FinishMovementAsync();
      FinishMovementAsync();  // Idempotent while the barrier is pending.
      CHECK(session.finish_pending);
      CHECK(InitMovementAndActions({0, 0}, window, k_action_move, BT_LMB) ==
            ActionStartResult::kBusy);
      // Alt arriving in the action-cleared portion of finish must not reset
      // moving through HookMouse or call UnhookMouse on start rejection.
      session.input.action = k_action_none;
      session.input.alt = 0;
      KBDLLHOOKSTRUCT alt{};
      alt.vkCode = VK_LMENU;
      LowLevelKeyboardProc(HC_ACTION, WM_SYSKEYDOWN,
                           reinterpret_cast<LPARAM>(&alt));
      CHECK(session.input.moving == 1 && session.input.hwnd == window);
      session.input.action = action;
      // Busy hotclicks pass through without disturbing the finishing drag.
      MSLLHOOKSTRUCT click{};
      click.pt = {110, 110};
      session.input.alt = 0;
      const UCHAR moving = session.input.moving;
      CHECK(LowLevelMouseProc(HC_ACTION, WM_LBUTTONDOWN,
                              reinterpret_cast<LPARAM>(&click)) == 0);
      CHECK(session.input.moving == moving && session.input.hwnd == window);
      // Keep the session active while dispatching. Window callbacks must not
      // reenter Unload and join a worker waiting for the current command.
      lifecycle_probe = true;
      CHECK(PumpUntil([&] { return !session.finish_pending; }));
      lifecycle_probe = false;
      CHECK(!session.finish_pending);
      CHECK(session.input.action.ac == AC_NONE && session.input.moving == 0);
      RECT rect{};
      CHECK(GetWindowRect(window, &rect));
      CHECK(rect.left == (resize ? 100 : 190));
      CHECK(rect.top == (resize ? 100 : 190));
      CHECK(rect.right - rect.left == (resize ? 250 : 200));
      CHECK(rect.bottom - rect.top == (resize ? 150 : 100));
      CHECK(Unload());
      CHECK(!PostWorkerCommand(FinishMovementCommand{}));
    }
  }
}

void TestNestedCancellation(HWND window, const std::wstring& path) {
  CHECK(WritePrivateProfileStringW(L"Performance", L"FullWin", L"1",
                                   path.c_str()));
  CHECK(SetWindowPos(window, nullptr, 100, 100, 200, 100,
                     SWP_NOACTIVATE | SWP_NOZORDER));
  CHECK(Load(window, path.c_str()));
  CHECK(InitMovementAndActions({110, 110}, window, k_action_move, BT_LMB) ==
        ActionStartResult::kConsumed);
  Session().input.moving = 1;
  Session().input.offset = {10, 10};
  CHECK(GetWindowRect(window, &Session().movement_rect));
  attempt_nested_start = true;
  cancel_on_position = true;
  MouseMove({200, 200});
  // The next command checks cancellation before finish can mask resurrection.
  MoveWindowAsync(window, 210, 210, 200, 100);
  FinishMovementAsync();
  CHECK(PumpUntil([] { return !Session().finish_pending; }));
  CHECK(!attempt_nested_start && !cancel_on_position &&
        !verify_cancelled_on_position);
  CHECK(Session().input.action.ac == AC_NONE && Session().input.moving == 0);
  CHECK(Session().last_window.hwnd == nullptr);
  CHECK(Unload());
}

void TestMoveFlags(HWND window, const std::wstring& path) {
  CHECK(Load(window, path.c_str()));
  const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
  // The active drag is deliberately unrelated to the request's target style.
  Session().input.resizable = 0;
  windowRR request{};
  request.hwnd = window;
  request.moveonly = 1;
  request.x = 120;
  request.y = 120;
  request.width = 200;
  request.height = 100;
  MoveWindowNow(&request);
  const UINT sync_flags = last_position_flags;
  CHECK((sync_flags & SWP_NOSIZE) != 0);
  CHECK((sync_flags & SWP_ASYNCWINDOWPOS) == 0);
  SetWindowLongPtrW(window, GWL_STYLE, style & ~WS_THICKFRAME);
  MoveWindowAsync(window, 130, 130, 200, 100);
  SetWindowLongPtrW(window, GWL_STYLE, style);  // Changes while queued.
  FinishMovementAsync();
  CHECK(PumpUntil([] { return !Session().finish_pending; }));
  CHECK((last_position_flags & (SWP_NOSIZE | SWP_ASYNCWINDOWPOS)) ==
        (sync_flags & (SWP_NOSIZE | SWP_ASYNCWINDOWPOS)));
  CHECK(Unload());
}

void TestBusyAdmission(HWND window, const std::wstring& path) {
  CHECK(WritePrivateProfileStringW(L"Input", L"LMB", L"Move", path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Input", L"LMBT", L"Move", path.c_str()));
  CHECK(
      WritePrivateProfileStringW(L"Input", L"Hotclicks", L"02", path.c_str()));
  CHECK(
      WritePrivateProfileStringW(L"Input", L"TTBActions", L"3", path.c_str()));
  CHECK(Load(window, path.c_str()));
  HookSession& session = Session();
  for (bool finishing : {false, true}) {
    session.input.hwnd = window;
    session.input.mdiclient = window;
    session.input.prevpt = {17, 29};
    session.input.action = k_action_move;
    session.input.moving = 1;
    session.input.hittest = 7;
    session.input.shift = session.input.ctrl = 1;
    session.input.blockmouseup = 2;
    session.input.alt = 0;
    session.move_running = !finishing;
    if (finishing) session.finish_pending = session.gesture;
    CHECK(InitMovementAndActions({0, 0}, nullptr, k_action_move, BT_LMB) ==
          ActionStartResult::kBusy);
    CHECK(TitleBarActions({0, 0}, k_action_move, BT_LMB) ==
          ActionStartResult::kBusy);
    MSLLHOOKSTRUCT input{};
    input.pt = {90, 95};
    for (UINT down : {UINT{WM_LBUTTONDOWN}, UINT{WM_RBUTTONDOWN},
                      UINT{WM_MBUTTONDOWN}, UINT{WM_MOUSEWHEEL}})
      CHECK(LowLevelMouseProc(HC_ACTION, down,
                              reinterpret_cast<LPARAM>(&input)) == 0);
    HooksWinProc(window, WM_HOTKEY, AC_MENU, PackAction({AC_MENU, 0, 0}));
    CHECK(session.input.hwnd == window && session.input.mdiclient == window);
    CHECK(session.input.prevpt.x == 17 && session.input.prevpt.y == 29);
    CHECK(session.input.hittest == 7 && session.input.alt == 0);
    CHECK(session.input.shift == 1 && session.input.ctrl == 1 &&
          session.input.blockmouseup == 2);
    // Release after the busy interval with a legacy swallow counter still set.
    // Each passed down's up must bypass that unrelated counter.
    session.finish_pending.reset();
    session.move_running = false;
    session.input.action = k_action_none;
    for (UINT up : {UINT{WM_LBUTTONUP}, UINT{WM_RBUTTONUP}, UINT{WM_MBUTTONUP}})
      CHECK(LowLevelMouseProc(HC_ACTION, up,
                              reinterpret_cast<LPARAM>(&input)) == 0);
    CHECK(session.input.blockmouseup == 2);
  }
  session.input.blockmouseup = 0;
  session.input.mdiclient = nullptr;
  HitTestTimeoutbl(nullptr, {0, 0});
  CHECK(session.input.hwnd == window);
  CHECK(Unload());
}

void TestBusyLongClickDrop(HWND window, const std::wstring& path) {
  CHECK(WritePrivateProfileStringW(L"Input", L"LongClickMove", L"1",
                                   path.c_str()));
  CHECK(Load(window, path.c_str()));
  for (bool finishing : {false, true}) {
    Session().input.hwnd = window;
    Session().input.clickpt = {17, 29};
    Session().input.blockmouseup = 2;
    Session().move_running = !finishing;
    if (finishing) Session().finish_pending = Session().gesture;
    MSLLHOOKSTRUCT down{};
    down.pt = {17, 29};
    CHECK(LowLevelMouseProc(HC_ACTION, WM_LBUTTONDOWN,
                            reinterpret_cast<LPARAM>(&down)) == 0);
    CHECK(!KillTimer(window, GRAB_TIMER));  // Busy down does not arm a grab.
    CHECK(SetTimer(window, GRAB_TIMER, 1000, TimerWindowProc));
    TimerWindowProc(window, WM_TIMER, GRAB_TIMER, 0);
    CHECK(!KillTimer(window, GRAB_TIMER));
    CHECK(Session().input.hwnd == window && Session().input.clickpt.x == 17);
    CHECK(Session().input.blockmouseup == 2 &&
          Session().input.ignoreclick == 0);
    Session().move_running = false;
    Session().finish_pending.reset();
    FinishMovementAsync();
    CHECK(PumpUntil([] { return !Session().finish_pending; }));
    CHECK(!KillTimer(window,
                     GRAB_TIMER));  // Finish does not replay the old hold.
    CHECK(LowLevelMouseProc(HC_ACTION, WM_LBUTTONUP,
                            reinterpret_cast<LPARAM>(&down)) == 0);
  }
  Session().input.blockmouseup = 0;
  CHECK(Unload());
}

int keyboard_installations = 0;
bool fail_keyboard_install = false;
HHOOK last_keyboard_handle = nullptr;
std::array<HOOKPROC, 4> keyboard_procedures{};
HHOOK WINAPI TrackKeyboardInstall(int type, HOOKPROC proc, HINSTANCE instance,
                                  DWORD thread) {
  ++keyboard_installations;
  if (keyboard_installations <= 4)
    keyboard_procedures[keyboard_installations - 1] = proc;
  if (fail_keyboard_install) return nullptr;
  return last_keyboard_handle = SetWindowsHookExW(type, proc, instance, thread);
}
void TestKeyboardRehook(HWND window, const std::wstring& path) {
  HookStartup startup;
  startup.keyboard_hook = TrackKeyboardInstall;
  keyboard_installations = 0;
  fail_keyboard_install = false;
  CHECK(LoadSession(window, path.c_str(), startup));
  CHECK(keyboard_installations == 1 && last_keyboard_handle);
  const HHOOK original = last_keyboard_handle;
  // The keyboard refresh timer exists even without a permanent mouse hook.
  CHECK(!KeepMouseHook());
  CHECK(KillTimer(window, REHOOK_TIMER));
  CHECK(SetTimer(window, REHOOK_TIMER, 5000, TimerWindowProc));
  TimerWindowProc(window, WM_TIMER, REHOOK_TIMER, 0);
  CHECK(keyboard_installations == 2);
  CHECK(!UnhookWindowsHookEx(original));
  const HHOOK retained = last_keyboard_handle;
  CHECK(keyboard_procedures[0] != keyboard_procedures[1]);
  fail_keyboard_install = true;
  TimerWindowProc(window, WM_TIMER, REHOOK_TIMER, 0);
  CHECK(keyboard_installations == 3 && last_keyboard_handle == retained);
  // A failed refresh retained the working hook. Remove it to simulate the OS
  // silently removing it, then prove the next tick installs a replacement.
  CHECK(UnhookWindowsHookEx(retained));
  fail_keyboard_install = false;
  TimerWindowProc(window, WM_TIMER, REHOOK_TIMER, 0);
  CHECK(keyboard_installations == 4 && last_keyboard_handle);
  CHECK(keyboard_procedures[2] == keyboard_procedures[3]);
  CHECK(Unload());
  TimerWindowProc(window, WM_TIMER, REHOOK_TIMER, 0);
  CHECK(keyboard_installations == 4);  // No recovery during/after teardown.
  CHECK(!UnhookWindowsHookEx(last_keyboard_handle));
}

class NativeLifecycleBackend final : public altsnap::app::HookLifecycleBackend {
 public:
  NativeLifecycleBackend(HWND owner, const std::wstring& path)
      : owner_(owner), path_(path) {}
  bool Enabled() const override { return enabled; }
  bool Stopping() const override { return stopping; }
  bool TryStop() override {
    stopping = true;
    const bool result = Unload();
    stopping = false;
    if (result)
      enabled = false;
    else {
      ++rejected;
      CHECK(HooksActive());
    }
    return result;
  }
  void Start() override {
    CHECK(!HooksActive());
    CHECK(Load(owner_, path_.c_str()));
    enabled = true;
    ++starts;
  }
  void SaveZones(int layout) override {
    CHECK(!HooksActive() && !g_mainhwnd);
    saved = layout;
  }
  bool enabled = false;
  bool stopping = false;
  int starts = 0;
  int rejected = 0;
  int saved = -1;

 private:
  HWND owner_;
  std::wstring path_;
};
void TestNativeLifecycleRetry(HWND window, const std::wstring& path) {
  using Lifecycle = altsnap::app::HookLifecycle;
  CHECK(WritePrivateProfileStringW(L"Performance", L"FullWin", L"1",
                                   path.c_str()));
  int position = 140;
  for (Lifecycle::Change change :
       {Lifecycle::Change::kToggle, Lifecycle::Change::kReload,
        Lifecycle::Change::kSaveZones}) {
    NativeLifecycleBackend backend(window, path);
    Lifecycle lifecycle(backend);
    lifecycle.Attach(window);
    native_lifecycle = &lifecycle;
    backend.Start();
    native_change = change;
    native_request_pending = true;
    MoveWindowAsync(window, position, position, 240, 140);
    position += 10;
    CHECK(PumpUntil(
        [&] { return !native_request_pending && !lifecycle.pending(); }));
    CHECK(backend.rejected >
          0);  // Unload really returned FALSE at command depth.
    CHECK(backend.starts == (change == Lifecycle::Change::kToggle ? 1 : 2));
    if (change == Lifecycle::Change::kSaveZones)
      CHECK(backend.saved == 7);
    else
      CHECK(backend.saved == -1);
    if (backend.enabled) lifecycle.Request(Lifecycle::Change::kToggle);
    CHECK(!HooksActive());
    lifecycle.Detach();
    native_lifecycle = nullptr;
  }
  // A settings request can also arrive while a low-level hook is inside a
  // synchronous window call, without any worker command on the stack.
  CHECK(WritePrivateProfileStringW(L"Input", L"GrabWithAlt", L"Nothing",
                                   path.c_str()));
  CHECK(
      WritePrivateProfileStringW(L"Input", L"Hotkeys", L"A4 A5", path.c_str()));
  NativeLifecycleBackend backend(window, path);
  Lifecycle lifecycle(backend);
  lifecycle.Attach(window);
  native_lifecycle = &lifecycle;
  backend.Start();
  g_mchwnd = CreateWindowExW(0, L"AltSnapLifecycleTest", L"", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, hinstDLL, nullptr);
  CHECK(g_mchwnd);
  native_trigger = WM_CLOSE;
  native_change = Lifecycle::Change::kReload;
  native_request_pending = true;
  KBDLLHOOKSTRUCT alt{};
  alt.vkCode = VK_LMENU;
  LowLevelKeyboardProc(HC_ACTION, WM_SYSKEYDOWN,
                       reinterpret_cast<LPARAM>(&alt));
  CHECK(!native_request_pending && backend.rejected == 1);
  CHECK(backend.starts == 1 && HooksActive());
  CHECK(PumpUntil([&] { return !lifecycle.pending(); }));
  CHECK(backend.starts == 2 && HooksActive());
  lifecycle.Request(Lifecycle::Change::kToggle);
  CHECK(!HooksActive());
  lifecycle.Detach();
  native_lifecycle = nullptr;
  native_trigger = WM_WINDOWPOSCHANGING;
}

void TestIdleEntryPoints(HWND window, WNDPROC callback) {
  CHECK(TrySession() == nullptr);
  for (UINT_PTR timer : {UINT_PTR{REHOOK_TIMER}, UINT_PTR{SPEED_TIMER},
                         UINT_PTR{GRAB_TIMER}, UINT_PTR{HIDELAYOUT_TIMER}})
    TimerWindowProc(window, WM_TIMER, timer, 0);
  for (UINT message :
       {UINT{WM_HOTKEY}, UINT{WM_STACKLIST}, UINT{WM_SETLAYOUTNUM},
        UINT{WM_GETBESTLAYOUT}, UINT{WM_TIMER}})
    CHECK(callback(window, message, 0, 0) == 0);
  MenuWindowProc(window, WM_KILLFOCUS, 0, 0);
  MenuWindowProc(window, WM_DESTROY, 0, 0);
  KBDLLHOOKSTRUCT key{};
  MSLLHOOKSTRUCT mouse{};
  LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&key));
  LowLevelMouseProc(HC_ACTION, WM_MOUSEMOVE, reinterpret_cast<LPARAM>(&mouse));
}

void TestDeferredAlt(HWND window, const std::wstring& path) {
  CHECK(WritePrivateProfileStringW(L"General", L"InactiveScroll", L"0",
                                   path.c_str()));
  CHECK(
      WritePrivateProfileStringW(L"Input", L"TTBActions", L"0", path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Input", L"LongClickMove", L"0",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Input", L"Hotclicks", L"", path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Input", L"GrabWithAlt", L"Nothing",
                                   path.c_str()));
  for (bool release : {false, true}) {
    CHECK(Load(window, path.c_str()));
    CHECK(!mousehook && !KeepMouseHook());
    CHECK(InitMovementAndActions({110, 110}, window, k_action_move, BT_LMB) ==
          ActionStartResult::kConsumed);
    FinishMovementAsync();
    const HWND target = Session().input.hwnd;
    CHECK(InitMovementAndActions({0, 0}, nullptr, k_action_move, BT_PROBE) ==
          ActionStartResult::kBusy);
    Session().input.blockaltup = 1;
    Session().input.sclickhwnd = window;
    KBDLLHOOKSTRUCT alt{};
    alt.vkCode = VK_LMENU;
    LowLevelKeyboardProc(HC_ACTION, WM_SYSKEYDOWN,
                         reinterpret_cast<LPARAM>(&alt));
    CHECK(Session().deferred_alt == VK_LMENU);
    CHECK(!Session().input.blockaltup && !Session().input.sclickhwnd);
    CHECK(Session().input.hwnd == target && !mousehook);
    if (release)
      LowLevelKeyboardProc(HC_ACTION, WM_SYSKEYUP,
                           reinterpret_cast<LPARAM>(&alt));
    CHECK(PumpUntil([] { return !Session().finish_pending; }));
    CHECK(!Session().deferred_alt);
    CHECK(Session().input.alt == (release ? 0 : VK_LMENU));
    CHECK(static_cast<bool>(mousehook.load()) == !release);
    CHECK(Unload());
  }
}

WNDPROC original_overlay_proc = nullptr;
bool cancel_combo_on_hide = false;
LRESULT CALLBACK CancelComboWindowProc(HWND window, UINT message, WPARAM wp,
                                       LPARAM lp) {
  if (message == WM_SHOWWINDOW && !wp && cancel_combo_on_hide) {
    cancel_combo_on_hide = false;
    KBDLLHOOKSTRUCT escape{};
    escape.vkCode = VK_ESCAPE;
    LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN,
                         reinterpret_cast<LPARAM>(&escape));
    Session().input.hwnd = nullptr;
  }
  return CallWindowProcW(original_overlay_proc, window, message, wp, lp);
}
void TestComboCancellation(HWND window, const std::wstring& path) {
  CHECK(WritePrivateProfileStringW(L"Performance", L"FullWin", L"0",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Performance", L"TransWinOpacity", L"160",
                                   path.c_str()));
  CHECK(Load(window, path.c_str()));
  CHECK(InitMovementAndActions({110, 110}, window, k_action_move, BT_LMB) ==
        ActionStartResult::kConsumed);
  ShowTransWin(SW_SHOWNA);
  const HWND overlay = g_transhwnd[0];
  original_overlay_proc = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(overlay, GWLP_WNDPROC,
                        reinterpret_cast<LONG_PTR>(CancelComboWindowProc)));
  cancel_combo_on_hide = true;
  DoMoveResizeMaxMinComboAction();
  CHECK(!cancel_combo_on_hide && !IsZoomed(window));
  CHECK(!Session().input.hwnd && Session().input.action.ac == AC_NONE);
  SetWindowLongPtrW(overlay, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(original_overlay_proc));
  CHECK(Unload());
}

void TestResizeAllEncoding(HWND window, const std::wstring& path) {
  CHECK(WritePrivateProfileStringW(L"Advanced", L"ResizeAll", L"2",
                                   path.c_str()));
  CHECK(Load(window, path.c_str()));
  const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
  SetWindowLongPtrW(window, GWL_STYLE, style & ~WS_THICKFRAME);
  CHECK((IsResizable(window) & 2) != 0);
  CHECK((GetWindowLongPtrW(window, GWL_STYLE) & WS_THICKFRAME) == 0);
  RECT rect{};
  CHECK(GetWindowRect(window, &rect));
  windowRR request{};
  request.hwnd = window;
  request.x = rect.left + 1;
  request.y = rect.top;
  request.width = rect.right - rect.left;
  request.height = rect.bottom - rect.top;
  request.moveonly = 1;
  MoveWindowNow(&request);
  CHECK((last_position_flags & SWP_ASYNCWINDOWPOS) == 0);
  CHECK(!request.hwnd);
  SetWindowLongPtrW(window, GWL_STYLE, style);
  CHECK(Unload());
  CHECK(WritePrivateProfileStringW(L"Advanced", L"ResizeAll", L"0",
                                   path.c_str()));
}

bool OnInteractiveDesktop() {
  USEROBJECTFLAGS flags{};
  DWORD length = 0;
  const bool visible =
      GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS, &flags,
                                sizeof(flags), &length) &&
      (flags.dwFlags & WSF_VISIBLE);
  std::array<wchar_t, 256> thread_name{};
  std::array<wchar_t, 256> input_name{};
  const bool have_thread = GetUserObjectInformationW(
      GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, thread_name.data(),
      sizeof(thread_name), &length);
  HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  const bool have_input =
      input && GetUserObjectInformationW(input, UOI_NAME, input_name.data(),
                                         sizeof(input_name), &length);
  const bool interactive =
      visible && have_thread && have_input &&
      std::wstring_view(thread_name.data()) == input_name.data();
  std::printf(
      "interactive desktop=%s; visible station=%s; thread=%ls; input=%ls\n",
      interactive ? "yes" : "no", visible ? "yes" : "no", thread_name.data(),
      input_name.data());
  if (input) CloseDesktop(input);
  return interactive;
}

constexpr ULONG_PTR kProbeTag = 0x41534E50;
std::atomic<unsigned> keyboard_probes{0};
HANDLE probe_seen = nullptr;
LRESULT CALLBACK ObservedKeyboardProc(int code, WPARAM wp, LPARAM lp) {
  const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
  if (code == HC_ACTION && key->dwExtraInfo == kProbeTag) {
    ++keyboard_probes;
    // Exercise the real engine callback, but keep F24 out of other apps.
    LowLevelKeyboardProc(code, wp, lp);
    SetEvent(probe_seen);
    return 1;
  }
  return LowLevelKeyboardProc(code, wp, lp);
}
HHOOK WINAPI InstallObservedKeyboard(int type, HOOKPROC, HINSTANCE instance,
                                     DWORD thread) {
  return SetWindowsHookExW(type, ObservedKeyboardProc, instance, thread);
}
UINT SendKeyboardProbe() {
  INPUT inputs[2]{};
  for (INPUT& input : inputs) {
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_F24;
    input.ki.dwExtraInfo = kProbeTag;
  }
  inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
  return SendInput(2, inputs, sizeof(INPUT));
}

HANDLE foreign_entered = nullptr;
HANDLE foreign_release = nullptr;
std::atomic<bool> foreign_armed{false};
LRESULT CALLBACK BlockedWindowProc(HWND window, UINT msg, WPARAM wp,
                                   LPARAM lp) {
  if (msg == WM_WINDOWPOSCHANGING && foreign_armed.exchange(false)) {
    SetEvent(foreign_entered);
    WaitForSingleObject(foreign_release, 5000);
  }
  if (msg == WM_DESTROY) PostQuitMessage(0);
  return DefWindowProcW(window, msg, wp, lp);
}

int TestBlockedForeignCommand(HWND owner, const std::wstring& path,
                              bool observe_hooks = false) {
  if (observe_hooks && !OnInteractiveDesktop()) {
    std::puts(
        "SKIP LL-hook survival: process is not on the interactive input "
        "desktop");
    return 77;
  }
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.hInstance = instance;
  wc.lpfnWndProc = BlockedWindowProc;
  wc.lpszClassName = L"AltSnapBlockedTargetTest";
  CHECK(RegisterClassW(&wc));
  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  foreign_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  foreign_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  probe_seen = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  std::atomic<HWND> target{nullptr};
  std::thread foreign([&] {
    target =
        CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 100, 100,
                        200, 100, nullptr, nullptr, instance, nullptr);
    SetEvent(ready);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  });
  CHECK(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0);
  CHECK(target.load());
  HookStartup startup;
  if (observe_hooks) startup.keyboard_hook = InstallObservedKeyboard;
  CHECK(LoadSession(owner, path.c_str(), startup));
  keyboard_probes = 0;
  bool input_available = false;
  if (observe_hooks) {
    UINT sent_before = 0;
    std::thread before([&] { sent_before = SendKeyboardProbe(); });
    input_available =
        PumpUntil([] { return keyboard_probes.load() >= 2; }, 2000);
    before.join();
    if (!input_available || sent_before != 2) {
      input_available = false;
      std::puts(
          "SKIP LL-hook survival: baseline SendInput produced no tagged hook "
          "callbacks");
    }
  }
  ResetEvent(probe_seen);
  const unsigned probes_before = keyboard_probes.load();
  bool entered = false;
  bool serviced_while_blocked = false;
  UINT sent_during = 0;
  std::thread controller([&] {
    entered = WaitForSingleObject(foreign_entered, 5000) == WAIT_OBJECT_0;
    if (entered && input_available) {
      std::thread injection([&] { sent_during = SendKeyboardProbe(); });
      serviced_while_blocked =
          WaitForSingleObject(probe_seen, 1500) == WAIT_OBJECT_0;
      // The foreign WndProc stays blocked for longer than a normal hook
      // timeout, independently of whether the owner services the injected
      // probe.
      Sleep(1600);
      SetEvent(foreign_release);
      injection.join();
    } else {
      if (entered) Sleep(1600);
      SetEvent(foreign_release);
    }
  });
  foreign_armed = true;
  MoveWindowAsync(target.load(), 120, 120, 250, 150);  // Synchronous resize.
  FinishMovementAsync();
  CHECK(PumpUntil([] { return !Session().finish_pending; }, 10000));
  controller.join();
  CHECK(entered);
  if (input_available) {
    CHECK(sent_during == 2 && serviced_while_blocked);
    CHECK(keyboard_probes.load() >= probes_before + 2);
    const unsigned probes_after_block = keyboard_probes.load();
    UINT sent_after = 0;
    std::thread after([&] { sent_after = SendKeyboardProbe(); });
    const bool survived = PumpUntil(
        [&] { return keyboard_probes.load() >= probes_after_block + 2; });
    after.join();
    CHECK(sent_after == 2 && survived);
    std::printf(
        "blocked foreign command: LL hook serviced during wait=%s, "
        "survived=%s\n",
        serviced_while_blocked ? "yes" : "no", survived ? "yes" : "no");
  }
  RECT moved{};
  CHECK(GetWindowRect(target.load(), &moved));
  CHECK(moved.left == 120 && moved.top == 120 &&
        moved.right - moved.left == 250 && moved.bottom - moved.top == 150);
  CHECK(Unload());
  PostMessageW(target.load(), WM_CLOSE, 0, 0);
  foreign.join();
  CHECK(UnregisterClassW(wc.lpszClassName, instance));
  CloseHandle(ready);
  CloseHandle(foreign_entered);
  CloseHandle(foreign_release);
  CloseHandle(probe_seen);
  return observe_hooks && !input_available ? 77 : 0;
}

int RunHookSurvival(const std::wstring& path) {
  hinstDLL = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.hInstance = hinstDLL;
  wc.lpfnWndProc = DefWindowProcW;
  wc.lpszClassName = L"AltSnapSurvivalOwner";
  if (!RegisterClassW(&wc)) return 77;
  const HWND owner = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                     HWND_MESSAGE, nullptr, hinstDLL, nullptr);
  if (!owner) {
    UnregisterClassW(wc.lpszClassName, hinstDLL);
    return 77;
  }
  CHECK(WritePrivateProfileStringW(L"Performance", L"FullWin", L"1",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Zones", L"UseZones", L"0", path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Advanced", L"UseCursor", L"0",
                                   path.c_str()));
  const int result = TestBlockedForeignCommand(owner, path, true);
  DestroyWindow(owner);
  UnregisterClassW(wc.lpszClassName, hinstDLL);
  return result;
}

void TestLifecycle(const std::wstring& path) {
  lifecycle_path = path.c_str();
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.hInstance = instance;
  wc.lpfnWndProc = LifecycleWindowProc;
  wc.lpszClassName = L"AltSnapLifecycleTest";
  if (!RegisterClassW(&wc)) {
    std::puts("SKIP native lifecycle: window class unavailable");
    return;
  }
  HWND window =
      CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 200,
                      100, nullptr, nullptr, instance, nullptr);
  if (!window) {
    std::puts("SKIP native lifecycle: window creation unavailable");
    UnregisterClass(wc.lpszClassName, instance);
    return;
  }
  hinstDLL = instance;
  CHECK(Load(nullptr, path.c_str()) == nullptr);
  CHECK(Load(window, nullptr) == nullptr);
  Unload();

  // No overlays: force keyboard hook installation to fail AFTER the worker
  // starts. A subsequent real Load must succeed without stale state/resources.
  CHECK(WritePrivateProfileStringW(L"Performance", L"FullWin", L"1",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Zones", L"UseZones", L"0", path.c_str()));
  HookStartup injected;
  // Dispatcher creation precedes the worker. Both startup failures must
  // destroy its window/class so the next session can register it again.
  injected.worker.create_event = FailWorkerEvent;
  CHECK(LoadSession(window, path.c_str(), injected) == nullptr);
  CHECK(g_mainhwnd == nullptr && !HooksActive());
  injected.worker.create_event = CreateEventW;
  injected.worker.create_thread = FailWorkerThread;
  CHECK(LoadSession(window, path.c_str(), injected) == nullptr);
  CHECK(g_mainhwnd == nullptr && !HooksActive());
  injected.worker.create_thread = CreateThread;
  injected.keyboard_hook = FailKeyboardHook;
  CHECK(LoadSession(window, path.c_str(), injected) == nullptr);
  CHECK(keyboard_stage == 1 && overlay_stage == 0);
  CHECK(g_mainhwnd == nullptr);
  CHECK(!PostWorkerCommand(KillWindowCommand{nullptr, true}));
  CHECK(!mousehook);
  Unload();
  // Also roll back when configuration cannot create its overlay windows.
  CHECK(WritePrivateProfileStringW(L"Performance", L"FullWin", L"0",
                                   path.c_str()));
  injected.create_trans_window = FailOverlay;
  CHECK(LoadSession(window, path.c_str(), injected) == nullptr);
  CHECK(overlay_stage == 1 && keyboard_stage == 1);
  CHECK(g_mainhwnd == nullptr && !HooksActive());
  for (HWND overlay : g_transhwnd) CHECK(overlay == nullptr);
  // Test genuine hook availability separately from injected failures.
  HHOOK keyboard =
      SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, instance, 0);
  HHOOK mouse = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, instance, 0);
  if (keyboard) UnhookWindowsHookEx(keyboard);
  if (mouse) UnhookWindowsHookEx(mouse);
  if (!keyboard || !mouse) {
    std::puts("SKIP native lifecycle: low-level hooks unavailable");
    DestroyWindow(window);
    UnregisterClass(wc.lpszClassName, instance);
    return;
  }

  for (int cycle = 0; cycle < 12; ++cycle) {
    // Alternate hollow frames / layered frame / full-window movement and
    // preview enabled / disabled to catch handles surviving config changes.
    CHECK(WritePrivateProfileStringW(L"Performance", L"FullWin",
                                     cycle % 3 == 2 ? L"1" : L"0",
                                     path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Performance", L"TransWinOpacity",
                                     cycle % 3 == 1 ? L"160" : L"0",
                                     path.c_str()));
    CHECK(WritePrivateProfileStringW(L"General", L"InactiveScroll", L"1",
                                     path.c_str()));
    CHECK(
        WritePrivateProfileStringW(L"Zones", L"UseZones", L"3", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Zones", L"ShowZonesPrevw",
                                     cycle % 2 ? L"1" : L"0", path.c_str()));
    const DWORD gdi_before =
        GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD user_before =
        GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    const WNDPROC callback = Load(window, path.c_str());
    CHECK(callback != nullptr);
    CHECK(mousehook);
    CHECK(Load(window, path.c_str()) == nullptr);
    bool foreign_load_rejected = false;
    std::thread foreign([&] {
      foreign_load_rejected = Load(window, path.c_str()) == nullptr;
      CHECK(!Unload());  // A foreign thread cannot destroy UI-owned resources.
      CHECK(!PostWorkerCommand(FinishMovementCommand{}));
    });
    foreign.join();
    CHECK(foreign_load_rejected && HooksActive());
    MoveWindowAsync(window, 10 + cycle, 20 + cycle, 220, 120);
    lifecycle_probe = true;
    const int sends_before = lifecycle_sends;
    CHECK(Unload());
    lifecycle_probe = false;
    CHECK(lifecycle_sends > sends_before);
    CHECK(g_mainhwnd == nullptr && !mousehook);
    TestIdleEntryPoints(window, callback);
    CHECK(!KeepMouseHook());
    TimerWindowProc(window, WM_TIMER, SPEED_TIMER, 0);  // Stale timer is inert.
    std::thread idle_caller([] {
      CHECK(Unload());  // Cleanup released the old thread's ownership.
      CHECK(!PostWorkerCommand(FinishMovementCommand{}));
    });
    idle_caller.join();
    CHECK(!PostWorkerCommand(KillWindowCommand{nullptr, true}));
    for (HWND overlay : g_transhwnd) CHECK(overlay == nullptr);
    CHECK(RegisteredHotkeys().empty());
    // Session() is deliberately unavailable once ownership has been released.
    // Move-only requests deliberately use SWP_ASYNCWINDOWPOS. Flush the OS
    // window messages after the worker has joined, as the normal UI loop does.
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    RECT rect{};
    CHECK(GetWindowRect(window, &rect));
    CHECK(rect.left == 10 + cycle && rect.top == 20 + cycle);
    // USER/GDI initialize process caches on first use of each overlay mode.
    if (cycle >= 3)
      CHECK(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <=
            gdi_before + 4);
    CHECK(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <=
          user_before + 4);
    Unload();
  }
  TestDragCommands(window, path);
  TestBusyAdmission(window, path);
  TestBusyLongClickDrop(window, path);
  TestDeferredAlt(window, path);
  TestKeyboardRehook(window, path);
  TestNativeLifecycleRetry(window, path);
  TestComboCancellation(window, path);
  TestNestedCancellation(window, path);
  TestMoveFlags(window, path);
  TestResizeAllEncoding(window, path);
  TestBlockedForeignCommand(window, path);
  CHECK(DestroyWindow(window));
  CHECK(UnregisterClassW(wc.lpszClassName, instance));
}

}  // namespace

int main(int argc, char** argv) {
  std::array<wchar_t, MAX_PATH + 1> temp_directory{};
  const DWORD length = GetTempPathW(static_cast<DWORD>(temp_directory.size()),
                                    temp_directory.data());
  if (length == 0 || length >= temp_directory.size()) {
    std::fputs("Cannot obtain a temporary directory\n", stderr);
    return 1;
  }
  std::array<wchar_t, MAX_PATH> temp_file{};
  if (!GetTempFileNameW(temp_directory.data(), L"ash", 0, temp_file.data())) {
    std::fputs("Cannot create a temporary INI file\n", stderr);
    return 1;
  }
  const std::wstring path = temp_file.data();
  if (argc == 2 && std::string_view(argv[1]) == "--hook-survival") {
    const int result = RunHookSurvival(path);
    CHECK(DeleteFileW(path.c_str()));
    return g_failures ? 1 : result;
  }
  TestBindings(path);
  TestByteReaders(path);
  TestMessages();
  TestModalGuard();
  TestLifecycle(path);
  CHECK(DeleteFileW(path.c_str()));
  std::printf(g_failures ? "%d FAILURE(S)\n" : "all hook tests passed\n",
              g_failures);
  return g_failures != 0;
}
