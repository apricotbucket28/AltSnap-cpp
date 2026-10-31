// window enum (split out of hooks.cc).

#include "hooks/window/window_enum.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "common/ini_section.h"
#include "hooks/blacklist.h"
#include "hooks/hooks_internal.h"
#include "hooks/overlay/window_menu.h"
#include "hooks/window/snap.h"
#include "hooks/window/window_info.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

/////////////////////////////////////////////////////////////////////////////
static BOOL CALLBACK EnumWindowsProc(HWND window, LPARAM lParam) {
  // Only store window if it's visible, not minimized to taskbar,
  // not the window we are dragging and not IsBlacklisted
  struct snwdata sn;
  if (ShouldSnapTo(window) && GetWindowRectL(window, &sn.rc)) {
    // Maximized?
    if (IsZoomed(window)) {
      // Skip maximized windows in MDI clients
      if (state.mdiclient) return TRUE;
      // Get monitor size
      MONITORINFO mi;
      GetMonitorInfoFromWin(window, &mi);
      // Crop this window so that it does not exceed the size of the monitor
      // This is done because when maximized, windows have an extra invisible
      // border (a border that stretches onto other monitors)
      CropRect(&sn.rc, &mi.rcWork);
    }
    OffsetRectMDI(&sn.rc);
    // Return if this window is overlapped by another window

    RECT bdrcL = {sn.rc.left, sn.rc.top, sn.rc.left, sn.rc.bottom};
    RECT bdrcT = {sn.rc.left, sn.rc.top, sn.rc.right, sn.rc.top};
    RECT bdrcR = {sn.rc.right, sn.rc.top, sn.rc.right, sn.rc.bottom};
    RECT bdrcB = {sn.rc.left, sn.rc.bottom, sn.rc.right, sn.rc.bottom};

    for (auto& wnd : wnds) {
      if (RectInRect(&wnd.rc, &sn.rc)) {
        return TRUE;
      }
      CropOutRectFromSeg(&bdrcL, &wnd.rc);
      CropOutRectFromSeg(&bdrcT, &wnd.rc);
      CropOutRectFromSeg(&bdrcR, &wnd.rc);
      CropOutRectFromSeg(&bdrcB, &wnd.rc);
    }
    // flags is telling which border is visible
    // Check if there is any remaining visibility
    // for each border of the rectangle
    sn.flag = ((bdrcL.top >= bdrcL.bottom) << 0) |
              ((bdrcT.left >= bdrcT.right) << 1) |
              ((bdrcR.top >= bdrcR.bottom) << 2) |
              ((bdrcB.left >= bdrcB.right) << 3);

    sn.hwnd = window;

    // Add window to wnds db
    wnds.push_back(sn);
  }
  return TRUE;
}

static BOOL CALLBACK EnumSnappedWindows(HWND hwnd, LPARAM lParam) {
  struct snwdata snw = {};
  if (ShouldSnapTo(hwnd) && !IsZoomed(hwnd) && GetWindowRectL(hwnd, &snw.rc)) {
    unsigned restore = 0;
    if (conf.SmartAero & 2 || IsWindowSnapped(hwnd)) {
      // In SMARTER snapping mode or if the WINDOW IS SNAPPED
      // We only consider the position of the window
      // to determine its snapping state
      MONITORINFO mi;
      GetMonitorInfoFromWin(hwnd, &mi);
      snw.flag = WhichSideRectInRect(&mi.rcWork, &snw.rc);
    } else if ((restore = GetRestoreFlag(hwnd)) && restore & SNAPPED &&
               restore & SNAPPEDSIDE) {
      // The window was AltSnapped...
      snw.flag = restore;
    } else {
      // thiw window is not snapped.
      return TRUE;  // next hwnd
    }
    // Add the window to the list
    snw.hwnd = hwnd;
    OffsetRectMDI(&snw.rc);
    snwnds.push_back(snw);
  }
  return TRUE;
}
// If lParam is set to 1 then only windows that are
// touching the current window will be considered.
void EnumSnapped() {
  snwnds.clear();
  if (conf.SmartAero & 1) {
    if (state.mdiclient)
      EnumChildWindows(state.mdiclient, EnumSnappedWindows, 0);
    else
      EnumDesktopWindows(nullptr, EnumSnappedWindows, 0);
  }
}
/////////////////////////////////////////////////////////////////////////////
// Uses the same DB than snapped windows db because they will never
// be used together Enum() vs EnumSnapped()
static BOOL CALLBACK EnumTouchingWindows(HWND hwnd, LPARAM lParam) {
  struct snwdata sn;
  if (ShouldSnapTo(hwnd) && !IsZoomed(hwnd) && IsResizable(hwnd) &&
      !IsBlacklisted(hwnd, BlkLst.Windows) && GetWindowRectL(hwnd, &sn.rc)) {
    // Only considers windows that are
    // touching the currently resized window
    RECT statewnd;
    GetWindowRectL(state.hwnd, &statewnd);
    sn.flag = AreRectsTouchingT(&statewnd, &sn.rc, conf.SnapThreshold / 2);
    if (sn.flag) {
      OffsetRectMDI(&sn.rc);

      // Return if this window is overlapped by another window
      for (auto& snwnd : snwnds) {
        if (RectInRect(&snwnd.rc, &sn.rc)) {
          return TRUE;
        }
      }
      // Add the window to the list
      sn.hwnd = hwnd;
      snwnds.push_back(sn);
    }
  }
  return TRUE;
}

///////////////////////////////////////////////////////////////////////////
// Just used in Enum
static void EnumMdi() {
  // Add MDIClient as the monitor
  RECT rc;
  if (GetClientRect(state.mdiclient, &rc)) monitors.push_back(rc);

  if (state.snap > 1) {
    EnumChildWindows(state.mdiclient, EnumWindowsProc, 0);
  }
  if (conf.StickyResize) {
    EnumChildWindows(state.mdiclient, EnumTouchingWindows, 0);
  }
}
///////////////////////////////////////////////////////////////////////////
// Enumerate all monitors/windows/MDI depending on state.
static void Enum() {
  monitors.clear();
  wnds.clear();
  snwnds.clear();

  // MDI
  if (state.mdiclient && IsWindow(state.mdiclient)) {
    EnumMdi();
    return;
  }

  // Enumerate monitors
  EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsProc, 0);

  // Enumerate windows
  if (state.snap >= 2) {
    EnumDesktopWindows(nullptr, EnumWindowsProc, 0);
  }

  if (conf.StickyResize) {
    EnumDesktopWindows(nullptr, EnumTouchingWindows, 0);
  }
}
///////////////////////////////////////////////////////////////////////////
//
// Invisible borders (DWM shadow) of the window being moved.
static RECT g_invisible_borders;

// Enumerates the monitors and windows once per movement, and returns the
// invisible borders of the window being moved.
const RECT& EnumOnce() {
  if (!(state.enumed & 1)) {
    Enum();  // Enumerate monitors and windows
    FixDWMRect(state.hwnd, &g_invisible_borders);
    state.enumed |= 1;
  }
  return g_invisible_borders;
}

// Recompute the invisible borders (e.g. after the window was restored).
void RecalcInvisibleBorders() {
  FixDWMRect(state.hwnd, &g_invisible_borders);
}
void EnumSnappedOnce() {
  if (!(state.enumed & 2)) {
    // LOGA("EnumSnapped");
    EnumSnapped();
    state.enumed |= 2;
  }
}
/////////////////////////////////////////////////////////////////////////////
// lParam means to include minimized windows (pass TRK_LASERMODE to
// TrackMenuOfWindows)
BOOL CALLBACK EnumAltTabWindows(HWND window, LPARAM lParam) {
  // Only store window if it's visible, not minimized
  // to taskbar and on the same monitor as the cursor
  if (IsAltTabAble(window) &&
      (!IsIconic(window) || (lParam && !IsToolWindow(window))) &&
      state.origin.monitor ==
          MonitorFromWindow(window, conf.MenuShowOffscreenWin
                                        ? MONITOR_DEFAULTTONEAREST
                                        : MONITOR_DEFAULTTONULL)) {
    hwnds.push_back(window);
    return TRUE;
  }
  return TRUE;
}
BOOL CALLBACK EnumAllAltTabWindows(HWND window, LPARAM lParam) {
  // Only store window if it's visible, not minimized
  // to taskbar, and either:
  //   offscreen windows are shown, or
  //   the window touches a monitor
  if (IsAltTabAble(window) &&
      (!IsIconic(window) || (lParam && !IsToolWindow(window))) &&
      (conf.MenuShowOffscreenWin ||
       MonitorFromWindow(window, MONITOR_DEFAULTTONULL))) {
    hwnds.push_back(window);
    return TRUE;
  }
  return TRUE;
}
BOOL CALLBACK EnumTopMostWindows(HWND window, LPARAM lParam) {
  // Only store window if it's visible, not minimized
  // to taskbar and on the same monitor as the cursor
  if (IsAltTabAble(window) &&
      GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_TOPMOST) {
    hwnds.push_back(window);
    return TRUE;
  }
  return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
[[nodiscard]] static BOOL StackedRectsT(
    const RECT* a, const RECT* b,
    const int T) {  // Determine wether or not the windows are stacked...
  return RectInRectT(a, b, T) || RectInRectT(b, a, T);
}
// Similar to the EnumAltTabWindows, to be used in AltTab();
BOOL CALLBACK EnumStackedWindowsProc(HWND hwnd, LPARAM lasermode) {
  // Only store window if it's visible, not minimized to taskbar
  RECT wnd, refwnd;
  if (IsAltTabAble(hwnd) && !IsIconic(hwnd) &&
      GetWindowRectL(state.hwnd, &refwnd) && GetWindowRectL(hwnd, &wnd) &&
      (lasermode || StackedRectsT(&refwnd, &wnd, conf.SnapThreshold / 2)) &&
      InflateRect(&wnd, conf.SnapThreshold, conf.SnapThreshold) &&
      (state.ignorept || PtInRect(&wnd, state.prevpt))) {
    hwnds.push_back(hwnd);
    return TRUE;
    LOG("EnumStackedWindowsProc found");
  } else {
    LOG("EnumStackedWindowsProc skip");
  }
  return TRUE;
}
BOOL CALLBACK MinimizeWindowProc(HWND hwnd, LPARAM lParam) {
  const auto* p =
      reinterpret_cast<const struct MinimizeWindowProcParams*>(lParam);
  hwnd = GetRootOwner(hwnd);

  if (hwnd != p->clickedhwnd
      // && IsAltTabAble(hwnd) ????
      && IsVisible(hwnd) && !IsIconic(hwnd) &&
      (WS_MINIMIZEBOX & GetWindowLongPtr(hwnd, GWL_STYLE))) {
    if (!p->hMon ||
        p->hMon == MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)) {
      MinimizeWindow(hwnd);
      minhwnds.push_back(hwnd);
      return TRUE;
    }
  }
  return TRUE;
}
// Finds the window that is just next the specified one.
// Works better if they are arranged like in snap layouts.
struct FindTiledWindow_struct {
  POINT opt;  // in
  long owidth;
  long oheight;
  HWND ihwnd;               // in
  HWND ohwnd;               // out
  POINT distance;           // internal
  unsigned char direction;  // in
  unsigned char diagonal;   // in
};
static BOOL CALLBACK FindTiledWindowEnumProc(HWND hwnd, LPARAM lp) {
  auto* tw = reinterpret_cast<struct FindTiledWindow_struct*>(lp);
  RECT rc;
  if (tw->ihwnd == hwnd || !IsAltTabAble(hwnd) || !GetWindowRectL(hwnd, &rc))
    return TRUE;  // Next hwnd
  POINT pt;
  pt.x = (rc.left + rc.right) / 2;
  pt.y = (rc.top + rc.bottom) / 2;
  long dx = pt.x - tw->opt.x;
  long dy = pt.y - tw->opt.y;
  long adx = abs(dx);
  long ady = abs(dy);
  // LOGA("adx = %d, ady = %d, tw->opt=%d,%d tw->oheight = %d, tw->owidth = %d"
  //     , adx,      ady,   tw->opt.x,tw->opt.y,  tw->oheight, tw->owidth);

  if (tw->diagonal) {
    // We only use the position of the center of each window.
    // We check windows within a 45deg cone around the direction of choice.
    switch (tw->direction) {
      case 0:  // LEFT
        if (dx < 0 && ady <= adx &&
            (adx < tw->distance.x ||
             (adx == tw->distance.x && ady < tw->distance.y)))
          break;      // Window is closer...
        return TRUE;  // skip
      case 1:         // UP
        if (dy < 0 && adx <= ady &&
            (ady < tw->distance.y ||
             (ady == tw->distance.y && adx < tw->distance.x)))
          break;
        return TRUE;
      case 2:  // RIGHT
        if (dx > 0 && ady <= adx &&
            (adx < tw->distance.x ||
             (adx == tw->distance.x && ady < tw->distance.y)))
          break;
        return TRUE;
      case 3:  // DOWN
        if (dy > 0 && adx <= ady &&
            (ady < tw->distance.y ||
             (ady == tw->distance.y && adx < tw->distance.x)))
          break;
        return TRUE;
      default:  // WTF?
        std::unreachable();
    }
  } else {
    // Square mode (not all space is covered)
    long beamW = std::min(tw->oheight, tw->owidth);
    switch (tw->direction) {
      case 0:  // LEFT
        if (dx < 0 && ady < beamW &&
            (adx < tw->distance.x ||
             (adx == tw->distance.x && ady < tw->distance.y)))
          break;      // Window is closer...
        return TRUE;  // skip
      case 1:         // UP
        if (dy < 0 && adx < beamW &&
            (ady < tw->distance.y ||
             (ady == tw->distance.y && adx < tw->distance.x)))
          break;
        return TRUE;
      case 2:  // RIGHT
        if (dx > 0 && ady < beamW &&
            (adx < tw->distance.x ||
             (adx == tw->distance.x && ady < tw->distance.y)))
          break;
        return TRUE;
      case 3:  // DOWN
        if (dy > 0 && adx < beamW &&
            (ady < tw->distance.y ||
             (ady == tw->distance.y && adx < tw->distance.x)))
          break;
        return TRUE;
      default:  // WTF?
        std::unreachable();
    }
  }
  // Update tw struct if we find a closer window.
  tw->distance.x = adx;
  tw->distance.y = ady;
  tw->ohwnd = hwnd;

  return TRUE;
}
HWND FindTiledWindow(HWND hwnd, unsigned char direction) {
  assert(direction < 4);

  RECT rc;
  if (GetWindowRectL(hwnd, &rc)) {
    struct FindTiledWindow_struct tw;
    long w = (rc.right - rc.left) / 2;
    long h = (rc.bottom - rc.top) / 2;
    tw.opt.x = (rc.left + rc.right) / 2;
    tw.opt.y = (rc.top + rc.bottom) / 2;
    tw.owidth = w;
    tw.oheight = h;
    tw.ihwnd = hwnd;
    tw.ohwnd = nullptr;
    tw.distance.x = 0x7ffffff0;
    tw.distance.y = 0x7ffffff0;
    tw.direction = direction;
    tw.diagonal = 0;

    EnumDesktopWindows(nullptr, FindTiledWindowEnumProc,
                       reinterpret_cast<LPARAM>(&tw));
    if (!tw.ohwnd) {  // Try again with diagonals
      tw.diagonal = 1;
      EnumDesktopWindows(nullptr, FindTiledWindowEnumProc,
                         reinterpret_cast<LPARAM>(&tw));
    }
    //        // Last resort, try with larger and larger beam...
    //        tw.diagonal = 0;
    //        int i;
    //        for (i=2; !tw.ohwnd && i<5; i++) {
    //            tw.oheight = i*h;
    //            tw.owidth  = i*h;
    //            EnumDesktopWindows(NULL, FindTiledWindowEnumProc,
    //            (LPARAM)&tw);
    //        }
    //        // TODO: Handle MDI clients.
    //        if (state.mdiclient) {
    //            EnumChildWindows(state.mdiclient, FindTiledWindowEnumProc,
    //            (LPARAM)&tw);
    //        } else {
    //            EnumDesktopWindows(NULL, FindTiledWindowEnumProc,
    //            (LPARAM)&tw);
    //        }
    // Log hwnd details (usefull for debugging)
    //        wchar_t buf[1024]=L"";
    //        PrintHwndDetails(tw.ohwnd, buf);
    //        static const char *directionStr="LTRB";
    //        LOGA("FOCUSING_%c %ls", directionStr[direction], buf);

    return tw.ohwnd;
  }
  return nullptr;
}

}  // namespace altsnap::hooks
