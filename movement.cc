// movement (split out of hooks.cc).

#include "movement.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "blacklist.h"
#include "click_actions.h"
#include "hooks_internal.h"
#include "ini_section.h"
#include "input_state.h"
#include "mouse_hook.h"
#include "send_input.h"
#include "snap.h"
#include "trans_window.h"
#include "window_actions.h"
#include "window_info.h"
#include "window_menu.h"
#include "window_move.h"
#include "zones.h"

namespace altsnap::hooks {

/////////////////////////////////////////////////////////////////////////////
static void StartSpeedMes() {
  if (conf.AeroMaxSpeed < MAX_SNAP_SPEED)
    SetTimer(g_mainhwnd, SPEED_TIMER, conf.AeroSpeedTau,
             static_cast<TIMERPROC>(TimerWindowProc));
}
void StopSpeedMes() {
  if (conf.AeroMaxSpeed < MAX_SNAP_SPEED)
    KillTimer(g_mainhwnd, SPEED_TIMER);  // Stop speed measurement
}
[[nodiscard]] static int DoubleClamp(
    int ptx, int left, int right,
    int rwidth) {  // Same logic applies to y axis
  int posx = 0;
  int cwidth = std::min(rwidth, right - left);
  if (right - ptx < cwidth / 2)
    posx = right - rwidth;
  else if (ptx - left < cwidth / 2)
    posx = left;
  else
    posx = ptx - 2 * rwidth / 5;

  return posx;
}
/////////////////////////////////////////////////////////////////////////////
// If we pass buttonX BT_PROBE it will tell us if we pass the blacklist.
int init_movement_and_actions(POINT pt, HWND hwnd, action_t action,
                              int buttonX) {
  // RGTICTAC tt; RGTic(&tt);
  LOG("\ninit_movement_and_actions(pt=%d,%d, hwnd=%x, action=%d, button=%d)",
      static_cast<int>(pt.x), static_cast<int>(pt.y),
      static_cast<UINT>(reinterpret_cast<INT_PTR>(hwnd)),
      static_cast<int>(action.ac), buttonX);
  RECT wnd;
  state.prevpt = pt;  // in case
  int button = LOWORD(buttonX);
  UCHAR probemode = HIWORD(buttonX);

  // Make sure nothing is in the way
  HideCursor();
  state.sclickhwnd = nullptr;
  KillAltSnapMenu();
  // LOGA("Nothing in the ways in %d us", (int)RGTac(&tt));

  // Get window from point or use the given one.
  // Get MDI chlild hwnd or root hwnd if not MDI!
  state.mdiclient = nullptr;
  state.hwnd = hwnd ? hwnd : MDIorNOT(WindowFromPoint(pt), &state.mdiclient);

  // LOGA("MDI info got %d us", (int)RGTac(&tt));
  if (!state.hwnd || (g_InMouseMove && state.hwnd == LastWin.hwnd) ||
      !IsWindow(state.hwnd)) {
    LOG("Init failed: state.hwnd=%x, LastWin.hwnd=%x, g_InMouseMove = %d",
        static_cast<UINT>(reinterpret_cast<INT_PTR>(state.hwnd)),
        static_cast<UINT>(reinterpret_cast<INT_PTR>(LastWin.hwnd)),
        static_cast<int>(g_InMouseMove));
    return 0;
  }

  // Get monitor info
  HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  GetMonitorInfo(monitor, &mi);
  CopyRect(&state.origin.mon, &mi.rcWork);
  // LOGA("MonitorInfo got! in %d us", (int)RGTac(&tt));

  // state.mdipt HAS to be set to Zero for ClientToScreen adds the offset
  state.mdipt.x = state.mdipt.y = 0;
  if (state.mdiclient) {
    GetMDInfo(&state.mdipt, &mi.rcMonitor);
    CopyRect(&state.origin.mon, &mi.rcMonitor);
  }
  // LOGA("MDI info got! in %d us", (int)RGTac(&tt));

  WINDOWPLACEMENT wndpl;
  wndpl.length = sizeof(wndpl);
  // Return if window is blacklisted,
  // if we can't get information about it,
  // or if the window is fullscreen.
  // state.hwnd == GetDesktopWindow()
  // state.hwnd == GetShellWindow()
  if (blacklisted(state.hwnd, BlkLst.Processes) ||
      !lstrcmp(state.classname, APP_NAME L"-Pin") ||
      (blacklisted(state.hwnd, BlkLst.Windows) && !state.hittest &&
       (button <
        BT_WHEELD /* || BT_HWHEELU > button*/))  // does not apply in titlebar,
                                                 // nor for the wheel action...
      || GetWindowPlacement(state.hwnd, &wndpl) == 0 ||
      GetWindowRect(state.hwnd, &wnd) == 0 ||
      ((state.origin.maximized = IsZoomed(state.hwnd)) && conf.BLMaximized) ||
      ((state.origin.fullscreen =
            IsFullscreenF(state.hwnd, &wnd, &mi.rcMonitor)) &&
       !conf.FullScreen)) {
    return 0;
  }

  // LOGA("Blacklists passed! in %d us", (int)RGTac(&tt));

  // If no action is to be done then we passed all balcklists
  if (probemode || action.ac == AC_NONE) return 1;

  // Set state
  state.blockaltup = state.alt;  // If alt is down...
  // return if window has to be moved/resized and does not respond in 1/4 s.
  state.prevpt = pt;

  // Set origin width/height by default from current state/wndpl.
  state.origin.monitor =
      MonitorFromWindow(state.hwnd, MONITOR_DEFAULTTONEAREST);
  state.origin.dpi = GetDpiForWindow(state.hwnd);
  state.origin.width =
      wndpl.rcNormalPosition.right - wndpl.rcNormalPosition.left;
  state.origin.height =
      wndpl.rcNormalPosition.bottom - wndpl.rcNormalPosition.top;
  state.resizable = IsResizable(state.hwnd);

  GetMinMaxInfo(state.hwnd, &state.mmi.Min,
                &state.mmi.Max);  // for CLAMPH/W functions
  // LOGA("Going to start action! in %d us", (int)RGTac(&tt));

  // Do things depending on what button was pressed
  if (MOUVEMENT(action)) {
    state.sactiondone = action;
    if (GetProp(state.hwnd, APP_MOVEONOFF)) {
      state.action = k_action_none;
      return 0;  // Movement was disabled for this window.
    }
    // AutoFocus on movement/resize.
    if (conf.AutoFocus || IsCtrlDown()) {
      SetForegroundWindowL(state.hwnd);
    }
    if (conf.DragSendsAltCtrl && !(GetKeyState(VK_MENU) & 0x8000) &&
        !(GetKeyState(VK_SHIFT) & 0x8000)) {
      // This will pop down menu and stuff
      // In case autofocus did not do it.
      PostThreadMessage(g_WorkerThreadID, WM_DOWORK,
                        reinterpret_cast<WPARAM>(SendAltCtrlAlt), 0);
    }

    // Set action statte.
    state.action = action;  // MOVE OR RESIZE
    state.moving = 0;
    // Wether or not we will use the zones
    state.usezones = ((conf.UseZones & 9) == 9) ^ state.shift;

    state.enumed = 0;  // Reset enum stuff
    StartSpeedMes();   // Speed timer

    int ret = 0;
    if (state.action.ac == AC_MOVE) {
      ret = ActionMove(pt, button);
    } else {
      ret = ActionResize(pt, &wnd, button);
    }
    if (ret == 1) return 1;  // block mouse down!
    if (ret == 0) return 0;  // Next hook!
    // else ret == -1 ...
    UpdateCursor(pt);
    SetWindowTrans(state.hwnd);

    LastWin.start = 1;
  } else if (BT_WHEELD <= button && button <= BT_HWHEELU) {
    // Wheel actions, directly return here
    // because maybe the action will not be done
    if (GetProp(state.hwnd, APP_MOVEONOFF)) {
      state.action = k_action_none;
      return 0;  // Wheel was disabled for this window.
    }
    return DoWheelActions(state.hwnd, action);
  } else if (action.ac == AC_RESTORE) {
    int rwidth = 0, rheight = 0;
    if (GetRestoreData(state.hwnd, &rwidth, &rheight) & SNAPPED) {
      ClearRestoreData(state.hwnd);
      RECT rc;
      GetWindowRect(state.hwnd, &rc);
      int posx = DoubleClamp(pt.x, rc.left, rc.right, rwidth) - state.mdipt.x;
      int posy = DoubleClamp(pt.y, rc.top, rc.bottom, rheight) - state.mdipt.y;

      MoveWindowAsync(state.hwnd, posx, posy, rwidth, rheight);
    }
    if (state.hittest == 2 && button == BT_LMB) return 0;
    state.blockmouseup = 1;
  } else {
    SClickActions(state.hwnd, action);
    state.blockmouseup = 1;  // because the action is done
  }
  // AN ACTION HAS BEEN DONE!!!

  // Remember time, position and button of this click
  // so we can check for double-click
  state.clicktime = GetTickCount();
  state.clickpt = pt;
  state.clickbutton = button;

  // Prevent mousedown from propagating
  return 1;
}

/////////////////////////////////////////////////////////////////////////////
// Called on MouseUp and on AltUp when using GrabWithAlt
static DWORD WINAPI FinishMovementNow(LPVOID pp) {
  LOG("FinishMovement");
  // LogState("FinishMovement\n");
  StopSpeedMes();
  ShowSnapLayoutPreview(ZONES_PREV_HIDE);
  // Sleep(5000);
  if (!conf.FullWin && LastWin.hwnd && state.moving == 1) {
    if (state.action.ac == AC_RESIZE) {
      ResizeAllSnappedWindows();
    }
    if (IsWindow(LastWin.hwnd) && !LastWin.snap) {
      if (LastWin.maximize) {
        MaximizeRestore_atpt(LastWin.hwnd, SW_MAXIMIZE, 1);
        LastWin.hwnd = nullptr;
      } else {
        LastWin.end = 1;
        MoveWindowNow(&LastWin);
      }
    }
  }

  // Clear restore data if needed
  unsigned rdata_flag = GetRestoreFlag(state.hwnd);
  if (rdata_flag & SNCLEAR) {
    ClearRestoreData(state.hwnd);
  }

  // Auto Remaximize if option enabled and conditions are met.
  if (conf.AutoRemaximize && state.moving &&
      (state.origin.maximized || state.origin.fullscreen) && !state.shift &&
      !state.mdiclient && state.action.ac == AC_MOVE) {
    state.action = k_action_none;
    HMONITOR monitor = MonitorFromPoint(state.prevpt, MONITOR_DEFAULTTONEAREST);
    if (monitor != state.origin.monitor) {
      if (state.origin.maximized) {
        MaximizeRestore_atpt(state.hwnd, SW_MAXIMIZE, 0);
      }
      if (state.origin.fullscreen) {
        MaximizeRestore_atpt(state.hwnd, SW_FULLSCREEN, 0);
      }
    }
  }

  HideTransWin();

  // Send WM_EXITSIZEMOVE and EVENT_SYSTEM_MOVESIZEEND
  if (conf.FullWin && state.moving == 1) NotifySizeMoveStaEnd(state.hwnd, 0);

  state.action = k_action_none;
  state.moving = 0;
  state.snap = conf.AutoSnap;
  state.cached_hwnd_blacklist = nullptr;

  // Unhook mouse if Alt is released
  if (!state.alt) {
    UnhookMouse();
  } else {
    // Just hide g_mainhwnd
    HideCursor();
  }
  g_InFinishMovement = 0;
  return 1;
}
void FinishMovementAsync() {
  g_InFinishMovement = 1;
  PostThreadMessage(g_WorkerThreadID, WM_DOWORK,
                    reinterpret_cast<WPARAM>(FinishMovementNow), 0);
}

/////////////////////////////////////////////////////////////////////////////
// state.action is the current action
// TODO: Generalize click combo...
void LockMovement() {
  state.moving = CURSOR_ONLY;
  LastWin.hwnd = nullptr;
  if (!conf.FullWin) HideTransWin();
}

}  // namespace altsnap::hooks
