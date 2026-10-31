/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright (C) 2015    Stefan Sundin                                   *
 * This program is free software: you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation, either version 3 or later.              *
 * Modified By Raymond Gillibert in 2022                                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include "hooks/actions/click_actions.h"
#include "hooks/actions/window_actions.h"
#include "hooks/blacklist.h"
#include "hooks/hook_config.h"
#include "hooks/hook_startup.h"
#include "hooks/hooks_internal.h"
#include "hooks/hotkey_message.h"
#include "hooks/keyboard_hook.h"
#include "hooks/mouse_hook.h"
#include "hooks/mouse_move.h"
#include "hooks/movement.h"
#include "hooks/overlay/pin_window.h"
#include "hooks/overlay/window_menu.h"
#include "hooks/send_input.h"
#include "hooks/window/window_info.h"
#include "hooks/window/window_move.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

LRESULT CALLBACK HooksWinProc(HWND hwnd, UINT msg, WPARAM wParam,
                              LPARAM lParam);

HWND g_transhwnd[4];  // 4 windows to make a hollow window
HWND g_mchwnd;        // For the Action menu messages
std::atomic<UCHAR> g_InFinishMovement;
std::atomic<UCHAR> g_InMouseMove;

namespace {
enum class Phase { kIdle, kStarting, kActive, kStopping };
std::atomic<Phase> g_phase{Phase::kIdle};
std::atomic<DWORD> g_owner_thread{0};
// Intentionally process-lived: no Worker/queue destruction under loader lock,
// including process termination without Unload. The app refuses to unload an
// active DLL; normal session resources are released by Stop/CleanUpSession.
void ExecuteWorkerCommand(WorkerCommand command) {
  std::visit(
      [](auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, MouseMoveCommand>) {
          g_InMouseMove = 1;
          MouseMoveNow(value.point);
          g_InMouseMove = 0;
        } else if constexpr (std::is_same_v<T, FinishMovementCommand>) {
          FinishMovementNow();
        } else if constexpr (std::is_same_v<T, MoveWindowCommand>) {
          MoveWindowNow(&value.request);
        } else if constexpr (std::is_same_v<T, ClickCommand>) {
          SendClick(value.value);
        } else if constexpr (std::is_same_v<T, AltCtrlCommand>) {
          SendAltCtrlAlt();
        } else if constexpr (std::is_same_v<T, MoveResizeComboCommand>) {
          DoMoveResizeMaxMinComboAction();
        } else if constexpr (std::is_same_v<T, KillWindowCommand>) {
          ActionKillNow(value.window);
        }
      },
      command);
}
Worker& g_worker = *new Worker(ExecuteWorkerCommand);
}  // namespace

bool PostWorkerCommand(WorkerCommand command) {
  return g_worker.Post(std::move(command));
}
bool HooksActive() {
  return g_phase == Phase::kActive;
}

windowRR_t LastWin;
HookState state;
std::vector<RECT> monitors;
std::vector<snwdata> wnds;
std::vector<HWND> hwnds;

Blacklists BlkLst;  // All the blacklists, read from the ini file
// Input sequences of the "Shrt0".."ShrtZ" ini options: a count byte followed
// by that many (key, up/down) pairs. Empty if the sequence is not defined.
std::array<std::vector<UCHAR>, 36> g_inputSequences;
// Cursor data
HWND g_mainhwnd = nullptr;

// Hook data
HINSTANCE hinstDLL = nullptr;
std::atomic<HHOOK> mousehook{nullptr};
static HHOOK g_keyhook = nullptr;
// snapped windows database.
std::vector<snwdata> snwnds;
void LogState(const char* Title) {
  FILE* f = fopen("ad.log", "a");  // append data...
  if (!f) return;
  fputs(Title, f);
  fprintf(f,
          "action=%d\n"
          "moving=%d\n"
          "ctrl=%d\n"
          "shift=%d\n"
          "alt=%d\n"
          "alt1=%d\n",
          static_cast<int>(state.action.ac), static_cast<int>(state.moving),
          static_cast<int>(state.ctrl), static_cast<int>(state.shift),
          static_cast<int>(state.alt), static_cast<int>(state.alt1));

  fprintf(f,
          "clickbutton=%d\n"
          "hwnd=%lx\n"
          "lwhwnd=%lx\n"
          "lwend=%d\n"
          "lwmoveonly=%d\n"
          "lwmaximize=%d\n"
          "lwsnap=%d\n"
          "blockaltup=%d\n"
          "blockmouseup=%d\n"
          "ignorekey=%d\n\n\n",
          static_cast<int>(state.clickbutton),
          static_cast<DWORD>(reinterpret_cast<DorQWORD>(state.hwnd)),
          static_cast<DWORD>(reinterpret_cast<DorQWORD>(LastWin.hwnd)),
          static_cast<int>(LastWin.end), static_cast<int>(LastWin.moveonly),
          static_cast<int>(LastWin.maximize), static_cast<int>(LastWin.snap),
          static_cast<int>(state.blockaltup),
          static_cast<int>(state.blockmouseup),
          static_cast<int>(state.ignorekey));
  fclose(f);
}

// Changes the Volume on Windows 2000+ using VK_VOLUME_UP/VK_VOLUME_DOWN
// Also uses OLE API if available

std::vector<HWND> minhwnds;

// Window for timers only...
VOID CALLBACK TimerWindowProc(HWND hwnd, UINT msg, UINT_PTR idEvent,
                              DWORD dwTime) {
  if (g_phase != Phase::kActive || msg != WM_TIMER)
    return;  // Ignore stale timer callbacks during startup/teardown.

  // LOG("TimerWindowProc(%x, %u, %u, %lu)", (UINT)(UINT_PTR)hwnd, msg, idEvent,
  // dwTime);
  switch (idEvent) {
    case REHOOK_TIMER: {
      // Silently rehook hooks if they have been stopped (>= Win7 and
      // LowLevelHooksTimeout) This can often happen if locking or sleeping the
      // computer a lot
      POINT pt;
      GetCursorPos(
          &pt);  // I donot know if we should really use the ASYN version.
      if (mousehook && !SamePt(state.prevpt, pt)) {
        UnhookWindowsHookEx(mousehook.exchange(nullptr));
        mousehook =
            SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hinstDLL, 0);
      }
    } break;
    case SPEED_TIMER: {
      static POINT oldpt;
      static int has_moved_to_fixed_pt;
      if (state.moving)
        state.Speed = std::max(abs(oldpt.x - state.prevpt.x),
                               abs(oldpt.y - state.prevpt.y));
      else
        state.Speed = 0;
      oldpt = state.prevpt;
      if (state.moving == 1 && state.Speed == 0 && !has_moved_to_fixed_pt) {
        has_moved_to_fixed_pt = 1;
        MouseMove(state.prevpt);
      }
      if (state.Speed) has_moved_to_fixed_pt = 0;
    } break;
    case GRAB_TIMER: {
      // Grab the action after a certain amount of time of click down
      HWND ptwnd = nullptr;
      UCHAR buttonswaped = 0;
      POINT pt;
      GetMsgPT(&pt);  // Hopefully the real current cursor position
      if (IsSamePTT(&pt, &state.clickpt) &&
          GetAsyncKeyState(
              1 + (buttonswaped = !!GetSystemMetrics(SM_SWAPBUTTON))) &&
          (ptwnd = WindowFromPoint(pt)) &&
          !IsAreaLongClikcable(HitTestTimeoutbl(ptwnd, pt))) {
        // Determine if we should actually move the Window by probing with
        // AC_NONE
        state.hittest = 0;  // No specific hittest here.
        int ret = InitMovementAndActions(pt, nullptr, k_action_move, BT_PROBE);
        if (ret) {  // Release mouse click if we have to move.
          InterlockedIncrement(&state.ignoreclick);
          mouse_event(buttonswaped ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP,
                      0, 0, 0, GetMessageExtraInfo());
          InterlockedDecrement(&state.ignoreclick);
          InitMovementAndActions(pt, nullptr, k_action_move, 0);
        }
      }
      KillTimer(g_mainhwnd, GRAB_TIMER);
      return;
    } break;
    case HIDELAYOUT_TIMER: {
      state.forcelayoutdisplay = 0;
      ShowSnapLayoutPreview(0);
      KillTimer(g_mainhwnd, HIDELAYOUT_TIMER);
    } break;

    default:;
  }
}
static void KillAllTimers() {
  KillTimer(g_mainhwnd, REHOOK_TIMER);
  KillTimer(g_mainhwnd, SPEED_TIMER);
  KillTimer(g_mainhwnd, GRAB_TIMER);
  KillTimer(g_mainhwnd, HIDELAYOUT_TIMER);
}

LRESULT CALLBACK HooksWinProc(HWND hwnd, UINT msg, WPARAM wParam,
                              LPARAM lParam) {
  if (g_phase != Phase::kActive &&
      (msg == WM_HOTKEY || msg == WM_STACKLIST || msg == WM_SETLAYOUTNUM))
    return 0;
  if (msg == WM_HOTKEY) {
    const auto request = DecodeHotkeyMessage(
        wParam, lParam, RegisteredHotkeys(), conf.UsePtWindow);
    if (!request) return 0;
    const action_t action = request->action;
    const bool ptwindow = request->point_window;

    if (action.ac > AC_RESIZE) {  // Exclude resize action in case...
      POINT pt;
      GetMsgPT(&pt);
      static const enum action noinitactions[] = {AC_KILL, AC_PAUSE, AC_RESUME,
                                                  AC_ASONOFF, AC_NONE};
      if (IsActionInList(static_cast<enum action>(action.ac), noinitactions)) {
        // Some actions pass directly through the default blacklists...
        HWND targethwnd =
            ptwindow ? WindowFromPoint(pt) : GetForegroundWindow();
        if (IsWindow(targethwnd)) {
          SClickActions(targethwnd, action);
        }
      } else {
        // For all other actions.
        HWND target_hwnd = nullptr;
        if (!ptwindow) {
          target_hwnd = GetForegroundWindow();
          // List of actions for which point should default to center.
          static const enum action resetPTaclist[] = {AC_MENU, AC_NSTACKED,
                                                      AC_PSTACKED, AC_NONE};
          if (IsActionInList(static_cast<enum action>(action.ac),
                             resetPTaclist)) {
            state.ignorept = 1;
          }
          // Might be of use for something?
          //} else if (IsActionInList(action, ptCenterAcList)) {
          //    // We must select the center of the current window as the point
          //    // pt to which the action will be done.
          //    RECT rc;
          //    GetWindowRect(target_hwnd, &rc);
          //    pt.x = (rc.left+rc.right)/2;
          //    pt.y = (rc.top+rc.bottom)/2;
          //}
        }
        state.shift = state.ctrl = 0;  // In case...
        InitMovementAndActions(pt, target_hwnd, action, 0);
        state.blockmouseup = 0;  // We must not block mouseup in this case...
        state.ignorept = 0;      // Reset...
      }
      return 0;
    }
  } else if (msg == WM_STACKLIST) {
    TrackMenuOfWindows(reinterpret_cast<WNDENUMPROC>(lParam), wParam);
    return 0;
  } else if (msg == WM_SETLAYOUTNUM) {
    SetLayoutNumber(wParam);
    return 0;
  } else if (msg == WM_GETLAYOUTREZ) {
    if (wParam >= Zones.size()) return 0;
    return GetLayoutRez(static_cast<int>(wParam));
  } else if (msg == WM_GETBESTLAYOUT) {
    return GetBestLayoutFromMonitors();
  } else if (msg == WM_GETZONES) {
    const auto zones = GetZones(wParam);
    RECT** output = reinterpret_cast<RECT**>(lParam);
    if (output) *output = zones.empty() ? nullptr : zones.data();
    return zones.size();
  }

  return DefWindowProc(hwnd, msg, wParam, lParam);
}
namespace {
// UI thread only. Also used after any partially completed Load().
void CleanUpSession() {
  g_phase = Phase::kStopping;
  g_worker.Close();
  if (g_keyhook) {
    UnhookWindowsHookEx(g_keyhook);
    g_keyhook = nullptr;
  }
  if (HHOOK hook = mousehook.exchange(nullptr)) UnhookWindowsHookEx(hook);
  KillAllTimers();
  UnregisterAllHotkeys();

  // Keep configuration, overlays and all shared containers alive until every
  // accepted job has finished. Stop services synchronous sends to this thread;
  // phase guards reject reentrant lifecycle calls and action messages.
  g_worker.Stop();
  SetKeepMouseHook(false);
  UnhookMouse();  // Restore opacity/cursor clipping even if disabled mid-drag.
  KillAllTimers();
  KillAltSnapMenu();
  for (HWND& window : g_transhwnd) {
    if (window) DestroyWindow(window);
    window = nullptr;
  }
  EnumThreadWindows(GetCurrentThreadId(), PostPinWindowsProcMessage, WM_CLOSE);
  UnregisterClass(APP_NAME L"-Timers", hinstDLL);
  UnregisterClass(APP_NAME L"-SClick", hinstDLL);
  UnregisterClass(APP_NAME L"-Trans", hinstDLL);
  UnregisterClass(APP_NAME L"-Pin", hinstDLL);
  UnregisterClass(APP_NAME L"-HotKeys", hinstDLL);
  SnapLayoutPreviewCreateDestroy(nullptr);
  FreeBlacklists();
  FreeAllInputSequences();
  monitors.clear();
  hwnds.clear();
  wnds.clear();
  snwnds.clear();
  minhwnds.clear();
  FreeZones();
  state = {};
  LastWin = {};
  g_InFinishMovement = 0;
  g_InMouseMove = 0;
  g_mainhwnd = nullptr;
  g_phase = Phase::kIdle;
}
}  // namespace

BOOL WINAPI Unload() {
  // Windows and low-level hooks belong to the Load caller. A foreign thread
  // must not destroy them, and a synchronous send during Stop must not reenter.
  if (g_owner_thread != 0 && g_owner_thread != GetCurrentThreadId()) {
    LOG("Unload rejected: wrong thread");
    return FALSE;
  }
  Phase expected = Phase::kActive;
  if (!g_phase.compare_exchange_strong(expected, Phase::kStopping)) {
    LOG("Unload rejected: phase %d", static_cast<int>(expected));
    return expected == Phase::kIdle;
  }
  CleanUpSession();
  return TRUE;
}

// Create a window for msessages handeling timers, menu etc.
HWND KreateMsgWin(WNDPROC proc, const wchar_t* name, LONG_PTR userdata) {
  WNDCLASS wnd;
  if (!GetClassInfo(hinstDLL, name, &wnd)) {
    // Register the class if no already created.
    ZeroObject(wnd);
    wnd.lpfnWndProc = proc;
    wnd.hInstance = hinstDLL;
    wnd.lpszClassName = name;
    RegisterClass(&wnd);
  }
  HWND parent = (LOBYTE(LOWORD(GetVersion())) >= 5) ? HWND_MESSAGE : g_mainhwnd;
  HWND hwnd = CreateWindowEx(0, wnd.lpszClassName, nullptr, 0, 0, 0, 0, 0,
                             parent, nullptr, hinstDLL, nullptr);
  if (hwnd && userdata) SetWindowLongPtr(hwnd, GWLP_USERDATA, userdata);
  return hwnd;
}

// Has to be called at startup, it mainly reads the config.
WNDPROC WINAPI Load(HWND mainhwnd, const wchar_t* inipath) {
  return LoadSession(mainhwnd, inipath, HookStartup{});
}

WNDPROC LoadSession(HWND mainhwnd, const wchar_t* inipath,
                    const HookStartup& startup) {
  if (!mainhwnd || !inipath ||
      GetWindowThreadProcessId(mainhwnd, nullptr) != GetCurrentThreadId())
    return nullptr;
  Phase expected = Phase::kIdle;
  if (!g_phase.compare_exchange_strong(expected, Phase::kStarting))
    return nullptr;
  g_owner_thread = GetCurrentThreadId();
  g_mainhwnd = mainhwnd;
  state = {};
  LastWin = {};
  if (!ReadConfig(inipath, startup.create_trans_window) ||
      !g_worker.Start(startup.worker)) {
    CleanUpSession();
    return nullptr;
  }
  RegisterAllHotkeys(inipath);
  if (KeepMouseHook()) {
    HookMouse();
    if (!mousehook ||
        !SetTimer(g_mainhwnd, REHOOK_TIMER, 5000, TimerWindowProc)) {
      CleanUpSession();
      return nullptr;
    }
  }
  g_keyhook =
      startup.keyboard_hook(WH_KEYBOARD_LL, LowLevelKeyboardProc, hinstDLL, 0);
  if (!g_keyhook) {
    CleanUpSession();
    return nullptr;
  }
  g_phase = Phase::kActive;
  return HooksWinProc;
}

}  // namespace altsnap::hooks
using altsnap::hooks::hinstDLL;

// Do not forget the -e_DllMain@12 for gcc... -eDllMain for x86_64
BOOL APIENTRY DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hInst);
    hinstDLL = hInst;
  }
  return TRUE;
}
