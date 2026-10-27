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
#include <vector>

#include "common/ini_section.h"
#include "hooks/actions/click_actions.h"
#include "hooks/blacklist.h"
#include "hooks/hook_config.h"
#include "hooks/hooks_internal.h"
#include "hooks/input_state.h"
#include "hooks/keyboard_hook.h"
#include "hooks/mouse_hook.h"
#include "hooks/mouse_move.h"
#include "hooks/movement.h"
#include "hooks/overlay/pin_window.h"
#include "hooks/overlay/trans_window.h"
#include "hooks/overlay/window_menu.h"
#include "hooks/window/snap.h"
#include "hooks/window/window_info.h"
#include "hooks/window/window_move.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

LRESULT CALLBACK HooksWinProc(HWND hwnd, UINT msg, WPARAM wParam,
                              LPARAM lParam);

HWND g_transhwnd[4];  // 4 windows to make a hollow window
HWND g_mchwnd;        // For the Action menu messages
DWORD g_WorkerThreadID;
static HANDLE g_WorkerThreadHANDLE;
UCHAR g_InFinishMovement;
UCHAR g_InMouseMove;

windowRR_t LastWin;
HookState state;
std::vector<RECT> monitors;
std::vector<snwdata> wnds;
std::vector<HWND> hwnds;
struct config conf;

Blacklists BlkLst;  // All the blacklists, read from the ini file
// Input sequences of the "Shrt0".."ShrtZ" ini options: a count byte followed
// by that many (key, up/down) pairs. Empty if the sequence is not defined.
std::array<std::vector<UCHAR>, 36> g_inputSequences;
// Cursor data
HWND g_mainhwnd = nullptr;

// Hook data
HINSTANCE hinstDLL = nullptr;
HHOOK mousehook = nullptr;
static HHOOK g_keyhook = nullptr;
/////////////////////////////////////////////////////////////////////////////
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

/////////////////////////////////////////////////////////////////////////////
// Changes the Volume on Windows 2000+ using VK_VOLUME_UP/VK_VOLUME_DOWN
// Also uses OLE API if available

/////////////////////////////////////////////////////////////////////////////
std::vector<HWND> minhwnds;
/////////////////////////////////////////////////////////////////////////////
// Window for timers only...
VOID CALLBACK TimerWindowProc(HWND hwnd, UINT msg, UINT_PTR idEvent,
                              DWORD dwTime) {
  if (msg != WM_TIMER) return;  // should never happen.

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
        UnhookWindowsHookEx(mousehook);
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
        int ret =
            init_movement_and_actions(pt, nullptr, k_action_move, BT_PROBE);
        if (ret) {  // Release mouse click if we have to move.
          InterlockedIncrement(&state.ignoreclick);
          mouse_event(buttonswaped ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP,
                      0, 0, 0, GetMessageExtraInfo());
          InterlockedDecrement(&state.ignoreclick);
          init_movement_and_actions(pt, nullptr, k_action_move, 0);
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
}

LRESULT CALLBACK HooksWinProc(HWND hwnd, UINT msg, WPARAM wParam,
                              LPARAM lParam) {
  if (msg == WM_HOTKEY) {
    int ptwindow = 0;
    action_t action = k_action_none;
    if (wParam > 0xC000) {  // HOTKEY
      // The user Pressed a hotkey.
      size_t action_idx = wParam - 0xC001;  // Remove the Offset
      assert(action_idx < std::size(conf.KBShortcutsList));
      action = conf.KBShortcutsList[action_idx];
      ptwindow = conf.UsePtWindow;
      LOG("Hotkey Pressed, action = %d", action.ac);
    } else if (0x0000 < wParam && wParam < 0x1000) {
      // The user called AltSnap.exe -afACTION
      UNPACK_ACTION(action, lParam);
      assert(wParam - 0x0000 == action.ac);
      ptwindow = 0;
    } else if (0x1000 < wParam && wParam < 0x2000) {
      // The user called AltSnap.exe -apACTION
      UNPACK_ACTION(action, lParam);
      assert(wParam - 0x1000 == action.ac);
      ptwindow = 1;
    }

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
        init_movement_and_actions(pt, target_hwnd, action, 0);
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
    return GetLayoutRez(wParam);
  } else if (msg == WM_GETBESTLAYOUT) {
    return GetBestLayoutFromMonitors();
  } else if (msg == WM_GETZONES) {
    auto layout_idx = static_cast<unsigned>(wParam);
    RECT** dZones = reinterpret_cast<RECT**>(lParam);
    if (dZones) *dZones = Zones[layout_idx].data();
    return Zones[layout_idx].size();  // Return number of zones on the layout
  }

  return DefWindowProc(hwnd, msg, wParam, lParam);
}
void WINAPI Unload() {
  // UnKook keyboard.
  if (g_keyhook) {
    [[maybe_unused]] const BOOL ret = UnhookWindowsHookEx(g_keyhook);
    LOG("UnhookWindowsHookEx(%llx) -> %d",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_keyhook)),
        static_cast<int>(ret));
  }
  g_keyhook = nullptr;

  // Quit Worker Thread...
  PostThreadMessage(g_WorkerThreadID, WM_DOWORK, 0, 0);

  conf.keepMousehook = 0;
  if (mousehook) {
    UnhookWindowsHookEx(mousehook);
    mousehook = nullptr;
  }
  KillAllTimers();
  KillAltSnapMenu();
  if (conf.TransWinOpacity) {
    DestroyWindow(g_transhwnd[0]);
    g_transhwnd[0] = nullptr;
  } else {
    for (auto& i : g_transhwnd) {
      DestroyWindow(i);
      i = nullptr;
    }
  }

  for (size_t idx = 0; idx < std::size(conf.KBShortcutsList); idx++)
    UnregisterHotKey(g_mainhwnd, 0xC001 + idx);

  EnumThreadWindows(GetCurrentThreadId(), PostPinWindowsProcMessage, WM_CLOSE);
  UnregisterClass(APP_NAME L"-Timers", hinstDLL);
  UnregisterClass(APP_NAME L"-SClick", hinstDLL);
  UnregisterClass(APP_NAME L"-Trans", hinstDLL);
  UnregisterClass(APP_NAME L"-Pin", hinstDLL);
  UnregisterClass(APP_NAME L"-HotKeys", hinstDLL);

  SnapLayoutPreviewCreateDestroy(nullptr /*DESTROY*/);

  freeblacklists();

  freeallinputSequences();

  monitors = {};
  hwnds = {};
  wnds = {};
  snwnds = {};
  minhwnds = {};
  freezones();

  // Wait for worker thread to have a clean closing...
  WaitForSingleObject(g_WorkerThreadHANDLE, 5000);
  CloseHandle(g_WorkerThreadHANDLE);
}
///////////////////////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////////////////////
// Has to be called at startup, it mainly reads the config.
WNDPROC WINAPI Load(HWND mainhwnd, const wchar_t* inipath) {
  assert(g_keyhook == nullptr);  // We cannot Load twice again...

  // Reset stuff...
  ZeroObject(state);
  ZeroObject(LastWin);

  ReadConfig(inipath);

  // Capture main hwnd from caller. This is also the cursor wnd
  g_mainhwnd = mainhwnd;

  // read and register all shortcuts related options.
  registerAllHotkeys(inipath);

  // Hook mouse if a permanent hook is needed
  if (conf.keepMousehook) {
    HookMouse();
    SetTimer(g_mainhwnd, REHOOK_TIMER, 5000,
             static_cast<TIMERPROC>(TimerWindowProc));  // Start rehook timer
  }

  // Set up the keyboard hook
  g_keyhook =
      SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hinstDLL, 0);
  if (g_keyhook == nullptr) {
    LOG("ERROR: Keyboard HOOK could not be set");
  }

  // Create worker thread.
  g_WorkerThreadHANDLE =
      CreateThread(nullptr, STACK, WorkerThread, nullptr,
                   STACK_SIZE_PARAM_IS_A_RESERVATION, &g_WorkerThreadID);
  // SetThreadPriority(g_WorkerThreadHANDLE, THREAD_PRIORITY_BELOW_NORMAL);

  return HooksWinProc;
}
/////////////////////////////////////////////////////////////////////////////

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
