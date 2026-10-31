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
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <vector>

#include "hooks/actions/click_actions.h"
#include "hooks/actions/window_actions.h"
#include "hooks/blacklist.h"
#include "hooks/command_dispatcher.h"
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

namespace {
enum class Phase { kIdle, kStarting, kActive, kStopping };
std::atomic<Phase> g_phase{Phase::kIdle};
std::atomic<DWORD> g_owner_thread{0};
HookSession g_session;
HHOOK g_keyhook = nullptr;
decltype(&SetWindowsHookExW) g_install_keyboard = SetWindowsHookExW;
unsigned g_keyboard_slot = 0;

template <unsigned Slot>
LRESULT CALLBACK KeyboardSlotProc(int code, WPARAM wp, LPARAM lp) {
  // Install the replacement first. Exactly one slot handles input while both
  // hooks coexist, avoiding a gap or duplicate handling during the handoff.
  if (!HooksActive() || g_keyboard_slot != Slot)
    return CallNextHookEx(nullptr, code, wp, lp);
  return LowLevelKeyboardProc(code, wp, lp);
}
HOOKPROC KeyboardProcedure(unsigned slot) {
  return slot == 0 ? KeyboardSlotProc<0> : KeyboardSlotProc<1>;
}
void RefreshKeyboardHook() {
  const unsigned next_slot = 1 - g_keyboard_slot;
  const HHOOK replacement = g_install_keyboard(
      WH_KEYBOARD_LL, KeyboardProcedure(next_slot), hinstDLL, 0);
  if (!replacement) return;  // Keep the old hook; the next timer retries.
  const HHOOK previous = std::exchange(g_keyhook, replacement);
  g_keyboard_slot = next_slot;
  if (previous && previous != replacement) UnhookWindowsHookEx(previous);
}

unsigned g_command_depth = 0;  // Owner-thread commands and callback frames.

void ExecuteOwnerCommand(WorkerCommand command) {
  // Session() enforces the same affinity for callbacks, timers and commands.
  (void)Session();
  SessionExecutionScope execution_scope;
  std::visit(
      [](auto value) {
        using T = decltype(value);
        if constexpr (std::is_same_v<T, MouseMoveCommand>) {
          if (value.gesture != Session().gesture) return;
          Session().move_running = true;
          MouseMoveNow(value.point);
          Session().move_running = false;
        } else if constexpr (std::is_same_v<T, FinishMovementCommand>) {
          if (value.gesture == Session().gesture) {
            FinishMovementNow();
          } else if (Session().finish_pending == value.gesture) {
            // Release only this command's barrier, never a newer finish.
            Session().finish_pending.reset();
          }
        } else if constexpr (std::is_same_v<T, MoveWindowCommand>) {
          if (IsWindow(value.request.hwnd)) MoveWindowNow(&value.request);
        } else if constexpr (std::is_same_v<T, ClickCommand>) {
          SendClick(value.value);
        } else if constexpr (std::is_same_v<T, AltCtrlCommand>) {
          SendAltCtrlAlt();
        } else if constexpr (std::is_same_v<T, MoveResizeComboCommand>) {
          if (value.gesture == Session().gesture &&
              IsMovementAction(Session().input.action))
            DoMoveResizeMaxMinComboAction();
        } else {
          static_assert(std::is_same_v<T, KillWindowCommand>);
          std::abort();  // Kill commands execute on the worker, never here.
        }
      },
      std::move(command));
}

// Process-lived: explicit Stop precedes DLL unload, never a loader-lock join.
CommandDispatcher& g_dispatcher = *new CommandDispatcher(ExecuteOwnerCommand);
Worker& g_worker = *new Worker([](WorkerCommand command) {
  if (const auto* kill = std::get_if<KillWindowCommand>(&command)) {
    ActionKillNow(kill->window, kill->shift);
  } else {
    g_dispatcher.Dispatch(std::move(command));
  }
});
}  // namespace

SessionExecutionScope::SessionExecutionScope()
    : bound_(TrySession() != nullptr) {
  if (bound_) ++g_command_depth;
}
SessionExecutionScope::~SessionExecutionScope() {
  if (bound_) --g_command_depth;
}

HookSession* TrySession() {
  const DWORD owner = g_owner_thread.load();
  return owner && owner == GetCurrentThreadId() ? &g_session : nullptr;
}
HookSession& Session() {
  HookSession* session = TrySession();
  if (!session) std::abort();
  return *session;
}

bool PostWorkerCommand(WorkerCommand command) {
  const DWORD owner = g_owner_thread.load();
  if (!owner || owner != GetCurrentThreadId()) return false;
  return g_worker.Post(std::move(command));
}
bool HooksActive() {
  return g_phase == Phase::kActive;
}

Blacklists BlkLst;  // All the blacklists, read from the ini file
// Input sequences of the "Shrt0".."ShrtZ" ini options: a count byte followed
// by that many (key, up/down) pairs. Empty if the sequence is not defined.
std::array<std::vector<UCHAR>, 36> g_inputSequences;
// Cursor data
HWND g_mainhwnd = nullptr;

// Hook data
HINSTANCE hinstDLL = nullptr;
std::atomic<HHOOK> mousehook{nullptr};

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
          static_cast<int>(Session().input.action.ac),
          static_cast<int>(Session().input.moving),
          static_cast<int>(Session().input.ctrl),
          static_cast<int>(Session().input.shift),
          static_cast<int>(Session().input.alt),
          static_cast<int>(Session().input.alt1));

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
          static_cast<int>(Session().input.clickbutton),
          static_cast<DWORD>(reinterpret_cast<DorQWORD>(Session().input.hwnd)),
          static_cast<DWORD>(
              reinterpret_cast<DorQWORD>(Session().last_window.hwnd)),
          static_cast<int>(Session().last_window.end),
          static_cast<int>(Session().last_window.moveonly),
          static_cast<int>(Session().last_window.maximize),
          static_cast<int>(Session().last_window.snap),
          static_cast<int>(Session().input.blockaltup),
          static_cast<int>(Session().input.blockmouseup),
          static_cast<int>(Session().input.ignorekey));
  fclose(f);
}

// Changes the Volume on Windows 2000+ using VK_VOLUME_UP/VK_VOLUME_DOWN
// Also uses OLE API if available

// Window for timers only...
VOID CALLBACK TimerWindowProc(HWND hwnd, UINT msg, UINT_PTR idEvent,
                              DWORD dwTime) {
  if (!HooksActive() || !TrySession() || msg != WM_TIMER)
    return;  // Ignore stale timer callbacks during startup/teardown.
  SessionExecutionScope execution_scope;

  // LOG("TimerWindowProc(%x, %u, %u, %lu)", (UINT)(UINT_PTR)hwnd, msg, idEvent,
  // dwTime);
  switch (idEvent) {
    case REHOOK_TIMER: {
      RefreshKeyboardHook();
      // Silently rehook hooks if they have been stopped (>= Win7 and
      // LowLevelHooksTimeout) This can often happen if locking or sleeping the
      // computer a lot
      POINT pt;
      GetCursorPos(
          &pt);  // I donot know if we should really use the ASYN version.
      if (mousehook && !SamePt(Session().input.prevpt, pt)) {
        UnhookWindowsHookEx(mousehook.exchange(nullptr));
        mousehook =
            SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hinstDLL, 0);
      }
    } break;
    case SPEED_TIMER: {
      POINT& oldpt = Session().speed_point;
      bool& has_moved_to_fixed_pt = Session().moved_to_fixed_point;
      if (Session().input.moving)
        Session().input.Speed =
            std::max(abs(oldpt.x - Session().input.prevpt.x),
                     abs(oldpt.y - Session().input.prevpt.y));
      else
        Session().input.Speed = 0;
      oldpt = Session().input.prevpt;
      if (Session().input.moving == 1 && Session().input.Speed == 0 &&
          !has_moved_to_fixed_pt) {
        has_moved_to_fixed_pt = 1;
        MouseMove(Session().input.prevpt);
      }
      if (Session().input.Speed) has_moved_to_fixed_pt = 0;
    } break;
    case GRAB_TIMER: {
      // A held click is not replayed onto a possibly different target after a
      // drag finishes. Cancel this attempt without injecting an up event.
      if (Session().finish_pending || Session().move_running) {
        KillTimer(g_mainhwnd, GRAB_TIMER);
        return;
      }
      // Grab the action after a certain amount of time of click down
      HWND ptwnd = nullptr;
      UCHAR buttonswaped = 0;
      POINT pt;
      GetMsgPT(&pt);  // Hopefully the real current cursor position
      if (IsSamePTT(&pt, &Session().input.clickpt) &&
          GetAsyncKeyState(
              1 + (buttonswaped = !!GetSystemMetrics(SM_SWAPBUTTON))) &&
          (ptwnd = WindowFromPoint(pt)) &&
          !IsAreaLongClikcable(HitTestTimeoutbl(ptwnd, pt))) {
        // Determine if we should actually move the Window by probing with
        // AC_NONE
        Session().input.hittest = 0;  // No specific hittest here.
        const auto ret =
            InitMovementAndActions(pt, nullptr, k_action_move, BT_PROBE);
        if (ret == ActionStartResult::kConsumed && !MovementStartBusy()) {
          // Release the click only after an admissible probe.
          InterlockedIncrement(&Session().input.ignoreclick);
          mouse_event(buttonswaped ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP,
                      0, 0, 0, GetMessageExtraInfo());
          InterlockedDecrement(&Session().input.ignoreclick);
          InitMovementAndActions(pt, nullptr, k_action_move, 0);
        }
      }
      KillTimer(g_mainhwnd, GRAB_TIMER);
      return;
    } break;
    case HIDELAYOUT_TIMER: {
      Session().input.forcelayoutdisplay = 0;
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
  if ((!HooksActive() || !TrySession()) &&
      (msg == WM_HOTKEY || msg == WM_STACKLIST || msg == WM_SETLAYOUTNUM ||
       msg == WM_GETBESTLAYOUT))
    return 0;
  SessionExecutionScope execution_scope;
  if (msg == WM_HOTKEY) {
    const auto request = DecodeHotkeyMessage(
        wParam, lParam, RegisteredHotkeys(), conf.UsePtWindow);
    if (!request || MovementStartBusy()) return 0;
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
            Session().input.ignorept = 1;
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
        Session().input.shift = Session().input.ctrl = 0;  // In case...
        InitMovementAndActions(pt, target_hwnd, action, 0);
        Session().input.blockmouseup =
            0;  // We must not block mouseup in this case...
        Session().input.ignorept = 0;  // Reset...
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
  g_install_keyboard = SetWindowsHookExW;
  if (HHOOK hook = mousehook.exchange(nullptr)) UnhookWindowsHookEx(hook);
  KillAllTimers();
  UnregisterAllHotkeys();

  // Keep configuration, overlays and all shared containers alive until every
  // accepted job has finished. Stop services synchronous sends to this thread;
  // phase guards reject reentrant lifecycle calls and action messages.
  g_worker.Stop();
  g_dispatcher.Stop();
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
  FreeZones();
  Session() = {};
  g_mainhwnd = nullptr;
  g_owner_thread = 0;
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
  // A foreign-window callback must not join a worker waiting for this command.
  if (g_command_depth != 0) return FALSE;
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
  g_install_keyboard = startup.keyboard_hook;
  g_keyboard_slot = 0;
  Session() = {};
  if (!ReadConfig(inipath, startup.create_trans_window) ||
      !g_dispatcher.Start(hinstDLL) || !g_worker.Start(startup.worker)) {
    CleanUpSession();
    return nullptr;
  }
  RegisterAllHotkeys(inipath);
  if (KeepMouseHook()) {
    HookMouse();
    if (!mousehook) {
      CleanUpSession();
      return nullptr;
    }
  }
  g_keyhook = g_install_keyboard(
      WH_KEYBOARD_LL, KeyboardProcedure(g_keyboard_slot), hinstDLL, 0);
  if (!g_keyhook ||
      !SetTimer(g_mainhwnd, REHOOK_TIMER, 5000, TimerWindowProc)) {
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
