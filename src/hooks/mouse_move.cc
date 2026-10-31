// mouse move (split out of hooks.cc).

#include "hooks/mouse_move.h"

#include <algorithm>

#include "hooks/hooks_internal.h"
#include "hooks/input_state.h"
#include "hooks/mouse_hook.h"
#include "hooks/overlay/trans_window.h"
#include "hooks/window/snap.h"
#include "hooks/window/snap_move.h"
#include "hooks/window/window_enum.h"
#include "hooks/window/window_move.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

void MouseMove(POINT pt) {
  if (!g_InFinishMovement) (void)PostWorkerMouseMove(pt);
}
// Where a move or resize step wants to put the window.
struct TargetRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// A maximized or snapped window that starts to move is restored first, keeping
// the size it had before it was snapped.
static void RestoreBeforeMove(TargetRect& target) {
  LastWin.moveonly = 0;
  WINDOWPLACEMENT wndpl;
  wndpl.length = sizeof(wndpl);
  GetWindowPlacement(state.hwnd, &wndpl);
  // Restore original width and height in case we are restoring
  // A Snapped + Maximized window.
  wndpl.showCmd = SW_RESTORE;
  const unsigned restore = GetRestoreFlag(state.hwnd);
  if (!(restore & ROLLED)) {  // Not if window is rolled!
    wndpl.rcNormalPosition.left = target.x;
    wndpl.rcNormalPosition.top = target.y;
    wndpl.rcNormalPosition.right =
        wndpl.rcNormalPosition.left + state.origin.width;
    wndpl.rcNormalPosition.bottom =
        wndpl.rcNormalPosition.top + state.origin.height;
  }
  if (restore & SNTHENROLLED) ClearRestoreData(state.hwnd);  // Only Flag?
  // Update the target size.
  target.width = wndpl.rcNormalPosition.right - wndpl.rcNormalPosition.left;
  target.height = wndpl.rcNormalPosition.bottom - wndpl.rcNormalPosition.top;
  SetWindowPlacement(state.hwnd, &wndpl);
  RecalcInvisibleBorders();
}

// Computes the new position of a window that is being moved. Returns false if
// the move was handled entirely by an Aero snap (nothing left to do).
static bool PlanMove(POINT pt, const RECT& wnd, bool was_snapped,
                     TargetRect& target) {
  LastWin.moveonly = state.moving;  // SWP_NOSIZE to SetWindowPos
  target.x = pt.x - state.offset.x;
  target.y = pt.y - state.offset.y;
  target.width = wnd.right - wnd.left;
  target.height = wnd.bottom - wnd.top;

  // Check if the window will snap anywhere
  if (AeroMoveSnap(pt, &target.x, &target.y, &target.width, &target.height) ==
      1) {
    state.moving = 1;
    return false;
  }
  MoveSnap(&target.x, &target.y, target.width, target.height,
           conf.SnapThreshold);
  MoveSnapToZone(pt, &target.x, &target.y, &target.width, &target.height);

  // Restore window if maximized when starting
  if (!LastWin.snap && (was_snapped || IsZoomed(state.hwnd))) {
    RestoreBeforeMove(target);
  }
  return true;
}

// Resizing from the center of the window: the window grows symmetrically.
static void PlanCenteredResize(POINT pt, const RECT& wnd, TargetRect& target) {
  if (state.shift)
    pt.x = state.shiftpt.x;
  else if (IsCtrlDown())
    pt.y = state.ctrlpt.y;
  target.width = wnd.right - wnd.left + 2 * (pt.x - state.offset.x);
  target.height = wnd.bottom - wnd.top + 2 * (pt.y - state.offset.y);
  target.x = wnd.left - (pt.x - state.offset.x) - state.mdipt.x;
  target.y = wnd.top - (pt.y - state.offset.y) - state.mdipt.y;

  // Keep the window perfectly centered.
  // even when going out of min or max sizes
  const int width = ClampWidth(target.width);
  target.x += (target.width - width) / 2;
  target.width = width;
  const int height = ClampHeight(target.height);
  target.y += (target.height - height) / 2;
  target.height = height;

  state.offset.x = pt.x;
  state.offset.y = pt.y;
}

// Resizing by one or two edges.
static void PlanEdgeResize(POINT pt, const RECT& wnd, TargetRect& target) {
  if (state.resize.y == RZ_TOP) {
    target.height =
        ClampHeight((wnd.bottom - pt.y + state.offset.y) - state.mdipt.y);
    target.y = state.origin.bottom - target.height;
  } else if (state.resize.y == RZ_YCENTER) {
    target.y = wnd.top - state.mdipt.y;
    target.height = wnd.bottom - wnd.top;
  } else if (state.resize.y == RZ_BOTTOM) {
    target.y = wnd.top - state.mdipt.y;
    target.height = pt.y - target.y + state.offset.y;
  }
  if (state.resize.x == RZ_LEFT) {
    target.width =
        ClampWidth((wnd.right - pt.x + state.offset.x) - state.mdipt.x);
    target.x = state.origin.right - target.width;
  } else if (state.resize.x == RZ_XCENTER) {
    target.x = wnd.left - state.mdipt.x;
    target.width = wnd.right - wnd.left;
  } else if (state.resize.x == RZ_RIGHT) {
    target.x = wnd.left - state.mdipt.x;
    target.width = pt.x - target.x + state.offset.x;
  }
  target.width = ClampWidth(target.width);
  target.height = ClampHeight(target.height);

  // Check if the window will snap anywhere
  ResizeSnap(&target.x, &target.y, &target.width, &target.height,
             conf.SnapThreshold, conf.SnapThreshold);
  AeroResizeSnap(pt, &target.x, &target.y, &target.width, &target.height);
}

static void PlanResize(POINT pt, RECT& wnd, TargetRect& target) {
  // Restore the window (to monitor size) if it's maximized
  if (!state.moving && IsZoomed(state.hwnd)) {
    RestoreToMonitorSize(state.hwnd, &wnd);
  }
  // Clear restore flag if needed
  ClearRestoreFlagOnResizeIfNeeded(state.hwnd);
  // Figure out new placement
  if (state.resize.x == RZ_XCENTER && state.resize.y == RZ_YCENTER) {
    PlanCenteredResize(pt, wnd, target);
  } else {
    PlanEdgeResize(pt, wnd, target);
  }
}

// Records `target` in LastWin and performs (or previews) the move/resize.
static void ApplyTarget(const TargetRect& target, RECT& wnd) {
  // LastWin is GLOBAL !
  LastWin.x = target.x;
  LastWin.y = target.y;
  LastWin.width = target.width;
  LastWin.height = target.height;
  LastWin.resize = state.resize;

  // Update the static wnd with new dimentions.
  wnd.left = target.x + state.mdipt.x;
  wnd.top = target.y + state.mdipt.y;
  wnd.right = target.x + state.mdipt.x + target.width;
  wnd.bottom = target.y + state.mdipt.y + target.height;

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

void MouseMoveNow(POINT pt) {
  if (g_InFinishMovement) {  // skip
    return;
  }

  // Check if window still exists
  if (!IsWindow(state.hwnd)) {
    LastWin.hwnd = nullptr;
    UnhookMouse();
    return;
  }

  // Ensure the point is in its clip rectangle (Windows bug)
  RECT clip = {};
  if (GetClipCursor(&clip)) {
    pt.x = std::clamp(pt.x, clip.left, clip.right - 1);
    pt.y = std::clamp(pt.y, clip.top, clip.bottom - 1);
  }

  if (conf.UseCursor)  // Draw the invisible cursor window
    MoveWindow(g_mainhwnd, pt.x - 128, pt.y - 128, 256, 256, FALSE);

  if (state.moving == CURSOR_ONLY) {
    if (state.action.ac == AC_RESIZE) {
      RECT rc = {};
      GetWindowRect(state.hwnd, &rc);
      SetEdgeAndOffset(&rc, pt);
      UpdateCursor(pt);
    }
    return;  // Movement was blocked...
  }

  static RECT wnd;  // Target movement rect, kept between calls.
  bool was_snapped = false;
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

  LastWin.odpi = state.origin.dpi;
  LastWin.moveonly = 0;

  // Convert pt in MDI coordinates.
  // state.mdipt is global!
  pt.x -= state.mdipt.x;
  pt.y -= state.mdipt.y;

  RestrictCursorToMon();  // When CTRL is pressed.

  TargetRect target;
  if (state.action.ac == AC_MOVE) {
    if (!PlanMove(pt, wnd, was_snapped, target)) return;
  } else if (state.action.ac == AC_RESIZE) {
    PlanResize(pt, wnd, target);
  }
  ApplyTarget(target, wnd);
}

}  // namespace altsnap::hooks
