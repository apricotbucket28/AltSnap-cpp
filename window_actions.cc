// window actions (split out of hooks.cc).

#include "window_actions.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "blacklist.h"
#include "click_actions.h"
#include "hooks_internal.h"
#include "ini_section.h"
#include "input_state.h"
#include "mouse_move.h"
#include "movement.h"
#include "pin_window.h"
#include "send_input.h"
#include "snap.h"
#include "snap_move.h"
#include "volume.h"
#include "window_enum.h"
#include "window_info.h"
#include "window_move.h"
#include "zones.h"

namespace altsnap::hooks {

///////////////////////////////////////////////////////////////////////////
int ActionPause(HWND hwnd, UCHAR pause) {
  //    LOGA("Entering pause/resume %d", (int)pause);
  if (!blacklisted(hwnd, BlkLst.Pause)) {
    wchar_t tmpname[256];
    DWORD pid = GetWindowProgName(hwnd, tmpname, std::size(tmpname));

    constexpr DWORD kAccessLevel1 =
        STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0xfff;
    constexpr DWORD kAccessLevel2 =
        STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0xffff;
    HANDLE ProcessHandle = OpenProcess(kAccessLevel2, FALSE, pid);
    if (!ProcessHandle) ProcessHandle = OpenProcess(kAccessLevel1, FALSE, pid);
    if (!ProcessHandle)
      ProcessHandle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);

    if (ProcessHandle) {
      //            LOGA("Going to pause/resume %d", (int)pause);
      if (pause)
        NtSuspendProcess(ProcessHandle);
      else
        NtResumeProcess(ProcessHandle);

      CloseHandle(ProcessHandle);
      //            LOGA("Done");
      return 1;
    }
  }
  return 0;
}
///////////////////////////////////////////////////////////////////////////
// Kill the process from hwnd
static DWORD WINAPI ActionKillThread(LPVOID hwnd) {
  if (state.shift) {
    // TODO: Aggressive command such as:
    // taskkill.exe /F /FI "status eq NOT RESPONDING" /FI "IMAGENAME ne
    // AltSnap.exe" /FI "IMAGENAME ne dwm.exe"
  } else {
    wchar_t tmpname[256];
    DWORD pid =
        GetWindowProgName(static_cast<HWND>(hwnd), tmpname, std::size(tmpname));
    // LOG("pid=%lu", pid);

    // Open the process
    HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    // LOG("proc=%lx", (DWORD)proc);
    if (proc) {
      TerminateProcess(proc, 1);
      CloseHandle(proc);
    }
    return 1;
  }
  return 0;
}
int ActionKill(HWND hwnd) {
  // LOG("hwnd=%lx",(DWORD) hwnd);
  if (!hwnd) return 0;

  if (isClassName(hwnd, L"Ghost")) {
    HWND tmp = HungWindowFromGhostWindow(hwnd);
    if (tmp) {
      hwnd = tmp;
    } else {
      // Unable to get hung window from ghost
      // Just send the close system command...
      PostMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
      return 1;
    }
  }

  if (blacklisted(hwnd, BlkLst.Pause)) return 0;

  PostThreadMessage(g_WorkerThreadID, WM_DOWORK,
                    reinterpret_cast<WPARAM>(ActionKillThread),
                    reinterpret_cast<LPARAM>(hwnd));

  return 1;
}
// Try harder to actually set the window foreground.
void ReallySetForegroundWindow(HWND hwnd) {
  if (!hwnd) return;
  // Check existing foreground Window.
  HWND fore = GetForegroundWindow();
  if (fore != hwnd) {
    if (state.alt != VK_MENU && state.alt != VK_CONTROL &&
        !(GetKeyState(VK_CONTROL) & 0x8000) &&
        !(GetKeyState(VK_MENU) & 0x8000)) {
      // If the physical Alt or Ctrl keys are not down
      // We need to activate the window with key input.
      // CTRL seems to work. Also Alt works but trigers the menu
      // So it is simpler to stick to CTRL.
      Send_KEY(VK_CONTROL);
    }
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
  }
}
void SetForegroundWindowL(HWND hwnd) {
  if (!state.mdiclient) {
    ReallySetForegroundWindow(hwnd);
  } else {
    ReallySetForegroundWindow(state.mdiclient);
    PostMessage(state.mdiclient, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(hwnd),
                0);
  }
}
void ToggleSnapState() {
  state.snap = state.snap ? 0 : 3;
}
/////////////////////////////////////////////////////////////////////////////
// 1.44
int ScrollPointedWindow(POINT pt, int delta, WPARAM wParam) {
  // Get window and foreground window
  HWND hwnd = WindowFromPoint(pt);
  if (!hwnd || blacklisted(hwnd, BlkLst.IScroll)) return 0;

  HWND foreground = GetForegroundWindow();

  // Return if foreground window is blacklisted
  if (foreground && blacklisted(foreground, BlkLst.Windows)) return 0;

  // If it's a groupbox, grab the real window
  LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  if ((style & BS_GROUPBOX) && isClassName(hwnd, L"Button")) {
    HWND groupbox = hwnd;
    EnableWindow(groupbox, FALSE);
    hwnd = WindowFromPoint(pt);
    EnableWindow(groupbox, TRUE);
    if (!hwnd) return 0;
  }

  // Get wheel info
  WPARAM wp = delta << 16;
  LPARAM lp = MAKELPARAM(pt.x, pt.y);

  // Change WM_MOUSEWHEEL to WM_MOUSEHWHEEL if shift is being depressed
  // Introduced in Vista and far from all programs have implemented it.
  if (wParam == WM_MOUSEWHEEL && IsAKeyDown(conf.HScrollKey)) {
    wParam = WM_MOUSEHWHEEL;
    wp = -wp;  // Up is left, down is right
  }

  // Add button information since we don't get it with the hook
  static const UCHAR toOr[] = {
      VK_LBUTTON,   // MK_LBUTTON  }, 1
      VK_RBUTTON,   // MK_RBUTTON  }, 2
      VK_CONTROL,   // MK_CONTROL  }, 4
      VK_SHIFT,     // MK_SHIFT    }, 8
      VK_MBUTTON,   // MK_MBUTTON  }, 16
      VK_XBUTTON1,  // MK_XBUTTON1 }, 32
      VK_XBUTTON2,  // MK_XBUTTON2 }  64
  };
  for (size_t i = 0; i < std::size(toOr); i++)  // Should we use GetKeyState?
    if (GetAsyncKeyState(toOr[i]) & 0x8000) wp |= (1 << i);

  // Forward scroll message
  SendMessage(hwnd, wParam, wp, lp);
  // Simulate small steps wheel (can be used to debug other programs)
  // SendMessage(hwnd, WM_MOUSEWHEEL, (state.shift?(delta/7):delta)<<16, lp);

  // Block original scroll event
  return 1;
}
/////////////////////////////////////////////////////////////////////////////
int ActionAltTab(POINT pt, short delta, short laser, WNDENUMPROC lpEnumFunc) {
  hwnds.clear();

  if (conf.MDI) {
    // Get Class name
    HWND hwnd = WindowFromPoint(pt);
    if (!hwnd) return 0;

    // Get MDIClient
    HWND mdiclient = nullptr;
    if (isClassName(hwnd, L"MDIClient")) {
      mdiclient = hwnd;  // we are pointing to the MDI client!
    } else {
      MDIorNOT(hwnd, &mdiclient);  // Get mdiclient from hwnd
    }
    // Enumerate and then reorder MDI windows
    if (mdiclient) {
      EnumChildWindows(mdiclient, lpEnumFunc, laser);

      if (hwnds.size() > 1) {
        if (delta > 0) {
          PostMessage(mdiclient, WM_MDIACTIVATE,
                      reinterpret_cast<WPARAM>(hwnds[hwnds.size() - 1]), 0);
        } else {
          SetWindowLevel(hwnds[0], hwnds[hwnds.size() - 1]);
          PostMessage(mdiclient, WM_MDIACTIVATE,
                      reinterpret_cast<WPARAM>(hwnds[1]), 0);
        }
      }
    }
  }  // End if MDI

  // Enumerate windows
  if (hwnds.size() <= 1) {
    state.origin.monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    hwnds.clear();
    EnumDesktopWindows(nullptr, lpEnumFunc, laser);
    if (hwnds.size() <= 1) {
      return 0;
    }

    // Reorder windows
    if (delta > 0) {
      ReallySetForegroundWindow(hwnds[hwnds.size() - 1]);
    } else {
      SetWindowLevel(hwnds[0], hwnds[hwnds.size() - 1]);
      ReallySetForegroundWindow(hwnds[1]);
    }
  }
  return 1;
}

/////////////////////////////////////////////////////////////////////////////
// Windows 2000+ Only
int ActionTransparency(HWND hwnd, short delta) {
  if (blacklisted(hwnd, BlkLst.Windows)) return 0;  // Spetial
  if (state.action.ac && MOUVEMENT(state.action))
    SetWindowTrans(reinterpret_cast<HWND>(-1));

  static int alpha = 255;
  short pre_delta = (state.shift) ? conf.AlphaDeltaShift : conf.AlphaDelta;
  int alpha_delta = ScaleDeltaAndAccum(delta, pre_delta);
  if (!alpha_delta) return 1;

  BYTE old_alpha = 0;
  if (GetLayeredWindowAttributes(hwnd, nullptr, &old_alpha, nullptr)) {
    alpha = old_alpha;  // If possible start from the current aplha.
  }

  alpha = CLAMP(conf.MinAlpha, alpha + alpha_delta, 255);  // Limit alpha

  SetWindowAlpha(hwnd, alpha);
  return 1;
}

void ActionTransparency2(HWND hwnd, action_t action) {
  // LOGA("Transparency_%d_%d", (int)action.fl, (int)action.wp);
  int alpha = 255;
  BYTE old_alpha = 255;

  if (action.fl == ACFL_SET)
    alpha = action.wp;
  else if (GetLayeredWindowAttributes(hwnd, nullptr, &old_alpha, nullptr))
    alpha = old_alpha;

  if (action.fl == ACFL_UP)  // 2=>TOP/UP
    alpha += action.wp;
  else if (action.fl == ACFL_DOWN)  // 4=>BOTTOM/Down
    alpha -= action.wp;
  else if (action.fl == ACFL_TOGGLE)
    alpha = old_alpha == 255 ? action.wp : 255;

  alpha = CLAMP(conf.MinAlpha, alpha, 255);  // Limit alpha

  SetWindowAlpha(hwnd, static_cast<BYTE>(alpha));
}

static void SetBottomMost(HWND hwnd) {
  HWND lowhwnd = HWND_BOTTOM;  // Lowest hwnd to consider.

  if (!blacklisted(hwnd, BlkLst.Bottommost)) {
    // Ignore bottommost list if the lowered windows is also part of it.
    HWND tmph = hwnd;
    int i = 0;
    while ((tmph = GetNextWindow(tmph, GW_HWNDNEXT)) && i++ < 1024) {
      if (IsVisible(tmph) && !IsIconic(tmph) &&
          blacklisted(tmph, BlkLst.Bottommost)) {
        lowhwnd = GetNextWindow(tmph, GW_HWNDPREV);
        break;
      }
    }
  }
  if (lowhwnd) SetWindowLevel(hwnd, lowhwnd);
}
void ActionLower(HWND hwnd, short delta, UCHAR shift, UCHAR fg) {
  if (delta > 0) {
    if (shift) {
      ToggleMaxRestore(hwnd);
    } else {
      if (conf.AutoFocus || fg) SetForegroundWindowL(hwnd);
      SetWindowLevel(hwnd, HWND_TOPMOST);
      SetWindowLevel(hwnd, HWND_NOTOPMOST);
    }
  } else if (delta == 0 && (IsCtrlDown() || IsAeraCapbutton(state.hittest))) {
    // turn lower in *Always on top* if Ctrl or [_][O][X]
    TogglesAlwaysOnTop(hwnd);
  } else {
    if (shift) {
      MinimizeWindow(hwnd);
    } else {
      if (hwnd == GetAncestor(GetForegroundWindow(), GA_ROOT)) {
        HWND tmp = GetWindow(hwnd, GW_HWNDNEXT);
        if (!tmp) tmp = GetWindow(hwnd, GW_HWNDPREV);
        if (tmp && hwnd != GetAncestor(tmp, GA_ROOT)) SetForegroundWindowL(tmp);
      }
      // Takes bottommost blacklist into account.
      SetBottomMost(hwnd);
    }
  }
}
/////////////////////////////////////////////////////////////////////////////
void ActionMaxRestMin(HWND hwnd, int delta) {
  int maximized = IsZoomed(hwnd);
  if (state.shift) {
    ActionLower(hwnd, delta, 0, IsCtrlDown());
    return;
  }

  if (delta > 0) {
    if (!maximized && IsResizable(hwnd)) MaximizeWindow(hwnd);
  } else {
    if (maximized)
      RestoreWindow(hwnd);
    else
      MinimizeWindow(hwnd);
  }
  if (conf.AutoFocus) SetForegroundWindowL(hwnd);
}

/////////////////////////////////////////////////////////////////////////////
// Adjust brightness
// Only works for desktop monitors that support DDC/CI, and is disabled for
// now. It is kept compiled (not #if'd out) so it cannot silently rot.
inline constexpr bool kBrightnessControlEnabled = false;

void ActionBrightness(const POINT pt, const short delta) {
  if (!kBrightnessControlEnabled) return;

  const HMONITOR hmon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
  DWORD count = 0;
  if (!hmon || !GetNumberOfPhysicalMonitorsFromHMONITOR(hmon, &count) ||
      count == 0) {
    LOG("GetNumberOfPhysicalMonitorsFromHMONITOR failed");
    return;
  }

  std::vector<PHYSICAL_MONITOR> physical_monitors(count);
  if (!GetPhysicalMonitorsFromHMONITOR(hmon, count, physical_monitors.data())) {
    LOG("Unable to get physical monitors");
    return;
  }

  const HANDLE monitor = physical_monitors[0].hPhysicalMonitor;
  DWORD capabilities = 0, color_temperatures = 0;
  if (GetMonitorCapabilities(monitor, &capabilities, &color_temperatures) &&
      (capabilities & MC_CAPS_BRIGHTNESS)) {
    DWORD lo = 0, cur = 0, hi = 0;
    if (GetMonitorBrightness(monitor, &lo, &cur, &hi)) {
      const int step = std::max<int>(1, (hi - lo) / 20);
      const int wanted = static_cast<int>(cur) + (delta > 0 ? step : -step);
      SetMonitorBrightness(monitor,
                           static_cast<DWORD>(std::clamp<int>(wanted, lo, hi)));
    }
  }
  DestroyPhysicalMonitors(count, physical_monitors.data());
}
/////////////////////////////////////////////////////////////////////////////
// Roll/Unroll Window. If delta > 0: Roll if < 0: Unroll if == 0: Toggle.
void RollWindow(HWND hwnd, int delta) {
  // if the window is maximized do a spetial
  // treatement with no restore flags
  RECT rc;
  GetWindowRect(hwnd, &rc);

  if (IsZoomed(hwnd)) {
    int ismxrolled = IsMXRolled(hwnd, &rc);
    if (delta <= 0 && ismxrolled) {
      // Unroll Maximized window
      WINDOWPLACEMENT wndpl;
      wndpl.length = sizeof(wndpl);
      GetWindowPlacement(hwnd, &wndpl);
      wndpl.showCmd = SW_SHOWMINIMIZED;
      SetWindowPlacement(hwnd, &wndpl);
      wndpl.showCmd = SW_SHOWMAXIMIZED;
      SetWindowPlacement(hwnd, &wndpl);
    } else if (delta >= 0 && !ismxrolled) {
      // Roll maximized window
      SetWindowPos(
          hwnd, nullptr, 0, 0, rc.right - rc.left, GetSystemMetrics(SM_CYMIN),
          SWP_NOMOVE | SWP_NOZORDER | SWP_NOSENDCHANGING | SWP_ASYNCWINDOWPOS);
    }
    return;
  }
  // Handle non maximized windows
  unsigned restore = GetRestoreFlag(hwnd);

  if (restore & ROLLED && delta <= 0) {  // UNROLL
    // Restore the Old height
    // Set origin width and height to the saved values
    int width = 0, height = 0;
    GetRestoreData(hwnd, &width, &height);
    width = rc.right - rc.left;  // keep current width
    ClearRestoreData(hwnd);
    SetWindowPos(
        hwnd, nullptr, 0, 0, width, height,
        SWP_NOSENDCHANGING | SWP_NOZORDER | SWP_NOMOVE | SWP_ASYNCWINDOWPOS);

  } else if (((!(restore & ROLLED) && delta == 0)) || delta > 0) {  // ROLL
    if (!(restore & ROLLED)) {  // Save window size if not saved already.
      SetRestoreData(hwnd, rc.right - rc.left, rc.bottom - rc.top, 0);
      // Add the SNAPPED falg is maximized and and add the SNTHENROLLED flag is
      // snapped
      SetRestoreFlag(hwnd, ROLLED | IsWindowSnapped(hwnd) << 10);
    }
    SetWindowPos(
        hwnd, nullptr, 0, 0, rc.right - rc.left, GetSystemMetrics(SM_CYMIN),
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOSENDCHANGING | SWP_ASYNCWINDOWPOS);
  }
}

int ActionZoom(HWND hwnd, short delta, short center) {
  if (!IsResizable(hwnd)) return 0;

  RECT rc, orc;
  GetWindowRect(hwnd, &rc);
  if (IsZoomed(hwnd)) RestoreToMonitorSize(hwnd, &rc);
  ClearRestoreFlagOnResizeIfNeeded(hwnd);
  SetEdgeAndOffset(&rc, state.prevpt);
  OffsetRectMDI(&rc);
  CopyRect(&orc, &rc);

  int left = 0, right = 0, top = 0, bottom = 0;
  int div = state.shift ? conf.ZoomFracShift : conf.ZoomFrac;
  // We increase/decrease at least 1 pixel or SnapThreshold/2.
  UCHAR T = state.shift ? 1 : conf.SnapThreshold / 2 + 1;
  UCHAR CT = !center ^ !IsCtrlDown() ||
             (state.resize.x == RZ_XCENTER && state.resize.y == RZ_YCENTER);

  if (!conf.AutoSnap ||
      (CT &&
       (state.resize.x == RZ_XCENTER ||
        state.resize.y == RZ_YCENTER))) {  // Try to conserve a bit better the
                                           // aspect ratio when in center mode.
    T = 1;                                 // Or when no snapping has to occur.
  }

  rgMMI_t mmi;
  GetMinMaxInfo(hwnd, &mmi.Min, &mmi.Max);  // for CLAMPH/W functions

  if (state.resize.x == RZ_LEFT) {
    right = std::max<int>(T, (rc.right - rc.left) / div);
    state.resize.x = RZ_RIGHT;
  } else if (state.resize.x == RZ_RIGHT) {
    left = std::max<int>(T, (rc.right - rc.left) / div);
    state.resize.x = RZ_LEFT;
  } else if (state.resize.x == RZ_XCENTER && CT) {
    left = std::max<int>(T, (state.prevpt.x - rc.left) / div);
    right = std::max<int>(T, (rc.right - state.prevpt.x) / div);
  }

  if (state.resize.y == RZ_TOP) {
    bottom = std::max<int>(T, (rc.bottom - rc.top) / div);
    state.resize.y = RZ_BOTTOM;
  } else if (state.resize.y == RZ_BOTTOM) {
    top = std::max<int>(T, (rc.bottom - rc.top) / div);
    state.resize.y = RZ_TOP;
  } else if (state.resize.y == RZ_YCENTER && CT) {
    top = std::max<int>(T, (state.prevpt.y - rc.top) / div);
    bottom = std::max<int>(T, (rc.bottom - state.prevpt.y) / div);
  }
  if (delta < 0) {
    // Zoom out
    rc.left += left;
    rc.right -= right;
    rc.top += top;
    rc.bottom -= bottom;
  } else {
    // Zoom in
    rc.left -= left;
    rc.right += right;
    rc.top -= top;
    rc.bottom += bottom;
  }

  int x = rc.left, y = rc.top, width = rc.right - rc.left,
      height = rc.bottom - rc.top;

  state.hwnd = hwnd;
  state.snap = conf.AutoSnap;  // Only use autosnap setting.

  ResizeSnap(&x, &y, &width, &height,
             std::min<UCHAR>(std::max(left, right) - 1,
                             conf.SnapThreshold)  // initial x threshold
             ,
             std::min<UCHAR>(std::max(top, bottom) - 1,
                             conf.SnapThreshold));  // initial y threshold
  // Make sure that the windows does not move
  // in case it is resized from bottom/right
  if (state.resize.x == RZ_LEFT) x = x + width - CLAMPWL(width, &mmi);
  if (state.resize.y == RZ_TOP) y = y + height - CLAMPHL(height, &mmi);
  // Avoid runaway effect when zooming in/out too much.
  if (state.resize.x == RZ_XCENTER && !ISCLAMPEDWL(width, &mmi)) x = orc.left;
  if (state.resize.y == RZ_YCENTER && !ISCLAMPEDHL(height, &mmi)) y = orc.top;
  width = CLAMPWL(width, &mmi);  // Double check
  height = CLAMPHL(height, &mmi);

  MoveWindowAsync(hwnd, x, y, width, height);
  return 1;
}
/////////////////////////////////////////////////////////////////////////////
int ActionMove(POINT pt, int button) {
  // If this is a double-click
  if (IsDoubleClick(button)) {
    SetOriginFromRestoreData(state.hwnd, k_action_move);
    LastWin.hwnd = nullptr;

    action_t acdbclick = GetActionMR(button);
    if (acdbclick.ac > AC_RESIZE) {
      // Use the Mouve button + While Moving action as doubleclick action.
      SClickActions(state.hwnd, acdbclick);
    } else {
      // Defult action for double move click
      if (state.shift) {
        RollWindow(state.hwnd, 0);  // Roll/Unroll Window...
      } else if (IsCtrlDown()) {
        MinimizeWindow(state.hwnd);
      } else if (state.resizable) {
        // Toggle Maximize window
        ToggleMaxRestore(state.hwnd);
      }
    }
    state.action = k_action_none;  // Stop move action
    state.clicktime = 0;           // Reset double-click time
    state.blockmouseup =
        1;  // Block the mouseup, otherwise it can trigger a context menu
    // Prevent mousedown from propagating
    return 1;
  }
  if (conf.MMMaximize & 2) {
    MouseMove(pt);  // Restore with simple Click
  } else if (conf.DragThreshold >= 2 ||
             (conf.DragThreshold == 1 && IsAnySnapped(state.hwnd))) {
    // Wait some move threshold before drag...
    // If the Window was maximized or if the user enabled the option.
    state.moving = DRAG_WAIT;
  }

  return -1;
}

/////////////////////////////////////////////////////////////////////////////
int ActionResize(POINT pt, const RECT* wnd, int button) {
  if (!state.resizable) {
    state.action = k_action_none;
    return 0;  // Next Hook
  }
  // Aero-move this window if this is a double-click
  if (IsDoubleClick(button)) {
    action_t acdbclick = GetActionMR(button);
    if (acdbclick.ac > AC_RESIZE) {
      // Use the Mouve button + While Moving action as doubleclick action.
      SClickActions(state.hwnd, acdbclick);
    } else {
      // Use SnapToCorner by default (legacy)
      SnapToCorner(state.hwnd, AUTORESIZE,
                   !state.shift ^ !(conf.AeroTopMaximizes & 2));
    }
    state.action = k_action_none;  // Stop resize action
    state.blockmouseup = 1;        // Block mouse up (context menu would pop)
    state.clicktime = 0;           // Reset double-click time
    // Prevent mousedown from propagating
    return 1;
  }
  SetEdgeAndOffset(wnd, pt);
  if (state.resize.y == RZ_YCENTER && state.resize.x == RZ_XCENTER) {
    if (conf.ResizeCenter == 0) {
      // Use Bottom Right Mode
      state.resize.x = RZ_RIGHT;
      state.resize.y = RZ_BOTTOM;
      state.offset.y = wnd->bottom - pt.y;
      state.offset.x = wnd->right - pt.x;
    } else if (conf.ResizeCenter == 2) {
      // Switch to Move action
      state.action.ac = AC_MOVE;
    } else if (conf.ResizeCenter == 3) {
      // Use diagonals to select pure L/C R/C T/C B/C
      SetEdgeToClosestSide(wnd, pt);
    }
  }
  if (conf.DragThreshold >= 3) {
    state.moving = DRAG_WAIT;
  }
  return -1;
}
/////////////////////////////////////////////////////////////////////////////
void ActionBorderless(HWND hwnd) {
  LONG_PTR style = 0;
  // Get anc clear eventual old style.
  LONG_PTR ostyle = ClearBorderlessFlag(hwnd);
  if (ostyle) {
    // Restore old style if found
    style = ostyle;
  } else {
    style = GetWindowLongPtr(hwnd, GWL_STYLE);
    if (!style) return;
    SetBorderlessFlag(hwnd, style);  // Save style
    if ((style & WS_CAPTION) == WS_CAPTION || style & WS_THICKFRAME) {
      style &= state.shift ? ~WS_CAPTION : ~(WS_CAPTION | WS_THICKFRAME);
    } else {
      style |= WS_OVERLAPPEDWINDOW;
    }
  }
  SetWindowLongPtr(hwnd, GWL_STYLE, style);

  // Under Windows 10, with DWM we HAVE to resize the windows twice
  // to have proper drawing. this is a bug.
  RECT rc;
  GetWindowRect(hwnd, &rc);
  SetWindowPos(
      hwnd, nullptr, rc.left, rc.top, rc.right - rc.left,
      rc.bottom - rc.top + 1,
      SWP_ASYNCWINDOWPOS | SWP_NOMOVE | SWP_FRAMECHANGED | SWP_NOZORDER);
  SetWindowPos(hwnd, nullptr, rc.left, rc.top, rc.right - rc.left,
               rc.bottom - rc.top,
               SWP_ASYNCWINDOWPOS | SWP_NOMOVE | SWP_NOZORDER);
}
void CenterWindow(HWND hwnd, unsigned flags, int full_monitor) {
  RECT mon;  // = state.origin.mon;
  POINT pt;
  int width = 0, height = 0;
  if (flags & CW_RESTORE) {
    SetOriginFromRestoreData(hwnd, k_action_move);
    width = state.origin.width;
    height = state.origin.height;
  } else {
    RECT rc;
    GetWindowRect(hwnd, &rc);
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
  }
  GetCursorPos(&pt);
  GetMonitorRect(&pt, full_monitor, &mon);

  RECT bd;
  FixDWMRectLL(hwnd, &bd, 0);

  // extra calculation to centre the portion that is inside the invisible
  // borders
  int x = mon.left +
          ((mon.right - mon.left) - (width - bd.left - bd.right)) / 2 - bd.left;
  int y = mon.top +
          ((mon.bottom - mon.top) - (height - bd.top - bd.bottom)) / 2 - bd.top;

  if (flags & CW_TRIM) {
    // Trim the window to the current monitor
    if (x < mon.left - bd.left) {
      x = mon.left - bd.left;
      width = mon.right - mon.left + bd.left + bd.right;
    }

    if (y < mon.top - bd.top) {
      y = mon.top - bd.top;
      height = mon.bottom - mon.top + bd.top + bd.bottom;
    }
  }
  MoveWindowAsync(hwnd, x, y, width, height);
}
/////////////////////////////////////////////////////////////////////////////
void ActionMaximize(HWND hwnd) {
  LastWin.hwnd = nullptr;
  if (state.shift) {
    MinimizeWindow(hwnd);
  } else if (IsResizable(hwnd)) {
    ToggleMaxRestore(hwnd);
  }
}
/////////////////////////////////////////////////////////////////////////////
void MaximizeHV(HWND hwnd, int horizontal) {
  RECT rc, bd, mon;
  if (!IsResizable(hwnd) || !GetWindowRect(hwnd, &rc)) return;
  OffsetRectMDI(&rc);

  POINT pt;
  GetCursorPos(&pt);
  GetMonitorRect(&pt, 0, &mon);
  SetOriginFromRestoreData(hwnd, k_action_move);

  SetRestoreData(hwnd, state.origin.width, state.origin.height, SNAPPED);
  FixDWMRect(hwnd, &bd);
  if (horizontal) {
    SetRestoreFlag(hwnd, SNAPPED | SNMAXW);
    MoveWindowAsync(hwnd, mon.left - bd.left, rc.top,
                    mon.right - mon.left + bd.left + bd.right,
                    rc.bottom - rc.top);
  } else {  // vertical
    SetRestoreFlag(hwnd, SNAPPED | SNMAXH);
    MoveWindowAsync(hwnd, rc.left, mon.top - bd.top, rc.right - rc.left,
                    mon.bottom - mon.top + bd.top + bd.bottom);
  }
}
void MinimizeAllOtherWindows(HWND hwnd, int CurrentMonOnly) {
  static HWND restore = nullptr;
  HMONITOR hMon = nullptr;
  hwnd = GetRootOwner(hwnd);
  //    if (!hwnd) return;

  if (CurrentMonOnly) hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

  if (restore == hwnd) {
    // We have to restore all saved windows (minhwnds) when
    // we click again on the same hwnd and have everything saved...
    for (auto hrest : minhwnds) {
      if (IsWindow(hrest) && IsIconic(hrest) &&
          (!hMon ||
           hMon == MonitorFromWindow(hrest, MONITOR_DEFAULTTONEAREST))) {
        // Synchronus restoration to keep the order of windows...
        ShowWindow(hrest, SW_RESTORE);
        SetWindowLevel(hrest, HWND_BOTTOM);
      }
    }
    SetForegroundWindowL(hwnd);
    minhwnds.clear();
    restore = nullptr;
  } else {
    struct MinimizeWindowProcParams p = {nullptr, hwnd};
    restore = hwnd;
    minhwnds.clear();
    if (state.mdiclient) {
      EnumChildWindows(state.mdiclient, MinimizeWindowProc,
                       reinterpret_cast<LPARAM>(&p));
    } else {
      p.hMon = hMon;
      EnumDesktopWindows(nullptr, MinimizeWindowProc,
                         reinterpret_cast<LPARAM>(&p));
    }
  }
}
// Move the window step by step to a direction.
// signed step size and direction = 0 for X 1 for Y
void StepWindow(HWND hwnd, action_t action) {
  // action.fl = 1=>LEFT, 2=>TOP, 3=>RIGHT, 4=>BOTTOM
  short step = action.fl <= 2 ? -action.wp : +action.wp;
  UCHAR direction = !(action.fl & 1);

  RECT rc;
  if (IsZoomed(hwnd)) {
    // return;
    RestoreToMonitorSize(hwnd, &rc);
  }
  if (!GetWindowRect(hwnd, &rc)) return;
  int x = rc.left, y = rc.top;
  int width = rc.right - rc.left;
  int height = rc.bottom - rc.top;
  UCHAR threshold = std::min<UCHAR>(abs(step) - 1, conf.SnapThreshold);

  if (direction == 0) {
    x += step;  // x-axis
    MoveSnap(&x, &y, width, height, threshold);
    y = rc.top;  // y does not change.
    rc.left = x;
    rc.right = x + width;
  } else {
    y += step;  // y-axis;
    MoveSnap(&x, &y, width, height, threshold);
    x = rc.left;  // x does not change.
    rc.top = y;
    rc.bottom = y + height;
  }

  if (IsRectInMonitors(&rc)) {
    // Do not move if target rect is not in a monitor.
    UINT flags = SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE |
                 SWP_NOSIZE | SWP_ASYNCWINDOWPOS;
    if (conf.IgnoreMinMaxInfo) flags |= SWP_NOSENDCHANGING;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, flags);
  }
}
void ActionASOnOff() {
  if (state.action.ac) FinishMovementAsync();
  state.altsnaponoff = !GetProp(g_mainhwnd, APP_ASONOFF);
  SetProp(g_mainhwnd, APP_ASONOFF,
          reinterpret_cast<HANDLE>(static_cast<DorQWORD>(state.altsnaponoff)));
  PostMessage(g_mainhwnd, WM_UPDATETRAY, 0, 0);
}
void ActionMoveOnOff(HWND hwnd) {
  if (GetProp(hwnd, APP_MOVEONOFF))
    RemoveProp(hwnd, APP_MOVEONOFF);
  else
    SetProp(hwnd, APP_MOVEONOFF, reinterpret_cast<HANDLE>(1));
}

}  // namespace altsnap::hooks
