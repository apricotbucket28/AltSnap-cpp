#include <array>
#include <cstdio>
#include <limits>
#include <string>
#include <thread>

#include "app/hook_shutdown.h"
#include "hooks/hook_config.h"
#include "hooks/hook_startup.h"
#include "hooks/hotkey_message.h"
#include "hooks/input_options.h"
#include "hooks/keyboard_hook.h"
#include "hooks/mouse_hook.h"
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
int lifecycle_sends = 0;
LRESULT CALLBACK LifecycleWindowProc(HWND window, UINT message, WPARAM wp,
                                     LPARAM lp) {
  if (message == WM_WINDOWPOSCHANGING && !HooksActive() &&
      g_mainhwnd == window) {
    CHECK(SendMessageW(window, kLifecycleSend, 0, 0) == 123);
  }
  if (message == kLifecycleSend) {
    ++lifecycle_sends;
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

int keyboard_stage = 0;
int overlay_stage = 0;
HHOOK WINAPI FailKeyboardHook(int, HOOKPROC, HINSTANCE, DWORD) {
  ++keyboard_stage;
  CHECK(PostWorkerCommand(
      ClickCommand{BT_NONE}));  // The real worker has started.
  SetLastError(ERROR_ACCESS_DENIED);
  return nullptr;
}
bool FailOverlay(const IniSection&) {
  ++overlay_stage;
  CHECK(!PostWorkerCommand(
      ClickCommand{BT_NONE}));  // Failure precedes worker startup.
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
  injected.keyboard_hook = FailKeyboardHook;
  CHECK(LoadSession(window, path.c_str(), injected) == nullptr);
  CHECK(keyboard_stage == 1 && overlay_stage == 0);
  CHECK(g_mainhwnd == nullptr);
  CHECK(!PostWorkerCommand(ClickCommand{BT_NONE}));
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
    CHECK(Load(window, path.c_str()) != nullptr);
    CHECK(mousehook);
    CHECK(Load(window, path.c_str()) == nullptr);
    bool foreign_load_rejected = false;
    std::thread foreign([&] {
      foreign_load_rejected = Load(window, path.c_str()) == nullptr;
      CHECK(!Unload());  // A foreign thread cannot destroy UI-owned resources.
    });
    foreign.join();
    CHECK(foreign_load_rejected && HooksActive());
    MoveWindowAsync(window, 9 + cycle, 19 + cycle, 221, 121);
    const int sends_before = lifecycle_sends;
    windowRR request{};
    request.hwnd = window;
    request.x = 10 + cycle;
    request.y = 20 + cycle;
    request.width = 220;
    request.height = 120;
    request.start = request.end = 1;
    CHECK(PostWorkerCommand(MoveWindowCommand{request}));
    CHECK(Unload());
    CHECK(lifecycle_sends > sends_before);
    CHECK(g_mainhwnd == nullptr && !mousehook);
    CHECK(!KeepMouseHook());
    CHECK(!PostWorkerCommand(ClickCommand{BT_NONE}));
    for (HWND overlay : g_transhwnd) CHECK(overlay == nullptr);
    CHECK(RegisteredHotkeys().empty());
    CHECK(monitors.empty() && wnds.empty() && snwnds.empty());
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
  CHECK(DestroyWindow(window));
  CHECK(UnregisterClassW(wc.lpszClassName, instance));
}

}  // namespace

int main() {
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
