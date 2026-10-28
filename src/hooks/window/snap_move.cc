// snap move (split out of hooks.cc).

#include "hooks/window/snap_move.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "common/ini_section.h"
#include "hooks/blacklist.h"
#include "hooks/hooks_internal.h"
#include "hooks/input_state.h"
#include "hooks/window/snap.h"
#include "hooks/window/window_enum.h"
#include "hooks/window/window_info.h"
#include "hooks/window/window_move.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

///////////////////////////////////////////////////////////////////////////
void MoveSnap(int* _posx, int* _posy, int wndwidth, int wndheight, UCHAR pth) {
  if (!state.snap || state.Speed > conf.AeroMaxSpeed) return;
  const RECT& bd = EnumOnce();

  int posx = *_posx + bd.left;
  int posy = *_posy + bd.top;
  wndwidth -= bd.left + bd.right;
  wndheight -= bd.top + bd.bottom;

  // thresholdx and thresholdy will shrink to make sure
  // the dragged window will snap to the closest windows
  int stickx = 0, sticky = 0;
  short thresholdx = 0, thresholdy = 0;
  UCHAR stuckx = 0, stucky = 0;
  thresholdx = thresholdy = pth;  // conf.SnapThreshold;

  // Loop monitors and windows
  size_t i = 0, j = 0;
  for (i = 0, j = 0; i < monitors.size() || j < wnds.size();) {
    RECT snapwnd;
    UCHAR snapinside = 0;
    unsigned sflag = 0;

    // Get snapwnd
    if (i < monitors.size()) {
      snapwnd = monitors[i];
      snapinside = 1;
      i++;
    } else if (j < wnds.size()) {
      snapwnd = wnds[j].rc;
      sflag = wnds[j].flag;
      snapinside = (state.snap != 2);
      j++;
    } else {
      // No monitors and no windows to snap to.
      return;
    }

    // Check if posx snaps
    if (IsInRangeT(posy, snapwnd.top, snapwnd.bottom, thresholdx) ||
        IsInRangeT(snapwnd.top, posy, posy + wndheight, thresholdx)) {
      UCHAR snapinside_cond =
          (snapinside || posy + wndheight - thresholdx < snapwnd.top ||
           snapwnd.bottom < posy + thresholdx);
      if (IsEqualT(snapwnd.right, posx, thresholdx) && !(sflag & BD_HIDDEN_R)) {
        // The left edge of the dragged window will snap to this window's right
        // edge
        stuckx = 1;
        stickx = snapwnd.right;
        thresholdx = snapwnd.right - posx;
      } else if (snapinside_cond &&
                 IsEqualT(snapwnd.right, posx + wndwidth, thresholdx) &&
                 !(sflag & BD_HIDDEN_R)) {
        // The right edge of the dragged window will snap to this window's right
        // edge
        stuckx = 1;
        stickx = snapwnd.right - wndwidth;
        thresholdx = snapwnd.right - (posx + wndwidth);
      } else if (snapinside_cond && IsEqualT(snapwnd.left, posx, thresholdx) &&
                 !(sflag & BD_HIDDEN_L)) {
        // The left edge of the dragged window will snap to this window's left
        // edge
        stuckx = 1;
        stickx = snapwnd.left;
        thresholdx = snapwnd.left - posx;
      } else if (IsEqualT(snapwnd.left, posx + wndwidth, thresholdx) &&
                 !(sflag & BD_HIDDEN_L)) {
        // The right edge of the dragged window will snap to this window's left
        // edge
        stuckx = 1;
        stickx = snapwnd.left - wndwidth;
        thresholdx = snapwnd.left - (posx + wndwidth);
      }
    }  // end if posx snaps

    // Check if posy snaps
    if (IsInRangeT(posx, snapwnd.left, snapwnd.right, thresholdy) ||
        IsInRangeT(snapwnd.left, posx, posx + wndwidth, thresholdy)) {
      UCHAR snapinside_cond =
          (snapinside || posx + wndwidth - thresholdy < snapwnd.left ||
           snapwnd.right < posx + thresholdy);
      if (IsEqualT(snapwnd.bottom, posy, thresholdy) &&
          !(sflag & BD_HIDDEN_B)) {
        // The top edge of the dragged window will snap to this window's bottom
        // edge
        stucky = 1;
        sticky = snapwnd.bottom;
        thresholdy = snapwnd.bottom - posy;
      } else if (snapinside_cond &&
                 IsEqualT(snapwnd.bottom, posy + wndheight, thresholdy) &&
                 !(sflag & BD_HIDDEN_B)) {
        // The bottom edge of the dragged window will snap to this window's
        // bottom edge
        stucky = 1;
        sticky = snapwnd.bottom - wndheight;
        thresholdy = snapwnd.bottom - (posy + wndheight);
      } else if (snapinside_cond && IsEqualT(snapwnd.top, posy, thresholdy) &&
                 !(sflag & BD_HIDDEN_T)) {
        // The top edge of the dragged window will snap to this window's top
        // edge
        stucky = 1;
        sticky = snapwnd.top;
        thresholdy = snapwnd.top - posy;
      } else if (IsEqualT(snapwnd.top, posy + wndheight, thresholdy) &&
                 !(sflag & BD_HIDDEN_T)) {
        // The bottom edge of the dragged window will snap to this window's top
        // edge
        stucky = 1;
        sticky = snapwnd.top - wndheight;
        thresholdy = snapwnd.top - (posy + wndheight);
      }
    }  // end if posy snaps
  }  // end for

  // Update posx and posy
  if (stuckx) {
    *_posx = stickx - bd.left;
  }
  if (stucky) {
    *_posy = sticky - bd.top;
  }
}

///////////////////////////////////////////////////////////////////////////
void ResizeSnap(int* posx, int* posy, int* wndwidth, int* wndheight, UCHAR pthx,
                UCHAR pthy) {
  if (!state.snap || state.Speed > conf.AeroMaxSpeed) return;

  // thresholdx and thresholdy will shrink to make sure
  // the dragged window will snap to the closest windows
  short thresholdx = 0, thresholdy = 0;
  UCHAR stuckleft = 0, stucktop = 0, stuckright = 0, stuckbottom = 0;
  int stickleft = 0, sticktop = 0, stickright = 0, stickbottom = 0;
  thresholdx = pthx;
  thresholdy = pthy;
  const RECT& borders = EnumOnce();

  // Loop monitors and windows
  size_t i = 0, j = 0;
  for (i = 0, j = 0; i < monitors.size() || j < wnds.size();) {
    RECT snapwnd;
    UCHAR snapinside = 0;

    // Get snapwnd
    if (i < monitors.size()) {
      CopyRect(&snapwnd, &monitors[i]);
      snapinside = 1;
      i++;
    } else if (j < wnds.size()) {
      CopyRect(&snapwnd, &wnds[j].rc);
      snapinside = (state.snap != 2);
      j++;
    } else {
      // nothing to snap to.
      return;
    }

    // Check if posx snaps
    if (IsInRangeT(*posy, snapwnd.top, snapwnd.bottom, thresholdx) ||
        IsInRangeT(snapwnd.top, *posy, *posy + *wndheight, thresholdx)) {
      UCHAR snapinside_cond = snapinside ||
                              (*posy + *wndheight - thresholdx < snapwnd.top) ||
                              (snapwnd.bottom < *posy + thresholdx);
      if (state.resize.x == RZ_LEFT &&
          IsEqualT(snapwnd.right, *posx, thresholdx)) {
        // The left edge of the dragged window will snap to this window's right
        // edge
        stuckleft = 1;
        stickleft = snapwnd.right;
        thresholdx = snapwnd.right - *posx;
      } else if (snapinside_cond && state.resize.x == RZ_RIGHT &&
                 IsEqualT(snapwnd.right, *posx + *wndwidth, thresholdx)) {
        // The right edge of the dragged window will snap to this window's right
        // edge
        stuckright = 1;
        stickright = snapwnd.right;
        thresholdx = snapwnd.right - (*posx + *wndwidth);
      } else if (snapinside_cond && state.resize.x == RZ_LEFT &&
                 IsEqualT(snapwnd.left, *posx, thresholdx)) {
        // The left edge of the dragged window will snap to this window's left
        // edge
        stuckleft = 1;
        stickleft = snapwnd.left;
        thresholdx = snapwnd.left - *posx;
      } else if (state.resize.x == RZ_RIGHT &&
                 IsEqualT(snapwnd.left, *posx + *wndwidth, thresholdx)) {
        // The right edge of the dragged window will snap to this window's left
        // edge
        stuckright = 1;
        stickright = snapwnd.left;
        thresholdx = snapwnd.left - (*posx + *wndwidth);
      }
    }

    // Check if posy snaps
    if (IsInRangeT(*posx, snapwnd.left, snapwnd.right, thresholdy) ||
        IsInRangeT(snapwnd.left, *posx, *posx + *wndwidth, thresholdy)) {
      UCHAR snapinside_cond = snapinside ||
                              (*posx + *wndwidth - thresholdy < snapwnd.left) ||
                              (snapwnd.right < *posx + thresholdy);
      if (state.resize.y == RZ_TOP &&
          IsEqualT(snapwnd.bottom, *posy, thresholdy)) {
        // The top edge of the dragged window will snap to this window's bottom
        // edge
        stucktop = 1;
        sticktop = snapwnd.bottom;
        thresholdy = snapwnd.bottom - *posy;
      } else if (snapinside_cond && state.resize.y == RZ_BOTTOM &&
                 IsEqualT(snapwnd.bottom, *posy + *wndheight, thresholdy)) {
        // The bottom edge of the dragged window will snap to this window's
        // bottom edge
        stuckbottom = 1;
        stickbottom = snapwnd.bottom;
        thresholdy = snapwnd.bottom - (*posy + *wndheight);
      } else if (snapinside_cond && state.resize.y == RZ_TOP &&
                 IsEqualT(snapwnd.top, *posy, thresholdy)) {
        // The top edge of the dragged window will snap to this window's top
        // edge
        stucktop = 1;
        sticktop = snapwnd.top;
        thresholdy = snapwnd.top - *posy;
      } else if (state.resize.y == RZ_BOTTOM &&
                 IsEqualT(snapwnd.top, *posy + *wndheight, thresholdy)) {
        // The bottom edge of the dragged window will snap to this window's top
        // edge
        stuckbottom = 1;
        stickbottom = snapwnd.top;
        thresholdy = snapwnd.top - (*posy + *wndheight);
      }
    }
  }  // end for

  // Update posx, posy, wndwidth and wndheight
  if (stuckleft) {
    *wndwidth = *wndwidth + *posx - stickleft + borders.left;
    *posx = stickleft - borders.left;
  }
  if (stucktop) {
    *wndheight = *wndheight + *posy - sticktop + borders.top;
    *posy = sticktop - borders.top;
  }
  if (stuckright) {
    *wndwidth = stickright - *posx + borders.right;
  }
  if (stuckbottom) {
    *wndheight = stickbottom - *posy + borders.bottom;
  }
}

///////////////////////////////////////////////////////////////////////////
// use snwnds[snwnds.size()].rc / .flag
static void GetAeroSnappingMetrics(int* leftWidth, int* rightWidth,
                                   int* topHeight, int* bottomHeight,
                                   const RECT* mon) {
  *leftWidth = CLAMPW((mon->right - mon->left) * conf.AHoff / 100);
  *rightWidth = CLAMPW((mon->right - mon->left) * (100 - conf.AHoff) / 100);
  *topHeight = CLAMPH((mon->bottom - mon->top) * conf.AVoff / 100);
  *bottomHeight = CLAMPH((mon->bottom - mon->top) * (100 - conf.AVoff) / 100);

  // do not go further is snapping state is toggled or shift is down.
  if (state.snap != conf.AutoSnap) return;
  EnumSnappedOnce();

  // Check on all the other snapped windows from the bottom most
  // To give precedence to the topmost windows
  size_t i = snwnds.size();
  while (i--) {
    unsigned flag = snwnds[i].flag;
    const RECT* wnd = &snwnds[i].rc;
    // if the window is in current monitor
    POINT tpt;
    tpt.x = (wnd->left + wnd->right) / 2;
    tpt.y = (wnd->top + wnd->bottom) / 2;
    if (PtInRect(mon, tpt)) {
      // We have a snapped window in the monitor
      if (flag & SNLEFT) {
        *leftWidth = CLAMPW(wnd->right - wnd->left);
        *rightWidth = CLAMPW(mon->right - wnd->right);
      } else if (flag & SNRIGHT) {
        *rightWidth = CLAMPW(wnd->right - wnd->left);
        *leftWidth = CLAMPW(wnd->left - mon->left);
      }
      if (flag & SNTOP) {
        *topHeight = CLAMPH(wnd->bottom - wnd->top);
        *bottomHeight = CLAMPH(mon->bottom - wnd->bottom);
      } else if (flag & SNBOTTOM) {
        *bottomHeight = CLAMPH(wnd->bottom - wnd->top);
        *topHeight = CLAMPH(wnd->top - mon->top);
      }
    }
  }  // next i
}
/////////////////////////////////////////////////////////////////////////
// also return monitor's DPI
// static UINT GetDPIMonitorRect(const POINT *pt, int full, RECT *_mon)
//{
//    UINT dpi = 0;
//    if (state.mdiclient
//    && GetClientRect(state.mdiclient, _mon)) {
//        return GetDpiForWindow(state.mdiclient); // MDI!
//    }
//
//    HMONITOR hmon = MonitorFromPoint(*pt, MONITOR_DEFAULTTONEAREST);
//    MONITORINFO mi; mi.cbSize = sizeof(mi);
//    GetMonitorInfo(hmon, &mi);
//
//    UINT dpiX, dpiY;
//    if(S_OK == GetDpiForMonitor(hmon, MDT_DEFAULT, &dpiX, &dpiY)) {
//        dpi = dpiX;
//    }
//
//    CopyRect(_mon, full? &mi.rcMonitor : &mi.rcWork);
//    return dpi;
//}

///////////////////////////////////////////////////////////////////////////
int AeroMoveSnap(POINT pt, int* posx, int* posy, int* wndwidth,
                 int* wndheight) {
  // return if last resizing is not finished or no Aero or not resizable.
  if ((!conf.Aero && !(conf.UseZones & 1)) || !state.resizable) return 0;

  LastWin.maximize = 0;
  LastWin.snap = 0;

  // We HAVE to check the monitor for each pt...
  RECT mon;
  GetMonitorRect(&pt, 0, &mon);

  int pLeft = mon.left + conf.AeroThreshold;
  int pRight = mon.right - conf.AeroThreshold;
  int pTop = mon.top + conf.AeroThreshold;
  int pBottom = mon.bottom - conf.AeroThreshold;
  int leftWidth = 0, rightWidth = 0, topHeight = 0, bottomHeight = 0;

  int Left = pLeft + conf.AeroThreshold;
  int Right = pRight - conf.AeroThreshold;
  int Top = pTop + conf.AeroThreshold;
  int Bottom = pBottom - conf.AeroThreshold;

  unsigned restore = GetRestoreFlag(state.hwnd);
  RECT trc;
  trc.left = pLeft;
  trc.top = pTop;
  trc.right = pRight;
  trc.bottom = pBottom;
  // Fallback when the cursor is not at a snapping edge: restore the original
  // size of a previously snapped window.
  const auto restore_original_size = [&]() {
    if (restore & SNAPPED) {
      // Restore original window size
      // Clear restore data at the end of the movement
      LastWin.moveonly = 0;
      SetRestoreFlag(state.hwnd, restore | SNCLEAR);
      restore = 0;
      *wndwidth = state.origin.width;
      *wndheight = state.origin.height;
    }
  };
  if (PtInRect(&trc, pt) || !conf.Aero) {
    restore_original_size();
  } else {
    GetAeroSnappingMetrics(&leftWidth, &rightWidth, &topHeight, &bottomHeight,
                           &mon);
    LastWin.moveonly = 0;  // We shall snap!
    // Move window
    if (pt.y < Top && pt.x < Left) {
      // Top left
      restore = SNAPPED | SNTOPLEFT;
      *wndwidth = leftWidth;
      *wndheight = topHeight;
      *posx = mon.left;
      *posy = mon.top;
    } else if (pt.y < Top && Right < pt.x) {
      // Top right
      restore = SNAPPED | SNTOPRIGHT;
      *wndwidth = rightWidth;
      *wndheight = topHeight;
      *posx = mon.right - *wndwidth;
      *posy = mon.top;
    } else if (Bottom < pt.y && pt.x < Left) {
      // Bottom left
      restore = SNAPPED | SNBOTTOMLEFT;
      *wndwidth = leftWidth;
      *wndheight = bottomHeight;
      *posx = mon.left;
      *posy = mon.bottom - *wndheight;
    } else if (Bottom < pt.y && Right < pt.x) {
      // Bottom right
      restore = SNAPPED | SNBOTTOMRIGHT;
      *wndwidth = rightWidth;
      *wndheight = bottomHeight;
      *posx = mon.right - *wndwidth;
      *posy = mon.bottom - *wndheight;
    } else if (pt.y < pTop) {
      // Pure Top
      if (!state.shift ^ !(conf.AeroTopMaximizes & 1) &&
          (state.Speed < conf.AeroMaxSpeed)) {
        if (conf.FullWin) {
          MaximizeRestoreAtPoint(state.hwnd, SW_MAXIMIZE, 1);
          LastWin.hwnd = nullptr;
          state.moving = 2;
          return 1;
        }
        *posx = mon.left;
        *posy = mon.top;
        *wndwidth = CLAMPW(mon.right - mon.left);
        *wndheight = CLAMPH(mon.bottom - mon.top);
        LastWin.maximize = 1;
        SetRestoreFlag(state.hwnd,
                       SNAPPED | SNCLEAR);  // To clear eventual snapping info
        return 0;

      } else {
        restore = SNAPPED | SNTOP;
        *wndwidth = CLAMPW(mon.right - mon.left);
        *wndheight = topHeight;
        *posx =
            mon.left + (mon.right - mon.left) / 2 - *wndwidth / 2;  // Center
        *posy = mon.top;
      }
    } else if (pt.y > pBottom) {
      // Pure Bottom
      restore = SNAPPED | SNBOTTOM;
      *wndwidth = CLAMPW(mon.right - mon.left);
      *wndheight = bottomHeight;
      *posx = mon.left + (mon.right - mon.left) / 2 - *wndwidth / 2;  // Center
      *posy = mon.bottom - *wndheight;
    } else if (pt.x < pLeft) {
      // Pure Left
      restore = SNAPPED | SNLEFT;
      *wndwidth = leftWidth;
      *wndheight = CLAMPH(mon.bottom - mon.top);
      *posx = mon.left;
      *posy = mon.top + (mon.bottom - mon.top) / 2 - *wndheight / 2;  // Center
    } else if (pt.x > pRight) {
      // Pure Right
      restore = SNAPPED | SNRIGHT;
      *wndwidth = rightWidth;
      *wndheight = CLAMPH(mon.bottom - mon.top);
      *posx = mon.right - *wndwidth;
      *posy = mon.top + (mon.bottom - mon.top) / 2 - *wndheight / 2;  // Center
    } else {
      restore_original_size();
    }
  }

  // Aero-move the window?
  if (restore & SNAPPED) {
    *wndwidth = CLAMPW(*wndwidth);
    *wndheight = CLAMPH(*wndheight);

    SetRestoreData(state.hwnd, state.origin.width, state.origin.height,
                   restore);

    RECT borders;
    FixDWMRect(state.hwnd, &borders);
    *posx -= borders.left;
    *posy -= borders.top;
    *wndwidth += borders.left + borders.right;
    *wndheight += borders.top + borders.bottom;

    // If we go too fast then do not move the window
    if (state.Speed > conf.AeroMaxSpeed) return 1;
    LastWin.moveonly = 0;
    if (conf.FullWin) {
      if (IsZoomed(state.hwnd)) {
        // Avoids flickering
        RestoreWindowTo(state.hwnd, *posx, *posy, *wndwidth, *wndheight);
        RecalcInvisibleBorders();
      }
      assert(LastWin.hwnd == NULL);

      LastWin.hwnd = state.hwnd;
      LastWin.x = *posx;
      LastWin.y = *posy;
      LastWin.width = *wndwidth;
      LastWin.height = *wndheight;
      LastWin.snap = 1;

      MoveWindowNow(&LastWin);
      return 1;
    }
  }
  return 0;
}
///////////////////////////////////////////////////////////////////////////
void AeroResizeSnap(POINT pt, int* posx, int* posy, int* wndwidth,
                    int* wndheight) {
  // return if last resizing is not finished
  if (!conf.Aero || state.Speed > conf.AeroMaxSpeed) return;

  static RECT borders;
  if (!state.moving) {
    FixDWMRect(state.hwnd, &borders);
  }
  unsigned restore = GetRestoreFlag(state.hwnd);
  if (state.resize.x == RZ_XCENTER && state.resize.y == RZ_TOP &&
      pt.y < state.origin.mon.top + conf.AeroThreshold) {
    restore = SNAPPED | SNMAXH;
    *wndheight = CLAMPH(state.origin.mon.bottom - state.origin.mon.top +
                        borders.bottom + borders.top);
    *posy = state.origin.mon.top - borders.top;
  } else if (state.resize.x == RZ_LEFT && state.resize.y == RZ_YCENTER &&
             pt.x < state.origin.mon.left + conf.AeroThreshold) {
    restore = SNAPPED | SNMAXW;
    *wndwidth = CLAMPW(state.origin.mon.right - state.origin.mon.left +
                       borders.left + borders.right);
    *posx = state.origin.mon.left - borders.left;
  }
  // Aero-move the window?
  if (restore & SNAPPED && restore & (SNMAXH | SNMAXW)) {
    SetRestoreData(state.hwnd, state.origin.width, state.origin.height,
                   restore);
  }
}
static void SetEdgeAndOffsetCF(const RECT* wnd, const UCHAR centerfrac,
                               POINT pt) {
  // Set edge and offset
  // Think of the window as nine boxes (corner regions get 38%, middle only 24%)
  // Does not use state.origin.width/height since that is based on
  // wndpl.rcNormalPosition which is not what you see when resizing a window
  // that Windows Aero resized
  int wndwidth = wnd->right - wnd->left;
  int wndheight = wnd->bottom - wnd->top;
  int SideS = (100 - centerfrac) / 2;
  int CenteR = 100 - SideS;

  if (pt.x - wnd->left < (wndwidth * SideS) / 100) {
    state.resize.x = RZ_LEFT;
    state.offset.x = pt.x - wnd->left;
  } else if (pt.x - wnd->left < (wndwidth * CenteR) / 100) {
    state.resize.x = RZ_XCENTER;
    state.offset.x =
        pt.x - state.mdipt.x;  // Used only if both x and y are CENTER
  } else {
    state.resize.x = RZ_RIGHT;
    state.offset.x = wnd->right - pt.x;
  }
  if (pt.y - wnd->top < (wndheight * SideS) / 100) {
    state.resize.y = RZ_TOP;
    state.offset.y = pt.y - wnd->top;
  } else if (pt.y - wnd->top < (wndheight * CenteR) / 100) {
    state.resize.y = RZ_YCENTER;
    state.offset.y = pt.y - state.mdipt.y;
  } else {
    state.resize.y = RZ_BOTTOM;
    state.offset.y = wnd->bottom - pt.y;
  }
  // Set window right/bottom origin
  state.origin.right = wnd->right - state.mdipt.x;
  state.origin.bottom = wnd->bottom - state.mdipt.y;
}

// Use disgonals to determine the closest side.
void SetEdgeToClosestSide(const RECT* wnd, const POINT pt) {
  const int W = wnd->right - wnd->left;
  const int H = wnd->bottom - wnd->top;
  const int x = pt.x - wnd->left;
  const int y = pt.y - wnd->top;
  char TR = y * W <= H * x;        // T/C or R/C mode
  char TL = (H - y) * W >= H * x;  // B/C or C/C mode
  if (TR) {                        // Top or right
    if (TL) {
      state.resize.y = RZ_TOP;
      state.offset.y = pt.y - wnd->top;
    } else {
      state.resize.x = RZ_RIGHT;
      state.offset.x = wnd->right - pt.x;
    }
  } else {     // Bottom or Left
    if (TL) {  // Bottom right
      state.resize.x = RZ_LEFT;
      state.offset.x = pt.x - wnd->left;
    } else {
      state.resize.y = RZ_BOTTOM;
      state.offset.y = wnd->bottom - pt.y;
    }
  }
}
void SetEdgeAndOffset(const RECT* wnd, const POINT pt) {
  SetEdgeAndOffsetCF(wnd, conf.CenterFraction, pt);
  if (conf.SidesFraction == conf.CenterFraction) {
    // Classic 9 quadrent mode
    return;
  }
  // Special mode.
  if (state.resize.x != RZ_XCENTER || state.resize.y != RZ_YCENTER) {
    SetEdgeAndOffsetCF(wnd, conf.SidesFraction, pt);
    if (conf.SidesFraction < conf.CenterFraction) {
    } else {  // (SidesFraction > CenterFraction)
      SetEdgeToClosestSide(wnd, pt);
    }
  }
}

static void NextBorders(RECT* pos, const RECT* cur, const RECT* def) {
  CopyRect(pos, def);
  for (auto& snwnd : snwnds) {
    const RECT* rc = &snwnd.rc;
    const unsigned flg = snwnd.flag;
    POINT tpt;
    tpt.x = (rc->left + rc->right) / 2;
    tpt.y = (rc->top + rc->bottom) / 2;
    if (!PtInRect(def, tpt)) continue;

    if (flg & (SNZONE | SNLEFT) && rc->right < cur->left)
      pos->left = std::max(pos->left, rc->right);
    if (flg & (SNZONE | SNTOP) && rc->bottom < cur->top)
      pos->top = std::max(pos->top, rc->bottom);
    if (flg & (SNZONE | SNRIGHT) && rc->left > cur->right)
      pos->right = std::min(pos->right, rc->left);
    if (flg & (SNZONE | SNBOTTOM) && rc->top > cur->bottom)
      pos->bottom = std::min(pos->bottom, rc->top);
  }
}
void SnapToCorner(HWND hwnd, resizexy_t resize, UCHAR flags) {
  // When trying to Snap or extend a non-resizeable window
  if (!(flags & SNTO_MOVETO) && !IsResizable(hwnd))
    flags = SNTO_MOVETO | SNTO_NEXTBD;  // Move to next bd instead

  SetOriginFromRestoreData(hwnd, k_action_move);
  rgMMI_t mmi;
  GetMinMaxInfo(hwnd, &mmi.Min, &mmi.Max);  // for CLAMPH/W functions

  // Get and set new position
  int posx = 0, posy = 0;  // wndwidth and wndheight are defined above
  unsigned restore = 1;
  const RECT* mon = &state.origin.mon;
  RECT bd, wnd;
  GetWindowRect(hwnd, &wnd);
  SetEdgeAndOffset(&wnd, state.prevpt);  // state.resize.x/y & state.offset.x/y
  if (!resize.x && !resize.y) resize = state.resize;
  FixDWMRect(hwnd, &bd);
  int wndwidth = wnd.right - wnd.left;
  int wndheight = wnd.bottom - wnd.top;

  RECT tmp;

  if (flags & SNTO_NEXTBD) {
    // Find next borders in each direction and set it up as monitor.
    EnumSnapped();
    RECT vwnd;
    GetWindowRectL(hwnd, &vwnd);
    NextBorders(&tmp, &vwnd, mon);
    mon = &tmp;
  }
  if (flags & SNTO_EXTEND) {
    /* Extend window's borders to monitor/Next border */
    posx = wnd.left - state.mdipt.x;
    posy = wnd.top - state.mdipt.y;

    if (resize.y == RZ_TOP) {
      posy = mon->top - bd.top;
      wndheight = CLAMPHL(wnd.bottom - state.mdipt.y - mon->top + bd.top, &mmi);
    } else if (resize.y == RZ_BOTTOM) {
      wndheight =
          CLAMPHL(mon->bottom - wnd.top + state.mdipt.y + bd.bottom, &mmi);
    }
    if (resize.x == RZ_RIGHT) {
      wndwidth =
          CLAMPWL(mon->right - wnd.left + state.mdipt.x + bd.right, &mmi);
    } else if (resize.x == RZ_LEFT) {
      posx = mon->left - bd.left;
      wndwidth = CLAMPWL(wnd.right - state.mdipt.x - mon->left + bd.left, &mmi);
    } else if (resize.x == RZ_XCENTER && resize.y == RZ_YCENTER) {
      wndwidth = CLAMPWL(mon->right - mon->left + bd.left + bd.right, &mmi);
      posx = mon->left - bd.left;
      posy = wnd.top - state.mdipt.y + bd.top;
      restore |= SNMAXW;
    }
  } else if (flags & SNTO_MOVETO) {
    /* Move the windows to a border (monitor or next border) */
    posx = wnd.left - state.mdipt.x;
    posy = wnd.top - state.mdipt.y;
    if (resize.x == RZ_LEFT) {
      posx = mon->left - bd.left;
    } else if (resize.x == RZ_RIGHT) {
      posx = mon->right + bd.right - wndwidth;
    }
    if (resize.y == RZ_TOP) {
      posy = mon->top - bd.top;
    } else if (resize.y == RZ_BOTTOM) {
      posy = mon->bottom + bd.bottom - wndheight;
    }
  } else { /* Aero Snap to corresponding side/corner */
    int leftWidth = 0, rightWidth = 0, topHeight = 0, bottomHeight = 0;
    // EnumSnapped();
    GetAeroSnappingMetrics(&leftWidth, &rightWidth, &topHeight, &bottomHeight,
                           mon);
    wndwidth = leftWidth;
    wndheight = topHeight;
    posx = mon->left;
    posy = mon->top;
    restore = SNTOPLEFT;

    if (resize.y == RZ_YCENTER) {
      wndheight = CLAMPHL(mon->bottom - mon->top, &mmi);  // Max Height
      posy += (mon->bottom - mon->top) / 2 - wndheight / 2;
      restore &= ~SNTOP;
    } else if (resize.y == RZ_BOTTOM) {
      wndheight = bottomHeight;
      posy = mon->bottom - wndheight;
      restore &= ~SNTOP;
      restore |= SNBOTTOM;
    }

    if (resize.x == RZ_XCENTER && resize.y != RZ_YCENTER) {
      wndwidth = CLAMPWL((mon->right - mon->left), &mmi);  // Max width
      posx += (mon->right - mon->left) / 2 - wndwidth / 2;
      restore &= ~SNLEFT;
    } else if (resize.x == RZ_XCENTER) {
      restore &= ~SNLEFT;
      if (resize.y == RZ_YCENTER) {
        restore |= SNMAXH;
        if (IsCtrlDown()) {
          LastWin.hwnd = nullptr;
          ToggleMaxRestore(hwnd);
          return;
        }
      }
      wndwidth = wnd.right - wnd.left - bd.left - bd.right;
      posx = wnd.left - state.mdipt.x + bd.left;
    } else if (resize.x == RZ_RIGHT) {
      wndwidth = rightWidth;
      posx = mon->right - wndwidth;
      restore |= SNRIGHT;
      restore &= ~SNLEFT;
    }
    // FixDWMRect
    posx -= bd.left;
    posy -= bd.top;
    wndwidth += bd.left + bd.right;
    wndheight += bd.top + bd.bottom;
  }
  MoveWindowAsync(hwnd, posx, posy, wndwidth, wndheight);
  // Save data to the window...
  if (!(GetRestoreFlag(hwnd) & SNAPPED))
    SetRestoreData(hwnd, state.origin.width, state.origin.height,
                   SNAPPED | restore);
}

}  // namespace altsnap::hooks
