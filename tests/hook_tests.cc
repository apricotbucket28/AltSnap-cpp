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
#include "hooks/window/snap.h"
#include "hooks/window/snap_move.h"
#include "hooks/window/window_enum.h"
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

void TestConfigReload(const std::wstring& path) {
  // Avoid display-dependent defaults and overlay creation in these reads.
  CHECK(WritePrivateProfileStringW(L"Performance", L"FullWin", L"1",
                                   path.c_str()));
  CHECK(ReadConfig(path.c_str()));
  CHECK(conf.Aero == 1 && conf.SmartAero == 1 && conf.AutoSnap == 0);
  CHECK(conf.CenterFraction == 24 && conf.SidesFraction == 24);
  CHECK(conf.AHoff == 50 && conf.AVoff == 50 && conf.SnapThreshold == 20);
  CHECK(conf.ResizeAll == 1 && conf.MoveRate == 2 && conf.ResizeRate == 4);
  CHECK(conf.Hotkeys[0] == VK_LMENU && conf.Hotkeys[1] == VK_RMENU &&
        conf.Hotkeys[2] == 0 && conf.EndSendKey == VK_CONTROL);
  CHECK(!KeepMouseHook());

  struct Setting {
    const wchar_t* section;
    const wchar_t* key;
    const wchar_t* value;
  };
  constexpr Setting settings[] = {
      {L"General", L"CenterFraction", L"150"},
      {L"General", L"AeroHoffset", L"125"},
      {L"General", L"AeroVoffset", L"37"},
      {L"General", L"AutoSnap", L"2"},
      {L"Advanced", L"ZoomFrac", L"0"},
      {L"Advanced", L"ZoomFracShift", L"1"},
      {L"Advanced", L"SnapGap", L"-3"},
      {L"Performance", L"MoveRate", L"0"},
      {L"Performance", L"ResizeRate", L"0"},
      {L"Performance", L"RezTimer", L"3"},
      {L"Performance", L"RefreshRate", L"17"},
      {L"Input", L"Hotkeys", L""},
      {L"Input", L"Hotclicks", L"06"},
      {L"Input", L"LMB", L"Resize"},
      {L"Input", L"Shrt0", L"41 01 41 02"},
      {L"Blacklist", L"Windows", L"*|RegressionWindow"},
  };
  for (const Setting& setting : settings)
    CHECK(WritePrivateProfileStringW(setting.section, setting.key,
                                     setting.value, path.c_str()));
  CHECK(ReadConfig(path.c_str()));
  CHECK(conf.CenterFraction == 100 && conf.SidesFraction == 100);
  CHECK(conf.AHoff == 100 && conf.AVoff == 37 && state.snap == 2);
  CHECK(conf.ZoomFrac == 2 && conf.ZoomFracShift == 2 && conf.SnapGap == -3);
  CHECK(conf.MoveRate == 1 && conf.ResizeRate == 1 && conf.RefreshRate == 0);
  CHECK(conf.Hotkeys[0] == 0 && KeepMouseHook());
  CHECK(conf.actions.GetMouse(BT_LMB, ActionContext::kNormal, false).ac ==
        AC_RESIZE);
  CHECK((g_inputSequences[0] == std::vector<UCHAR>{2, 0x41, 1, 0x41, 2}));
  CHECK(BlkLst.Windows.Contains(L"x.exe", L"x", L"RegressionWindow"));

  for (const wchar_t* section :
       {L"General", L"Advanced", L"Input", L"Blacklist", L"Performance"})
    CHECK(WritePrivateProfileStringW(section, nullptr, nullptr, path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Performance", L"FullWin", L"1",
                                   path.c_str()));
  CHECK(ReadConfig(path.c_str()));
  CHECK(conf.CenterFraction == 24 && conf.SidesFraction == 24);
  CHECK(conf.ZoomFrac == 16 && conf.SnapGap == 0 && state.snap == 0);
  CHECK(conf.Hotkeys[0] == VK_LMENU && conf.Hotclick[0] == 0);
  CHECK(!KeepMouseHook() && BlkLst.Windows.empty());
  for (const auto& sequence : g_inputSequences) CHECK(sequence.empty());
}

void TestSnapGeometry() {
  // Supply the already-enumerated desktop geometry. No real desktop windows,
  // monitor arrangement, DWM borders, or input timing influence the decisions.
  state = {};
  state.enumed = 1;
  state.snap = 1;
  monitors = {{-1000, -800, 0, 0}};
  wnds.clear();
  const auto move = [](int x, int y, int width, int height, int expected_x,
                       int expected_y) {
    MoveSnap(&x, &y, width, height, 20);
    CHECK(x == expected_x && y == expected_y);
  };
  move(-990, -790, 200, 100, -1000, -800);
  move(-210, -110, 200, 100, -200, -100);
  move(-980, -780, 200, 100, -1000, -800);  // Threshold is inclusive.
  move(-979, -779, 200, 100, -979, -779);  // Outside threshold.
  state.snap = 0;
  move(-990, -790, 200, 100, -990, -790);
  state.snap = 1;
  state.Speed = static_cast<unsigned>(conf.AeroMaxSpeed) + 1;
  move(-990, -790, 200, 100, -990, -790);
  state.Speed = 0;
  monitors.clear();
  wnds = {{{100, 100, 300, 300}, nullptr, 0}};
  move(305, 150, 100, 100, 300, 150);  // Outside edge.
  move(105, 150, 100, 100, 100, 150);  // Inside edge.
  state.snap = 2;
  move(105, 150, 100, 100, 105, 150);  // Outside-only mode.
  move(305, 150, 100, 100, 300, 150);
  wnds[0].flag = BD_HIDDEN_R;
  move(305, 150, 100, 100, 305, 150);
  wnds[0].flag = 0;
  wnds.push_back({{100, 100, 295, 300}, nullptr, 0});
  move(292, 150, 100, 100, 295, 150);  // Closer positive-distance edge wins.

  wnds.clear();
  monitors = {{0, 0, 1000, 800}};
  state.snap = 1;
  state.resize = {RZ_LEFT, RZ_TOP};
  int x = 10, y = 10, width = 200, height = 100;
  ResizeSnap(&x, &y, &width, &height, 20, 20);
  CHECK(x == 0 && y == 0 && width == 210 && height == 110);
  state.resize = {RZ_RIGHT, RZ_BOTTOM};
  x = 700;
  y = 600;
  width = 290;
  height = 190;
  ResizeSnap(&x, &y, &width, &height, 20, 20);
  CHECK(x == 700 && y == 600 && width == 300 && height == 200);
  monitors.clear();
  state = {};
}

std::vector<UINT> movement_messages;
LRESULT CALLBACK MovementWindowProc(HWND window, UINT message, WPARAM wp,
                                    LPARAM lp) {
  if (message == WM_ENTERSIZEMOVE || message == WM_EXITSIZEMOVE ||
      message == WM_SIZING)
    movement_messages.push_back(message);
  if (message == WM_SIZING) {
    CHECK(wp == WMSZ_BOTTOMRIGHT);
    // Model a terminal accepting only whole character cells.
    RECT& rect = *reinterpret_cast<RECT*>(lp);
    rect.right = rect.left + 240;
    rect.bottom = rect.top + 160;
    return TRUE;
  }
  return DefWindowProcW(window, message, wp, lp);
}

void TestMovementResults() {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.hInstance = instance;
  wc.lpfnWndProc = MovementWindowProc;
  wc.lpszClassName = L"AltSnapMovementRegression";
  CHECK(RegisterClassW(&wc));
  // Hidden popup avoids frame minimum sizes and desktop activation.
  HWND window = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 40, 50, 200,
                                100, nullptr, nullptr, instance, nullptr);
  CHECK(window);
  if (window) {
    windowRR request{};
    request.hwnd = window;
    request.x = 70;
    request.y = 80;
    request.width = 253;
    request.height = 177;
    request.odpi = GetDpiForWindow(window);
    request.resize = {RZ_RIGHT, RZ_BOTTOM};
    request.start = request.end = 1;
    movement_messages.clear();
    MoveWindowNow(&request);
    RECT rect{};
    CHECK(GetWindowRect(window, &rect));
    CHECK(rect.left == 70 && rect.top == 80 && rect.right == 310 &&
          rect.bottom == 240);
    CHECK((movement_messages == std::vector<UINT>{WM_SIZING}));
    // Start/end notifications are posted; sizing is synchronous.
    MSG message{};
    while (PeekMessageW(&message, window, 0, 0, PM_REMOVE))
      DispatchMessageW(&message);
    CHECK((movement_messages ==
           std::vector<UINT>{WM_SIZING, WM_ENTERSIZEMOVE, WM_EXITSIZEMOVE}));
    CHECK(!request.hwnd && !request.start && !request.end);

    // Move-only requests must preserve size and must not send WM_SIZING.
    request.hwnd = window;
    request.x = 90;
    request.y = 100;
    request.width = 999;
    request.height = 999;
    request.moveonly = 1;
    movement_messages.clear();
    MoveWindowNow(&request);
    CHECK(GetWindowRect(window, &rect));
    CHECK(rect.left == 90 && rect.top == 100 && rect.right == 330 &&
          rect.bottom == 260);
    CHECK(movement_messages.empty());

    // Restore dimensions use full 32-bit components on this x64 build.
    SetRestoreData(window, 70001, 80002, SNAPPED | SNLEFT);
    int width = 0, height = 0;
    CHECK(GetRestoreData(window, &width, &height) == (SNAPPED | SNLEFT));
    CHECK(width == 70001 && height == 80002);
    SetRestoreFlag(window, SNAPPED | SNZONE);
    CHECK(GetRestoreData(window, &width, &height) == (SNAPPED | SNZONE));
    CHECK(width == 70001 && height == 80002);
    ClearRestoreData(window);
    CHECK(GetRestoreData(window, &width, &height) == 0);
    CHECK(width == 0 && height == 0 && GetRestoreFlag(window) == 0);
    CHECK(DestroyWindow(window));
  }
  CHECK(UnregisterClassW(wc.lpszClassName, instance));
}

constexpr UINT kLifecycleSend = WM_APP + 101;
const wchar_t* lifecycle_path = nullptr;
int lifecycle_sends = 0;
UINT last_position_flags = 0;
LRESULT CALLBACK LifecycleWindowProc(HWND window, UINT message, WPARAM wp,
                                     LPARAM lp) {
  if (message == WM_WINDOWPOSCHANGING) {
    last_position_flags = reinterpret_cast<const WINDOWPOS*>(lp)->flags;
  }
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

void TestTargetMoveFlags(HWND window, const std::wstring& path) {
  const LONG_PTR original_style = GetWindowLongPtrW(window, GWL_STYLE);
  for (const wchar_t* full : {L"0", L"1"}) {
    CHECK(WritePrivateProfileStringW(L"Performance", L"FullWin", full,
                                     path.c_str()));
    for (int resize_all : {0, 1, 2, 3}) {
      const std::wstring value = std::to_wstring(resize_all);
      CHECK(WritePrivateProfileStringW(L"Advanced", L"ResizeAll", value.c_str(),
                                       path.c_str()));
      for (bool frame : {false, true}) {
        SetWindowLongPtrW(window, GWL_STYLE,
                          frame ? original_style | WS_THICKFRAME
                                : original_style & ~WS_THICKFRAME);
        CHECK(Load(window, path.c_str()));
        const bool sync = frame || (resize_all & 2);
        CHECK(((IsResizable(window) & 2) != 0) == sync);
        // The active drag's cached classification belongs to another window.
        state.resizable = sync ? 0 : 2;
        const auto request_for_target = [&] {
          RECT rect{};
          CHECK(GetWindowRect(window, &rect));
          windowRR request{};
          request.hwnd = window;
          request.x = rect.left + 1;
          request.y = rect.top;
          request.width = rect.right - rect.left;
          request.height = rect.bottom - rect.top;
          request.odpi = GetDpiForWindow(window);
          request.moveonly = 1;
          return request;
        };
        const auto check_flags = [&] {
          CHECK((last_position_flags & SWP_NOSIZE) != 0);
          CHECK(((last_position_flags & SWP_ASYNCWINDOWPOS) == 0) == sync);
        };
        // A frameless target with ResizeAll=0 used to reach the shared cache.
        BlkLst.AResize = altsnap::Blacklist(L"unmatched.exe");
        state.hwnd = window;
        state.cached_hwnd_blacklist = nullptr;
        state.exename = L"cache sentinel";
        windowRR request = request_for_target();
        MoveWindowNow(&request);
        CHECK(!request.hwnd);
        CHECK(state.cached_hwnd_blacklist == nullptr);
        CHECK(state.exename == L"cache sentinel");
        check_flags();
        request = request_for_target();
        // Exercise the public async submission path as used by snap/zone
        // actions.
        MoveWindowAsync(window, request.x, request.y, request.width,
                        request.height);
        CHECK(Unload());
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
        check_flags();
      }
    }
  }
  SetWindowLongPtrW(window, GWL_STYLE, original_style);
  CHECK(WritePrivateProfileStringW(L"Advanced", L"ResizeAll", L"0",
                                   path.c_str()));
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
  TestTargetMoveFlags(window, path);
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
  TestConfigReload(path);
  TestSnapGeometry();
  TestMovementResults();
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
