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
  if (!Session().finish_pending)
    (void)PostWorkerCommand(MouseMoveCommand{pt, Session().gesture});
}
namespace {
// USER calls can dispatch nested hooks even though every access is on the
// owner thread. A cancelled/replaced/locked step must not resume applying data.
class MovementStep final {
 public:
  MovementStep()
      : gesture_(Session().gesture),
        window_(Session().input.hwnd),
        action_(Session().input.action.ac),
        moving_(Session().input.moving) {}
  bool SameDrag() const {
    return Session().gesture == gesture_ && Session().input.hwnd == window_ &&
           Session().input.action.ac == action_;
  }
  bool Current() const {
    return SameDrag() && Session().input.moving == moving_;
  }

 private:
  uint64_t gesture_;
  HWND window_;
  BYTE action_;
  UCHAR moving_;
};
}  // namespace

// Where a move or resize step wants to put the window.
struct TargetRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// A maximized or snapped window that starts to move is restored first, keeping
// the size it had before it was snapped.
static bool RestoreBeforeMove(TargetRect& target, const MovementStep& step) {
  Session().last_window.moveonly = 0;
  WINDOWPLACEMENT wndpl;
  wndpl.length = sizeof(wndpl);
  GetWindowPlacement(Session().input.hwnd, &wndpl);
  // Restore original width and height in case we are restoring
  // A Snapped + Maximized window.
  wndpl.showCmd = SW_RESTORE;
  const unsigned restore = GetRestoreFlag(Session().input.hwnd);
  if (!(restore & ROLLED)) {  // Not if window is rolled!
    wndpl.rcNormalPosition.left = target.x;
    wndpl.rcNormalPosition.top = target.y;
    wndpl.rcNormalPosition.right =
        wndpl.rcNormalPosition.left + Session().input.origin.width;
    wndpl.rcNormalPosition.bottom =
        wndpl.rcNormalPosition.top + Session().input.origin.height;
  }
  if (restore & SNTHENROLLED)
    ClearRestoreData(Session().input.hwnd);  // Only Flag?
  // Update the target size.
  target.width = wndpl.rcNormalPosition.right - wndpl.rcNormalPosition.left;
  target.height = wndpl.rcNormalPosition.bottom - wndpl.rcNormalPosition.top;
  SetWindowPlacement(Session().input.hwnd, &wndpl);
  if (!step.Current()) return false;
  RecalcInvisibleBorders();
  return step.Current();
}

// Computes the new position of a window that is being moved. Returns false if
// the move was handled entirely by an Aero snap (nothing left to do).
static bool PlanMove(POINT pt, const RECT& wnd, bool was_snapped,
                     TargetRect& target, const MovementStep& step) {
  Session().last_window.moveonly =
      Session().input.moving;  // SWP_NOSIZE to SetWindowPos
  target.x = pt.x - Session().input.offset.x;
  target.y = pt.y - Session().input.offset.y;
  target.width = wnd.right - wnd.left;
  target.height = wnd.bottom - wnd.top;

  // Check if the window will snap anywhere
  if (AeroMoveSnap(pt, &target.x, &target.y, &target.width, &target.height) ==
      1) {
    if (step.SameDrag() && Session().input.moving != CURSOR_ONLY)
      Session().input.moving = 1;
    return false;
  }
  if (!step.Current()) return false;
  MoveSnap(&target.x, &target.y, target.width, target.height,
           conf.SnapThreshold);
  if (!step.Current()) return false;
  MoveSnapToZone(pt, &target.x, &target.y, &target.width, &target.height);

  if (!step.Current()) return false;
  // Restore window if maximized when starting
  if (!Session().last_window.snap &&
      (was_snapped || IsZoomed(Session().input.hwnd))) {
    if (!RestoreBeforeMove(target, step)) return false;
  }
  return true;
}

// Resizing from the center of the window: the window grows symmetrically.
static void PlanCenteredResize(POINT pt, const RECT& wnd, TargetRect& target) {
  if (Session().input.shift)
    pt.x = Session().input.shiftpt.x;
  else if (IsCtrlDown())
    pt.y = Session().input.ctrlpt.y;
  target.width = wnd.right - wnd.left + 2 * (pt.x - Session().input.offset.x);
  target.height = wnd.bottom - wnd.top + 2 * (pt.y - Session().input.offset.y);
  target.x =
      wnd.left - (pt.x - Session().input.offset.x) - Session().input.mdipt.x;
  target.y =
      wnd.top - (pt.y - Session().input.offset.y) - Session().input.mdipt.y;

  // Keep the window perfectly centered.
  // even when going out of min or max sizes
  const int width = ClampWidth(target.width);
  target.x += (target.width - width) / 2;
  target.width = width;
  const int height = ClampHeight(target.height);
  target.y += (target.height - height) / 2;
  target.height = height;

  Session().input.offset.x = pt.x;
  Session().input.offset.y = pt.y;
}

// Resizing by one or two edges.
static void PlanEdgeResize(POINT pt, const RECT& wnd, TargetRect& target,
                           const MovementStep& step) {
  if (Session().input.resize.y == RZ_TOP) {
    target.height = ClampHeight((wnd.bottom - pt.y + Session().input.offset.y) -
                                Session().input.mdipt.y);
    target.y = Session().input.origin.bottom - target.height;
  } else if (Session().input.resize.y == RZ_YCENTER) {
    target.y = wnd.top - Session().input.mdipt.y;
    target.height = wnd.bottom - wnd.top;
  } else if (Session().input.resize.y == RZ_BOTTOM) {
    target.y = wnd.top - Session().input.mdipt.y;
    target.height = pt.y - target.y + Session().input.offset.y;
  }
  if (Session().input.resize.x == RZ_LEFT) {
    target.width = ClampWidth((wnd.right - pt.x + Session().input.offset.x) -
                              Session().input.mdipt.x);
    target.x = Session().input.origin.right - target.width;
  } else if (Session().input.resize.x == RZ_XCENTER) {
    target.x = wnd.left - Session().input.mdipt.x;
    target.width = wnd.right - wnd.left;
  } else if (Session().input.resize.x == RZ_RIGHT) {
    target.x = wnd.left - Session().input.mdipt.x;
    target.width = pt.x - target.x + Session().input.offset.x;
  }
  target.width = ClampWidth(target.width);
  target.height = ClampHeight(target.height);

  // Check if the window will snap anywhere
  ResizeSnap(&target.x, &target.y, &target.width, &target.height,
             conf.SnapThreshold, conf.SnapThreshold);
  if (!step.Current()) return;
  AeroResizeSnap(pt, &target.x, &target.y, &target.width, &target.height);
}

static bool PlanResize(POINT pt, RECT& wnd, TargetRect& target,
                       const MovementStep& step) {
  // Restore the window (to monitor size) if it's maximized
  if (!Session().input.moving && IsZoomed(Session().input.hwnd)) {
    RestoreToMonitorSize(Session().input.hwnd, &wnd);
  }
  if (!step.Current()) return false;
  // Clear restore flag if needed
  ClearRestoreFlagOnResizeIfNeeded(Session().input.hwnd);
  // Figure out new placement
  if (Session().input.resize.x == RZ_XCENTER &&
      Session().input.resize.y == RZ_YCENTER) {
    PlanCenteredResize(pt, wnd, target);
  } else {
    PlanEdgeResize(pt, wnd, target, step);
  }
  return step.Current();
}

// Records `target` in LastWin and performs (or previews) the move/resize.
static void ApplyTarget(const TargetRect& target, RECT& wnd,
                        const MovementStep& step) {
  Session().last_window.x = target.x;
  Session().last_window.y = target.y;
  Session().last_window.width = target.width;
  Session().last_window.height = target.height;
  Session().last_window.resize = Session().input.resize;

  // Update the static wnd with new dimentions.
  wnd.left = target.x + Session().input.mdipt.x;
  wnd.top = target.y + Session().input.mdipt.y;
  wnd.right = target.x + Session().input.mdipt.x + target.width;
  wnd.bottom = target.y + Session().input.mdipt.y + target.height;

  // Save hwnd After we are sure movement will occur.
  Session().last_window.hwnd = Session().input.hwnd;

  if (!conf.FullWin) {
    MoveTransWin(&Session().last_window);
    if (!step.Current()) return;
    if (!IsTransWinVisible()) ShowTransWin(SW_SHOWNA);
    if (!step.Current()) return;
    ResizeTouchingWindows(&Session().last_window);
    if (!step.Current()) return;
    Session().input.moving = 1;
  } else {
    // Resize other windows
    const bool resized = ResizeTouchingWindows(&Session().last_window) != 0;
    if (!step.Current()) return;
    if (!resized) {
      // Keep a nested cancellation from invalidating an in-flight request.
      windowRR request = Session().last_window;
      MoveWindowNow(&request);
      if (!step.Current()) return;
      Session().last_window = request;
    }
    Session().input.moving = 1;
  }
}

void MouseMoveNow(POINT pt) {
  if (!IsMovementAction(Session().input.action)) return;
  const MovementStep step;
  // Check if window still exists
  if (!IsWindow(Session().input.hwnd)) {
    Session().last_window.hwnd = nullptr;
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

  if (!step.Current()) return;
  if (Session().input.moving == CURSOR_ONLY) {
    if (Session().input.action.ac == AC_RESIZE) {
      RECT rc = {};
      GetWindowRect(Session().input.hwnd, &rc);
      if (!step.Current()) return;
      SetEdgeAndOffset(&rc, pt);
      UpdateCursor(pt);
    }
    return;  // Movement was blocked...
  }

  RECT& wnd = Session().movement_rect;
  bool was_snapped = false;
  if (!Session().input.moving) {
    // First initialize wnd with the actual Window rect
    if (!GetWindowRect(Session().input.hwnd, &wnd)) return;

    if (!step.Current()) return;
    // Restore Aero snapped window when MOVE starts
    SetOriginFromRestoreData(Session().input.hwnd, Session().input.action);
    if (Session().input.action.ac == AC_MOVE) {
      was_snapped = IsWindowSnapped(Session().input.hwnd);
      RestoreOldWin(pt, was_snapped, &wnd);
    }
  }

  if (!step.Current()) return;
  Session().last_window.odpi = Session().input.origin.dpi;
  Session().last_window.moveonly = 0;

  // Convert pt in MDI coordinates.
  // state.mdipt is global!
  pt.x -= Session().input.mdipt.x;
  pt.y -= Session().input.mdipt.y;

  RestrictCursorToMon();  // When CTRL is pressed.

  if (!step.Current()) return;
  TargetRect target{};
  if (Session().input.action.ac == AC_MOVE) {
    if (!PlanMove(pt, wnd, was_snapped, target, step)) return;
  } else if (Session().input.action.ac == AC_RESIZE) {
    if (!PlanResize(pt, wnd, target, step)) return;
  } else {
    return;
  }
  if (!step.Current()) return;
  ApplyTarget(target, wnd, step);
}

}  // namespace altsnap::hooks
