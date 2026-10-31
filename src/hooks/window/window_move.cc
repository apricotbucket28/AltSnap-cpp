// window move (split out of hooks.cc).

#include "hooks/window/window_move.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "hooks/actions/window_actions.h"
#include "hooks/blacklist.h"
#include "hooks/hooks_internal.h"
#include "hooks/mouse_move.h"
#include "hooks/window/snap.h"
#include "hooks/window/window_enum.h"
#include "hooks/window/window_info.h"

namespace altsnap::hooks {

static void EndDeferWindowPosNow(HDWP hwndSS) {
  EndDeferWindowPos(hwndSS);
  if (conf.RefreshRate) ASleep(conf.RefreshRate);
  Session().last_window.hwnd = nullptr;
}

static int ShouldResizeTouching() {
  return Session().input.action.ac == AC_RESIZE &&
         ((conf.StickyResize & 1 && Session().input.shift) ||
          ((conf.StickyResize & 3) == 2 && !Session().input.shift));
}
int ResizeTouchingWindows(windowRR* lw) {
  if (!ShouldResizeTouching()) return 0;
  const RECT& bd = EnumOnce();
  if (Session().snapped_windows.empty()) return 0;
  // posx, posy,  correspond to the VISIBLE rect
  // of the current window...
  int posx = lw->x + bd.left;
  int posy = lw->y + bd.top;
  int width = lw->width - (bd.left + bd.right);
  int height = lw->height - (bd.top + bd.bottom);

  HDWP hwndSS = nullptr;  // For DeferwindowPos.
  if (conf.FullWin) {
    hwndSS = BeginDeferWindowPos(Session().snapped_windows.size() + 1);
  }
  for (auto& snwnd : Session().snapped_windows) {
    RECT* nwnd = &snwnd.rc;
    unsigned flag = snwnd.flag;
    HWND hwnd = snwnd.hwnd;

    POINT tpt;
    tpt.x = (nwnd->left + nwnd->right) / 2;
    tpt.y = (nwnd->top + nwnd->bottom) / 2;
    if (!PtInRect(&Session().input.origin.mon, tpt)) continue;

    if (PureLeft(flag)) {
      nwnd->right = posx;
    } else if (PureRight(flag)) {
      POINT Min, Max;
      GetMinMaxInfo(hwnd, &Min, &Max);
      nwnd->left = std::clamp<LONG>(posx + width, nwnd->right - Max.x,
                                    nwnd->right - Min.x);
    } else if (PureTop(flag)) {
      nwnd->bottom = posy;
    } else if (PureBottom(flag)) {
      POINT Min, Max;
      GetMinMaxInfo(hwnd, &Min, &Max);
      nwnd->top = std::clamp<LONG>(posy + height, nwnd->bottom - Max.x,
                                   nwnd->bottom - Min.x);
    } else {
      continue;
    }
    if (hwndSS) {
      RECT nbd;
      FixDWMRect(hwnd, &nbd);
      hwndSS = DeferWindowPos(
          hwndSS, hwnd, nullptr, nwnd->left - nbd.left, nwnd->top - nbd.top,
          nwnd->right - nwnd->left + nbd.left + nbd.right,
          nwnd->bottom - nwnd->top + nbd.top + nbd.bottom,
          SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    snwnd.flag = flag | TORESIZE;
  }

  if (hwndSS) {
    // Draw changes ONLY if full win is ON,
    hwndSS = DeferWindowPos(hwndSS, Session().input.hwnd, nullptr, lw->x, lw->y,
                            lw->width, lw->height,
                            SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    if (hwndSS) EndDeferWindowPosNow(hwndSS);
  }
  return 1;
}

void ResizeAllSnappedWindows() {
  if (!conf.StickyResize || Session().snapped_windows.empty()) return;

  HDWP hwndSS = BeginDeferWindowPos(Session().snapped_windows.size() + 1);
  for (auto& snwnd : Session().snapped_windows) {
    if (hwndSS && snwnd.flag & TORESIZE) {
      RECT bd;
      FixDWMRect(snwnd.hwnd, &bd);
      InflateRectBorder(&snwnd.rc, &bd);
      hwndSS = DeferWindowPos(
          hwndSS, snwnd.hwnd, nullptr, snwnd.rc.left, snwnd.rc.top,
          snwnd.rc.right - snwnd.rc.left, snwnd.rc.bottom - snwnd.rc.top,
          SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
  }
  if (hwndSS)
    hwndSS = DeferWindowPos(hwndSS, Session().last_window.hwnd, nullptr,
                            Session().last_window.x, Session().last_window.y,
                            Session().last_window.width,
                            Session().last_window.height,
                            SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
  if (hwndSS) EndDeferWindowPosNow(hwndSS);
  Session().last_window.hwnd = nullptr;
}
// Upstream: MaximizeRestore_atpt
void MaximizeRestoreAtPoint(HWND hwnd, UINT sw_cmd, int origin) {
  WINDOWPLACEMENT wndpl;
  wndpl.length = sizeof(wndpl);
  GetWindowPlacement(hwnd, &wndpl);
  if (sw_cmd != SW_FULLSCREEN) wndpl.showCmd = sw_cmd;

  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  if (sw_cmd == SW_MAXIMIZE || sw_cmd == SW_FULLSCREEN) {
    HMONITOR wndmonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    HMONITOR monitor =
        MonitorFromPoint(Session().input.prevpt, MONITOR_DEFAULTTONEAREST);

    if (origin & 1) {
      // set normal windpw plasement to origin.
      wndpl.rcNormalPosition.right =
          wndpl.rcNormalPosition.left + Session().input.origin.width;
      wndpl.rcNormalPosition.bottom =
          wndpl.rcNormalPosition.top + Session().input.origin.height;
    }

    GetMonitorInfo(monitor, &mi);

    // Center window on monitor, if needed
    if (monitor != wndmonitor) {
      CenterRectInRect(&wndpl.rcNormalPosition, &mi.rcWork);
    }
  }
  if (origin & 2) wndpl.flags |= WPF_ASYNCWINDOWPLACEMENT;
  SetWindowPlacement(hwnd, &wndpl);
  if (sw_cmd == SW_FULLSCREEN) {
    MoveWindowAsync(hwnd, mi.rcMonitor.left, mi.rcMonitor.top,
                    mi.rcMonitor.right - mi.rcMonitor.left,
                    mi.rcMonitor.bottom - mi.rcMonitor.top);
  }
}

static void RestoreWindowToRect(HWND hwnd, const RECT* rc) {
  RECT zbd, bd;
  FixDWMRect(hwnd, &zbd);  // Zoomed invisible borders (that were applied)
  WINDOWPLACEMENT wndpl;
  wndpl.length = sizeof(wndpl);
  GetWindowPlacement(hwnd, &wndpl);
  wndpl.showCmd = SW_RESTORE;
  // wndpl.flags |= flags;
  CopyRect(&wndpl.rcNormalPosition, rc);
  if (LOBYTE(GetVersion()) >= 10) {
    // On Windows 10+ we got invisible borders...
    // wndpl.flags &= ~WPF_ASYNCWINDOWPLACEMENT;
    // Synchronus restore because we have to check for Invisible
    // borders again that are different when Zoomed/restored.
    SetWindowPlacement(hwnd, &wndpl);
    FixDWMRect(hwnd, &bd);  // Restored invisible borders
    if (!EqualRect(&zbd, &bd)) {
      // Wrong invisible borders were applied,
      // correct it with an async move.
      RECT& r = wndpl.rcNormalPosition;
      DeflateRectBorder(&r, &zbd);
      InflateRectBorder(&r, &bd);

      // Move again window to its proper rect
      UINT swpFlgs = SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                     SWP_NOZORDER /*|SWP_ASYNCWINDOWPOS*/;
      if (conf.IgnoreMinMaxInfo) swpFlgs |= SWP_NOSENDCHANGING;
      SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left,
                   r.bottom - r.top, swpFlgs);
    }
  } else {
    SetWindowPlacement(hwnd, &wndpl);
  }
}
void RestoreWindowTo(HWND hwnd, int x, int y, int w, int h) {
  RECT rc = {x, y, x + w, y + h};
  RestoreWindowToRect(hwnd, &rc);
}
/* Helper function to Move the window in the worker thread
 * Also restores the window if needed. */
void MoveWindowAsync(HWND hwnd, int x, int y, int w, int h) {
  RECT rc;
  UCHAR moveonly = 0;
  resizexy_t resize = {RZ_XCENTER, RZ_YCENTER};
  if (!GetWindowRect(hwnd, &rc)) return;
  moveonly = w == rc.right - rc.left && h == rc.bottom - rc.top;
  if (!moveonly) {
    // Determine the correct resizexy_t structure
    // depending on the wished position and the current onr
    static_assert(RZ_XCENTER == (RZ_LEFT | RZ_RIGHT), "!!");
    if (rc.left == x) resize.x &= ~RZ_LEFT;
    if (rc.right == x + w) resize.x &= ~RZ_RIGHT;
    if (rc.top == y) resize.y &= ~RZ_TOP;
    if (rc.bottom == y + h) resize.y &= ~RZ_BOTTOM;
  }

  windowRR request{};
  request.hwnd = hwnd;
  request.x = x;
  request.y = y;
  request.width = w;
  request.height = h;
  request.end = 1;
  request.start = 1;
  request.resize = resize;
  request.moveonly = moveonly;
  (void)PostWorkerCommand(MoveWindowCommand{request});
}

/* Send WM_SIZING before SetWindowPos so that apps like terminal
 * emulators can snap to their character-cell grid.  */
BOOL LetWindowKickBack(HWND hwnd, struct windowRR* lw) {
  /* Apply the blacklist (also applies to WM_*SIZEMOVE) */
  if (IsBlacklisted(hwnd, BlkLst.SSizeMove)) return FALSE;

  /* WM_SIZING may enforce minimum width/height */
  if (conf.IgnoreMinMaxInfo != 0) {
    rgMMI_t mmi;
    GetMinMaxInfoF(hwnd, &mmi.Min, &mmi.Max, 0);  // for CLAMPHL/WL functions

    if (CLAMPWL(lw->width, &mmi) != lw->width ||
        CLAMPHL(lw->height, &mmi) != lw->height)
      return FALSE;
  }

  static const unsigned char map[3][3] = {
      //             LEFT              RIGHT             XCENTER
      /* TOP */ {WMSZ_TOPLEFT, WMSZ_TOPRIGHT, WMSZ_TOP},
      /* BOTTOM */ {WMSZ_BOTTOMLEFT, WMSZ_BOTTOMRIGHT, WMSZ_BOTTOM},
      /* CENTER */ {WMSZ_LEFT, WMSZ_RIGHT, WMSZ_BOTTOMRIGHT},
  };

  const resizexy_t rz = lw->resize;
  if (rz.x == 0 || rz.y == 0 || lw->moveonly) return FALSE;
  const BYTE rz_flag = map[rz.y - 1][rz.x - 1];

  // Send WM_SIZING so apps can adjust to their grid (eg terminals)
  RECT rc = {lw->x, lw->y, lw->x + lw->width, lw->y + lw->height};
  // const int o_right = rc.right, o_bottom = rc.bottom;
  DWORD_PTR ret = 0;
  if (SendMessageTimeout(hwnd, WM_SIZING, rz_flag,
                         reinterpret_cast<LPARAM>(&rc), SMTO_ABORTIFHUNG, 64,
                         &ret) &&
      ret) {
    int nw = rc.right - rc.left;
    int nh = rc.bottom - rc.top;
    lw->x = rc.left;
    lw->y = rc.top;

    // if (rz.x == RZ_LEFT) // Keep right border in place
    //     lw->x = o_right - nw;
    // if (rz.y == RZ_TOP) // Keep bottom border in place
    //     lw->y = o_bottom - nh;

    // All directions mode
    // Try to keep the window centered somewhat...
    if (rz.x == RZ_XCENTER && rz.y == RZ_YCENTER) {
      lw->x = lw->x + (lw->width - nw) / 2;
      lw->y = lw->y + (lw->height - nh) / 2;
    }

    lw->width = nw;
    lw->height = nh;

    return TRUE;
  }
  return FALSE;
}

// Move the windows in a thread in case it is very slow to resize
// Upstream: MoveResizeWindowNow_
static void MoveResizeWindowNowEx(struct windowRR* lw, UINT flag) {
  HWND hwnd = nullptr;
  hwnd = lw->hwnd;

  // At the End of movement unless it is complete in one go
  // ie: FullWin=0 or MoveWindowAsync
  if (lw->end && !lw->start) Sleep(8);

  // Send WM_ENTERSIZEMOVE and EVENT_SYSTEM_MOVESIZESTART
  if (lw->start) NotifySizeMoveStaEnd(hwnd, 1);  // Once darg actually starts.

  LOG("MoveResizeWindowNowEx:lw->end=%d, start=%d, moveonly=%d, maximize=%d, "
      "snap=%d, resize{%d,%d}",
      static_cast<int>(lw->end), static_cast<int>(lw->start),
      static_cast<int>(lw->moveonly), static_cast<int>(lw->maximize),
      static_cast<int>(lw->snap), static_cast<int>(lw->resize.x),
      static_cast<int>(lw->resize.y));
  if (lw->end && !lw->maximize && (IsZoomed(hwnd) || IsWindowSnapped(hwnd))) {
    // Use Restore
    RestoreWindowTo(hwnd, lw->x, lw->y, lw->width, lw->height);
  } else {
    LetWindowKickBack(hwnd, lw);
    SetWindowPos(hwnd, nullptr, lw->x, lw->y, lw->width, lw->height, flag);

    // Send WM_SYNCPAINT in case to wait for the end of movement
    // And to avoid windows to "slide through" the whole WM_MOVE queue
    if (flag & SWP_ASYNCWINDOWPOS) {  // SendMessage(hwnd, WM_SYNCPAINT, 0, 0);
      DorQWORD dummy = 0;
      SendMessageTimeout(hwnd, WM_SYNCPAINT, 0, 0, SMTO_NORMAL, 120, &dummy);
    }
    if (conf.RefreshRate) ASleep(conf.RefreshRate);  // Accurate!!!
  }

  // In case of FullWin=0 or MoveWindowAsync
  if (lw->end /*&& !conf.FullWin*/) NotifySizeMoveStaEnd(hwnd, 0);

  lw->start = 0;
  lw->end = 0;
  lw->hwnd = nullptr;
}

/* kMoveAsyncFlags |SWP_DEFERERASE ??*/
inline constexpr UINT kResizeFlags =
    SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE;
inline constexpr UINT kMoveThickBordersFlags = kResizeFlags | SWP_NOSIZE;
inline constexpr UINT kMoveAsyncFlags =
    kMoveThickBordersFlags | SWP_ASYNCWINDOWPOS;
DWORD MoveWindowNow(windowRR* lw) {
  // Use the target's legacy resizability encoding for both paths. ResizeAll
  // is a raw INI byte and may itself contribute bit 2, even without a frame.
  const bool thick_frame = (IsResizable(lw->hwnd) & 2) != 0;
  // lw->resizing_now = 1;
  //    RECT rc;
  //    int notsamesize = 0;
  //    if (GetWindowRect(lw->hwnd, &rc)) {
  //        int cW = rc.right - rc.left;
  //        int cH = rc.bottom - rc.top;
  //        UINT cdpi = GetDpiForWindow(lw->hwnd);
  //        if ( cdpi && cdpi != lw->odpi ) {
  //            // If dpi is not the same we must check the *scaled* values.
  //            if (cW == lw->width && cH == lw->height)
  //                // Window had no time to resize between monitors?
  //                notsamesize = 0;
  //            else
  //                notsamesize = (cW * lw->odpi)>>3 != (lw->width  * cdpi)>>3
  //                       || (cH * lw->odpi)>>3 != (lw->height * cdpi)>>3;
  //        } else {
  //            notsamesize =  cW != lw->width ||  cH != lw->height;
  //        }
  //        //LOGA("MV: %d:%d/%d -> %d:%d/%d %s", cW, cH, cdpi , lw->width,
  //        lw->height, lw->odpi, notsamesize?"(dif)":"(eq)");
  //    }
  //    UINT flag = notsamesize? kResizeFlags: state.resizable&2 ?
  //    kMoveThickBordersFlags: kMoveAsyncFlags;
  int notsamesize = 1;
  int nothingtodo = 0;
  RECT rc;
  if (conf.FullWin) {
    notsamesize = !lw->moveonly;
    if (lw->odpi == GetDpiForWindow(lw->hwnd) && GetWindowRect(lw->hwnd, &rc)) {
      int cW = rc.right - rc.left;
      int cH = rc.bottom - rc.top;
      nothingtodo = rc.left == lw->x && rc.top == lw->y && cW == lw->width &&
                    cH == lw->height;
    }

  } else {
    // Hollow rectangle mode.
    if (GetWindowRect(lw->hwnd, &rc)) {
      int cW = rc.right - rc.left;
      int cH = rc.bottom - rc.top;
      notsamesize = cW != lw->width || cH != lw->height;
    }
  }

  UINT flag = notsamesize   ? kResizeFlags
              : thick_frame ? kMoveThickBordersFlags
                            : kMoveAsyncFlags;
  if (conf.IgnoreMinMaxInfo) flag |= SWP_NOSENDCHANGING;

  if (nothingtodo)
    lw->hwnd = nullptr;  // DONE!
  else
    MoveResizeWindowNowEx(lw, flag);

  // lw->resizing_now = 0;
  return 0;
}

// index 1 => normal restore on any move restore & 1
// restore & 3 => Both 1 & 2 ie: Maximized then rolled.
void RestoreOldWin(const POINT pt, unsigned was_snapped, RECT* ownd) {
  // Restore old width/height?
  unsigned restore = 0;
  int rwidth = 0, rheight = 0;
  unsigned rdata_flag = GetRestoreData(Session().input.hwnd, &rwidth, &rheight);

  if (((rdata_flag & SNAPPED) &&
       !(Session().input.origin.maximized && rdata_flag & 2))) {
    // Set origin width and height to the saved values
    if (!Session().input.usezones) {
      restore = rdata_flag;
      Session().input.origin.width = rwidth;
      Session().input.origin.height = rheight;
      ClearRestoreData(Session().input.hwnd);
    }
  }

  RECT wnd;
  GetWindowRect(Session().input.hwnd, &wnd);
  // In case window is Maximized + Rolled get bottom where it needs to be
  // So that the window stays fully in the monitor
  // Note: a maximized then rolled window does not have the rolled flag.
  if (Session().input.origin.maximized)
    wnd.bottom = Session().input.origin.mon.bottom + Session().input.mdipt.y;

  // Set offset
  Session().input.offset.x =
      Session().input.origin.width *
      std::min(pt.x - wnd.left, wnd.right - wnd.left) /
      std::max(wnd.right - wnd.left, static_cast<LONG>(1));
  Session().input.offset.y =
      Session().input.origin.height *
      std::min(pt.y - wnd.top, wnd.bottom - wnd.top) /
      std::max(wnd.bottom - wnd.top, static_cast<LONG>(1));

  if (rdata_flag & ROLLED) {
    if (Session().input.origin.maximized || was_snapped) {
      // if we restore a Rolled + Maximized/snapped window...
      Session().input.offset.y =
          GetSystemMetricsForWin(SM_CYMIN, Session().input.hwnd) / 2;
    } else {
      Session().input.offset.x = pt.x - wnd.left;
      Session().input.offset.y = pt.y - wnd.top;
    }
  }

  if (restore) {
    Session().last_window.moveonly = 0;
    ownd->left = pt.x - Session().input.offset.x - Session().input.mdipt.x;
    ownd->top = pt.y - Session().input.offset.y - Session().input.mdipt.y;
    ownd->right = ownd->left + Session().input.origin.width;
    ownd->bottom = ownd->top + Session().input.origin.height;
    ClearRestoreData(Session().input.hwnd);
  }
}

void SetOriginFromRestoreData(HWND hwnd, action_t action) {
  // Set Origin width and height needed for AC_MOVE/RESIZE/CENTER/MAXHV
  int rwidth = 0, rheight = 0;
  unsigned rdata_flag = GetRestoreData(hwnd, &rwidth, &rheight);
  // Clear snapping info if asked.
  if (rdata_flag & SNCLEAR || (conf.SmartAero & 4 && action.ac == AC_MOVE)) {
    ClearRestoreData(hwnd);
    rdata_flag = 0;
  }
  // Replace origin width/height if available in the restore Data.
  if (rdata_flag && !Session().input.origin.maximized) {
    Session().input.origin.width = rwidth;
    Session().input.origin.height = rheight;
  }
}

// state.origin.mon initialized in init_mov..
// returns the final window rectangle.
void RestoreToMonitorSize(HWND hwnd, RECT* wnd) {
  ClearRestoreData(hwnd);  // Clear restore flag and data
  WINDOWPLACEMENT wndpl;
  wndpl.length = sizeof(wndpl);
  GetWindowPlacement(hwnd, &wndpl);

  // Set size to origin monitor to prevent flickering
  // Get monitor info
  MONITORINFO mi;
  GetMonitorInfoFromWin(hwnd, &mi);

  CopyRect(wnd, &mi.rcWork);
  CopyRect(&wndpl.rcNormalPosition, wnd);

  if (Session().input.mdiclient) {
    // Make it a little smaller since MDIClients by
    // default have scrollbars that would otherwise appear
    wndpl.rcNormalPosition.right -= 8;
    wndpl.rcNormalPosition.bottom -= 8;
  }
  wndpl.showCmd = SW_RESTORE;
  SetWindowPlacement(hwnd, &wndpl);
  if (Session().input.mdiclient) {
    // Get new values from MDIClient,
    // since restoring the child have changed them,
    Sleep(1);  // Sometimes needed
    GetMDInfo(&Session().input.mdipt, wnd);
    CopyRect(&Session().input.origin.mon, wnd);

    Session().input.origin.right = wnd->right;
    Session().input.origin.bottom = wnd->bottom;
  }
  // Fix wnd for MDI offset and invisible borders
  RECT borders;
  FixDWMRect(hwnd, &borders);
  OffsetRect(wnd, Session().input.mdipt.x, Session().input.mdipt.y);
  InflateRectBorder(wnd, &borders);
}
// Call if you need to resize the window.
// If smart areo is not enabled then, we need to clear the restore date
void ClearRestoreFlagOnResizeIfNeeded(HWND hwnd) {
  if (!(conf.SmartAero & 1)) {
    // Always clear when AeroSmart is disabled.
    ClearRestoreData(hwnd);
  } else {
    // Do not clear is the window was snapped to some side or rolled.
    unsigned smart_restore_flag = (SNZONE | SNAPPEDSIDE | ROLLED);
    if (!(GetRestoreFlag(hwnd) & smart_restore_flag)) ClearRestoreData(hwnd);
  }
}
void MoveToCurrentMonitorIfNeeded(HWND hwnd) {
  if (Session().input.mdiclient) return;
  RECT rc;
  GetWindowRect(hwnd, &rc);

  POINT pt;
  pt.x = rc.left + (rc.right - rc.left) / 2;
  pt.y = rc.top + (rc.bottom - rc.top) / 2;

  assert(Session().input.origin.monitor != 0);

  // If the window centre is outside the current monitor
  // (use the point instead of window, since MonitorFromWindow
  // returns ones that are only touching, which are still not
  // accessible for the user)
  if (MonitorFromPoint(pt, MONITOR_DEFAULTTONULL) !=
      Session().input.origin.monitor) {
    // Put the window on-screen
    CenterWindow(hwnd, CW_TRIM, 0);
  }
}

}  // namespace altsnap::hooks
