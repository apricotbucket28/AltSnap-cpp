// mouse move (split out of hooks.cc).

#include "mouse_move.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "blacklist.h"
#include "hooks_internal.h"
#include "ini_section.h"
#include "input_state.h"
#include "mouse_hook.h"
#include "snap.h"
#include "snap_move.h"
#include "trans_window.h"
#include "window_enum.h"
#include "window_move.h"
#include "zones.h"

namespace altsnap::hooks {

void MouseMove(POINT pt) {
  if (!g_InFinishMovement)
    PostThreadMessage(g_WorkerThreadID, WM_DOMOUSEMOVE, pt.x, pt.y);
}
void MouseMoveNow(POINT pt) {
  // LOG("MouseMove(%d,%d)", pt.x, pt.y);
  if (g_InFinishMovement) return;  // Skip...

  // Check if window still exists
  if (!IsWindow(state.hwnd)) {
    LastWin.hwnd = nullptr;
    UnhookMouse();
    return;
  }

  // Ensure the point is in its clip rectangle (Windows bug)
  RECT clip;
  if (GetClipCursor(&clip)) {
    pt.x = CLAMP(clip.left, pt.x, clip.right - 1);
    pt.y = CLAMP(clip.top, pt.y, clip.bottom - 1);
    // if(!PtInRect(&clip, pt))
    //   LOGA("WTF pt=%d,%d not in ClipRect=%d,%d,%d,%d", pt.x, pt.y, clip.left,
    //   clip.top, clip.right, clip.bottom);
  }

  if (conf.UseCursor)  // Draw the invisible cursor window
    MoveWindow(g_mainhwnd, pt.x - 128, pt.y - 128, 256, 256, FALSE);

  if (state.moving == CURSOR_ONLY) {
    if (state.action.ac == AC_RESIZE) {
      RECT rc;
      GetWindowRect(state.hwnd, &rc);
      SetEdgeAndOffset(&rc, pt);
      UpdateCursor(pt);
    }
    return;  // Movement was blocked...
  }

  static RECT wnd;  // Targect movement rect.
  UCHAR was_snapped = 0;
  if (!state.moving) {
    // First initialize wnd with the actual Window rect
    if (!GetWindowRect(state.hwnd, &wnd)) return;

    // Restore Aero snapped window when MOVE starts
    SetOriginFromRestoreData(state.hwnd, state.action);
    if (state.action.ac == AC_MOVE) {
      was_snapped = IsWindowSnapped(state.hwnd);
      RestoreOldWin(pt, was_snapped, &wnd);
    }
  }
  int posx = 0, posy = 0, wndwidth = 0, wndheight = 0;

  // ZeroObject(LastWin);
  LastWin.odpi = state.origin.dpi;
  LastWin.moveonly = 0;

  // Convert pt in MDI coordinates.
  // state.mdipt is global!
  pt.x -= state.mdipt.x;
  pt.y -= state.mdipt.y;

  RestrictCursorToMon();  // When CTRL is pressed.

  // Get new position for window
  if (state.action.ac == AC_MOVE) {
    // SWP_NOSIZE to SetWindowPos
    LastWin.moveonly = state.moving;
    posx = pt.x - state.offset.x;
    posy = pt.y - state.offset.y;
    wndwidth = wnd.right - wnd.left;
    wndheight = wnd.bottom - wnd.top;

    // Check if the window will snap anywhere
    int ret = AeroMoveSnap(pt, &posx, &posy, &wndwidth, &wndheight);
    if (ret == 1) {
      state.moving = 1;
      return;
    }
    MoveSnap(&posx, &posy, wndwidth, wndheight, conf.SnapThreshold);
    MoveSnapToZone(pt, &posx, &posy, &wndwidth, &wndheight);

    // Restore window if maximized when starting
    if (!LastWin.snap && (was_snapped || IsZoomed(state.hwnd))) {
      LastWin.moveonly = 0;
      WINDOWPLACEMENT wndpl;
      wndpl.length = sizeof(wndpl);
      GetWindowPlacement(state.hwnd, &wndpl);
      // Restore original width and height in case we are restoring
      // A Snapped + Maximized window.
      wndpl.showCmd = SW_RESTORE;
      unsigned restore = GetRestoreFlag(state.hwnd);
      if (!(restore & ROLLED)) {  // Not if window is rolled!
        wndpl.rcNormalPosition.left = posx;
        wndpl.rcNormalPosition.top = posy;
        wndpl.rcNormalPosition.right =
            wndpl.rcNormalPosition.left + state.origin.width;
        wndpl.rcNormalPosition.bottom =
            wndpl.rcNormalPosition.top + state.origin.height;
      }
      if (restore & SNTHENROLLED) ClearRestoreData(state.hwnd);  // Only Flag?
      // Update wndwidth and wndheight
      wndwidth = wndpl.rcNormalPosition.right - wndpl.rcNormalPosition.left;
      wndheight = wndpl.rcNormalPosition.bottom - wndpl.rcNormalPosition.top;
      SetWindowPlacement(state.hwnd, &wndpl);
      RecalcInvisibleBorders();
    }

  } else if (state.action.ac == AC_RESIZE) {
    // Restore the window (to monitor size) if it's maximized
    if (!state.moving && IsZoomed(state.hwnd)) {
      RestoreToMonitorSize(state.hwnd, &wnd);
    }
    // Clear restore flag if needed
    ClearRestoreFlagOnResizeIfNeeded(state.hwnd);
    // Figure out new placement
    if (state.resize.x == RZ_XCENTER && state.resize.y == RZ_YCENTER) {
      if (state.shift)
        pt.x = state.shiftpt.x;
      else if (IsCtrlDown())
        pt.y = state.ctrlpt.y;
      wndwidth = wnd.right - wnd.left + 2 * (pt.x - state.offset.x);
      wndheight = wnd.bottom - wnd.top + 2 * (pt.y - state.offset.y);
      posx = wnd.left - (pt.x - state.offset.x) - state.mdipt.x;
      posy = wnd.top - (pt.y - state.offset.y) - state.mdipt.y;

      // Keep the window perfectly centered.
      // even when going out of min or max sizes
      int W = CLAMPW(wndwidth);
      int dW = wndwidth - W;
      posx += dW / 2;
      wndwidth = W;
      int H = CLAMPH(wndheight);
      int dH = wndheight - H;
      posy += dH / 2;
      wndheight = H;

      state.offset.x = pt.x;
      state.offset.y = pt.y;

    } else {
      if (state.resize.y == RZ_TOP) {
        wndheight =
            CLAMPH((wnd.bottom - pt.y + state.offset.y) - state.mdipt.y);
        posy = state.origin.bottom - wndheight;
      } else if (state.resize.y == RZ_YCENTER) {
        posy = wnd.top - state.mdipt.y;
        wndheight = wnd.bottom - wnd.top;
      } else if (state.resize.y == RZ_BOTTOM) {
        posy = wnd.top - state.mdipt.y;
        wndheight = pt.y - posy + state.offset.y;
      }
      if (state.resize.x == RZ_LEFT) {
        wndwidth = CLAMPW((wnd.right - pt.x + state.offset.x) - state.mdipt.x);
        posx = state.origin.right - wndwidth;
      } else if (state.resize.x == RZ_XCENTER) {
        posx = wnd.left - state.mdipt.x;
        wndwidth = wnd.right - wnd.left;
      } else if (state.resize.x == RZ_RIGHT) {
        posx = wnd.left - state.mdipt.x;
        wndwidth = pt.x - posx + state.offset.x;
      }
      wndwidth = CLAMPW(wndwidth);
      wndheight = CLAMPH(wndheight);

      // Check if the window will snap anywhere
      ResizeSnap(&posx, &posy, &wndwidth, &wndheight, conf.SnapThreshold,
                 conf.SnapThreshold);
      AeroResizeSnap(pt, &posx, &posy, &wndwidth, &wndheight);
    }
  }
  // LastWin is GLOBAL !
  LastWin.x = posx;
  LastWin.y = posy;
  LastWin.width = wndwidth;
  LastWin.height = wndheight;
  LastWin.resize = state.resize;

  // Update the static wnd with new dimentions.
  wnd.left = posx + state.mdipt.x;
  wnd.top = posy + state.mdipt.y;
  wnd.right = posx + state.mdipt.x + wndwidth;
  wnd.bottom = posy + state.mdipt.y + wndheight;

  // Save hwnd After we are sure movement will occur.
  LastWin.hwnd = state.hwnd;

  if (!conf.FullWin) {
    MoveTransWin(&LastWin);
    if (!IsTransWinVisible()) ShowTransWin(SW_SHOWNA);
    state.moving = 1;
    ResizeTouchingWindows(&LastWin);

  } else {
    // Resize other windows
    if (!ResizeTouchingWindows(&LastWin)) {
      // The resize touching also resizes LastWin.
      MoveWindowNow(&LastWin);
    }
    state.moving = 1;
  }
}

}  // namespace altsnap::hooks
