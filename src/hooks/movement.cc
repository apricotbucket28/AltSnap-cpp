// movement (split out of hooks.cc).

#include "hooks/movement.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "common/ini_section.h"
#include "hooks/actions/click_actions.h"
#include "hooks/actions/window_actions.h"
#include "hooks/blacklist.h"
#include "hooks/hooks_internal.h"
#include "hooks/input_state.h"
#include "hooks/mouse_hook.h"
#include "hooks/overlay/trans_window.h"
#include "hooks/overlay/window_menu.h"
#include "hooks/send_input.h"
#include "hooks/window/snap.h"
#include "hooks/window/window_info.h"
#include "hooks/window/window_move.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

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

namespace {

// The window to act on: the given one, or the one under the cursor (the MDI
// child if it is inside an MDI client). Returns false if there is none.
bool PickTargetWindow(POINT pt, HWND hwnd) {
  state.mdiclient = nullptr;
  state.hwnd = hwnd ? hwnd : MDIorNOT(WindowFromPoint(pt), &state.mdiclient);
  if (!state.hwnd || (g_InMouseMove && state.hwnd == LastWin.hwnd) ||
      !IsWindow(state.hwnd)) {
    LOG("Init failed: state.hwnd=%x, LastWin.hwnd=%x, g_InMouseMove = %d",
        static_cast<UINT>(reinterpret_cast<INT_PTR>(state.hwnd)),
        static_cast<UINT>(reinterpret_cast<INT_PTR>(LastWin.hwnd)),
        static_cast<int>(g_InMouseMove));
    return false;
  }
  return true;
}

// Whether AltSnap must leave this window alone: blacklisted, our own pin
// window, a window we cannot query, or a maximized/fullscreen one the user
// excluded. Also records whether the window is maximized or fullscreen.
// The checks are ordered: IsBlacklisted() caches the window's names first.
bool IsExcludedWindow(int button, MONITORINFO& mi, WINDOWPLACEMENT& wndpl,
                      RECT& wnd) {
  if (IsBlacklisted(state.hwnd, BlkLst.Processes)) return true;
  if (!lstrcmp(state.classname, APP_NAME L"-Pin")) return true;
  // The Windows blacklist does not apply in the titlebar, nor to the wheel.
  if (IsBlacklisted(state.hwnd, BlkLst.Windows) && !state.hittest &&
      button < BT_WHEELD) {
    return true;
  }
  if (GetWindowPlacement(state.hwnd, &wndpl) == 0) return true;
  if (GetWindowRect(state.hwnd, &wnd) == 0) return true;
  state.origin.maximized = IsZoomed(state.hwnd);
  if (state.origin.maximized && conf.BLMaximized) return true;
  state.origin.fullscreen = IsFullscreenF(state.hwnd, &wnd, &mi.rcMonitor);
  return state.origin.fullscreen && !conf.FullScreen;
}

// Starts a move or a resize of state.hwnd. Returns what the mouse hook must do:
// 1 = block the mouse button, 0 = pass it on.
int StartMoveOrResize(POINT pt, action_t action, RECT& wnd, int button) {
  state.sactiondone = action;
  if (GetProp(state.hwnd, kPropMoveOnOff)) {
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
    (void)PostWorkerTask([] { SendAltCtrlAlt(nullptr); });
  }

  // Set action state.
  state.action = action;  // MOVE OR RESIZE
  state.moving = 0;
  // Whether or not we will use the zones
  state.usezones = ((conf.UseZones & 9) == 9) ^ state.shift;

  state.enumed = 0;  // Reset enum stuff
  StartSpeedMes();   // Speed timer

  int ret;
  if (state.action.ac == AC_MOVE) {
    ret = ActionMove(pt, button);
  } else {
    ret = ActionResize(pt, &wnd, button);
  }
  if (ret == 1) return 1;  // block mouse down!
  if (ret == 0) return 0;  // Next hook!
  // else ret == -1: the action started.
  UpdateCursor(pt);
  SetWindowTrans(state.hwnd);

  LastWin.start = 1;
  return -1;
}

// Restores the size a snapped window had before it was snapped.
// Returns 0 to pass the click on, 1 to block it.
int RestoreSnappedWindow(POINT pt, int button) {
  int rwidth = 0, rheight = 0;
  if (GetRestoreData(state.hwnd, &rwidth, &rheight) & SNAPPED) {
    ClearRestoreData(state.hwnd);
    RECT rc;
    GetWindowRect(state.hwnd, &rc);
    const int posx =
        DoubleClamp(pt.x, rc.left, rc.right, rwidth) - state.mdipt.x;
    const int posy =
        DoubleClamp(pt.y, rc.top, rc.bottom, rheight) - state.mdipt.y;
    MoveWindowAsync(state.hwnd, posx, posy, rwidth, rheight);
  }
  if (state.hittest == 2 && button == BT_LMB) return 0;
  state.blockmouseup = 1;
  return 1;
}

}  // namespace

// If we pass buttonX BT_PROBE it will tell us if we pass the blacklist.
// Upstream: init_movement_and_actions
int InitMovementAndActions(POINT pt, HWND hwnd, action_t action, int buttonX) {
  LOG("\ninit_movement_and_actions(pt=%d,%d, hwnd=%x, action=%d, button=%d)",
      static_cast<int>(pt.x), static_cast<int>(pt.y),
      static_cast<UINT>(reinterpret_cast<INT_PTR>(hwnd)),
      static_cast<int>(action.ac), buttonX);
  state.prevpt = pt;  // in case
  const int button = LOWORD(buttonX);
  const UCHAR probemode = HIWORD(buttonX);  // (truncated, as before)

  // Make sure nothing is in the way
  HideCursor();
  state.sclickhwnd = nullptr;
  KillAltSnapMenu();

  if (!PickTargetWindow(pt, hwnd)) return 0;

  // Get monitor info
  HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  GetMonitorInfo(monitor, &mi);
  CopyRect(&state.origin.mon, &mi.rcWork);

  // state.mdipt HAS to be set to Zero for ClientToScreen adds the offset
  state.mdipt.x = state.mdipt.y = 0;
  if (state.mdiclient) {
    GetMDInfo(&state.mdipt, &mi.rcMonitor);
    CopyRect(&state.origin.mon, &mi.rcMonitor);
  }

  WINDOWPLACEMENT wndpl;
  wndpl.length = sizeof(wndpl);
  RECT wnd;
  if (IsExcludedWindow(button, mi, wndpl, wnd)) return 0;

  // If no action is to be done then we passed all blacklists
  if (probemode || action.ac == AC_NONE) return 1;

  // Set state
  state.blockaltup = state.alt;  // If alt is down...
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

  // Do things depending on what button was pressed
  if (IsMovementAction(action)) {
    const int ret = StartMoveOrResize(pt, action, wnd, button);
    if (ret >= 0) return ret;
  } else if (BT_WHEELD <= button && button <= BT_HWHEELU) {
    // Wheel actions, directly return here
    // because maybe the action will not be done
    if (GetProp(state.hwnd, kPropMoveOnOff)) {
      state.action = k_action_none;
      return 0;  // Wheel was disabled for this window.
    }
    return DoWheelActions(state.hwnd, action);
  } else if (action.ac == AC_RESTORE) {
    if (RestoreSnappedWindow(pt, button) == 0) return 0;
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
        MaximizeRestoreAtPoint(LastWin.hwnd, SW_MAXIMIZE, 1);
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
        MaximizeRestoreAtPoint(state.hwnd, SW_MAXIMIZE, 0);
      }
      if (state.origin.fullscreen) {
        MaximizeRestoreAtPoint(state.hwnd, SW_FULLSCREEN, 0);
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
  if (!PostWorkerTask([] { FinishMovementNow(nullptr); }))
    g_InFinishMovement = 0;
}

// state.action is the current action
// TODO: Generalize click combo...
void LockMovement() {
  state.moving = CURSOR_ONLY;
  LastWin.hwnd = nullptr;
  if (!conf.FullWin) HideTransWin();
}

}  // namespace altsnap::hooks
