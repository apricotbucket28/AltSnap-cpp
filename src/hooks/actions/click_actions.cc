// click actions (split out of hooks.cc).

#include "hooks/actions/click_actions.h"

#include <algorithm>
#include <array>
#include <vector>

#include "hooks/actions/volume.h"
#include "hooks/actions/window_actions.h"
#include "hooks/blacklist.h"
#include "hooks/hooks_internal.h"
#include "hooks/input_state.h"
#include "hooks/movement.h"
#include "hooks/overlay/pin_window.h"
#include "hooks/overlay/window_menu.h"
#include "hooks/send_input.h"
#include "hooks/window/snap_move.h"
#include "hooks/window/window_enum.h"
#include "hooks/window/window_info.h"
#include "hooks/window/window_move.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

// Single click commands
void SClickActions(HWND hwnd, action_t action) {
  static const resizexy_t rxy_map[] = {
      {RZ_LEFT, RZ_YCENTER},
      {RZ_XCENTER, RZ_TOP},
      {RZ_RIGHT, RZ_YCENTER},
      {RZ_XCENTER, RZ_BOTTOM},
  };
  Session().input.sactiondone = action;
  LOG("Going to perform action (%d)_%d_%d", static_cast<int>(action.ac),
      static_cast<int>(action.fl), static_cast<int>(action.wp));
  switch (action.ac) {
    case AC_MINIMIZE:
      MinimizeWindow(hwnd);
      break;
    case AC_MAXIMIZE:
      ActionMaximize(hwnd);
      break;
    case AC_CENTER:
      CenterWindow(
          hwnd, !Session().input.shift /*Session().input.shift? 0: CW_RESTORE*/,
          0);
      break;
    case AC_CENTER2:
      CenterWindow(hwnd, !Session().input.shift, /*full*/ 1);
      break;
    case AC_ALWAYSONTOP:
      TogglesAlwaysOnTop(hwnd);
      break;
    case AC_CLOSE:
      PostMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
      break;
    case AC_TRANSPARENCY:
      ActionTransparency2(hwnd, action);
      break;
    case AC_LOWER:
      ActionLower(hwnd, 0, Session().input.shift, IsCtrlDown());
      break;
    case AC_FOCUS:
      if (action.fl == 0) {
        ActionLower(hwnd, +120, Session().input.shift, 1);
      } else if (action.fl <= 4) {
        ReallySetForegroundWindow(FindTiledWindow(hwnd, action.fl - 1));
      }
      break;
    case AC_BORDERLESS:
      ActionBorderless(hwnd);
      break;
    case AC_KILL:
      ActionKill(hwnd);
      break;
    case AC_PAUSE:
      ActionPause(hwnd, 1);
      break;
    case AC_RESUME:
      ActionPause(hwnd, 0);
      break;
    case AC_ROLL:
      RollWindow(hwnd, action.fl == ACFL_UP     ? +120
                       : action.fl == ACFL_DOWN ? -120
                                                : 0);
      break;
    case AC_MAXHV:
      MaximizeHV(hwnd, Session().input.shift);
      break;
    case AC_MINALL:
      MinimizeAllOtherWindows(hwnd, Session().input.shift);
      break;
    case AC_MUTE:
      SendKey(VK_VOLUME_MUTE);
      break;
    case AC_SIDESNAP:
      SnapToCorner(hwnd, AUTORESIZE, !!Session().input.shift);
      break;
    case AC_EXTENDSNAP:
      SnapToCorner(hwnd, AUTORESIZE, !Session().input.shift);
      break;
    case AC_EXTENDTNEDGE:
      if (!action.fl)
        SnapToCorner(hwnd, AUTORESIZE,
                     Session().input.shift ? SNTO_MOVETO | SNTO_NEXTBD
                                           : SNTO_EXTEND | SNTO_NEXTBD);
      else
        SnapToCorner(hwnd, rxy_map[std::min(action.fl - 1, 3)],
                     SNTO_EXTEND | SNTO_NEXTBD);
      break;
    case AC_MOVETNEDGE:
      if (!action.fl)
        SnapToCorner(hwnd, AUTORESIZE,
                     Session().input.shift ? SNTO_EXTEND | SNTO_NEXTBD
                                           : SNTO_MOVETO | SNTO_NEXTBD);
      else
        SnapToCorner(hwnd, rxy_map[std::min(action.fl - 1, 3)],
                     SNTO_MOVETO | SNTO_NEXTBD);
      break;
    case AC_MENU:
      ActionMenu(hwnd);
      break;
    case AC_NSTACKED:
      ActionAltTab(Session().input.prevpt, +120, Session().input.shift,
                   EnumStackedWindowsProc);
      break;
    case AC_NSTACKED2:
      ActionAltTab(Session().input.prevpt, +120, !Session().input.shift,
                   EnumStackedWindowsProc);
      break;
    case AC_PSTACKED:
      ActionAltTab(Session().input.prevpt, -120, Session().input.shift,
                   EnumStackedWindowsProc);
      break;
    case AC_PSTACKED2:
      ActionAltTab(Session().input.prevpt, -120, !Session().input.shift,
                   EnumStackedWindowsProc);
      break;
    case AC_STACKLIST:
      ActionStackList(Session().input.shift ? TRK_LASERMODE : 0);
      break;
    case AC_STACKLIST2:
      ActionStackList(Session().input.shift ? 0 : TRK_LASERMODE);
      break;
    case AC_ALTTABLIST:
      PostMessage(g_mainhwnd, WM_STACKLIST, TRK_MOVETOMONITOR | TRK_LASERMODE,
                  Session().input.shift
                      ? reinterpret_cast<LPARAM>(EnumAllAltTabWindows)
                      : reinterpret_cast<LPARAM>(EnumAltTabWindows));
      break;
    case AC_ALTTABFULLLIST:
      PostMessage(g_mainhwnd, WM_STACKLIST, TRK_MOVETOMONITOR | TRK_LASERMODE,
                  Session().input.shift
                      ? reinterpret_cast<LPARAM>(EnumAltTabWindows)
                      : reinterpret_cast<LPARAM>(EnumAllAltTabWindows));
      break;

    case AC_MZONE:
      MoveWindowToTouchingZone(hwnd, action.fl - 1, 0);
      break;
    case AC_XZONE:
      MoveWindowToTouchingZone(hwnd, action.fl - 1, 1);
      break;
    case AC_STEP:
      StepWindow(hwnd, action);
      break;

    case AC_NLAYOUT:
      PostMessage(g_mainhwnd, WM_COMMAND,
                  CMD_SNAPLAYOUT + (CurrentLayout() + 1) % conf.MaxLayouts, 0);
      break;
    case AC_PLAYOUT:
      PostMessage(g_mainhwnd, WM_COMMAND,
                  CMD_SNAPLAYOUT +
                      (CurrentLayout() + conf.MaxLayouts - 1) % conf.MaxLayouts,
                  0);
      break;

    case AC_ASONOFF:
      ActionASOnOff();
      break;
    case AC_MOVEONOFF:
      ActionMoveOnOff(hwnd);
      break;
    case AC_SHRT:
      if (action.fl < g_inputSequences.size() &&
          !g_inputSequences[action.fl].empty()) {
        SendInputSequence(g_inputSequences[action.fl].data());
      }
      break;
  }
}

int DoWheelActions(HWND hwnd, action_t action) {
  Session().input.sactiondone = action;
  // Return if in the scroll blacklist.
  if (IsBlacklisted(hwnd, BlkLst.Scroll)) {
    return 0;  // Next hook!
  }
  int ret = 1;
  switch (action.ac) {
    case AC_ALTTAB:
      ActionAltTab(
          Session().input.prevpt, Session().input.delta, /*laser=0*/ 0,
          Session().input.shift ? EnumStackedWindowsProc : EnumAltTabWindows);
      break;
    case AC_VOLUME:
      ActionVolume(Session().input.delta);
      break;
    case AC_TRANSPARENCY:
      ret = ActionTransparency(hwnd, Session().input.delta);
      break;
    case AC_LOWER:
      ActionLower(hwnd, Session().input.delta, Session().input.shift,
                  IsCtrlDown());
      break;
    case AC_MAXIMIZE:
      ActionMaxRestMin(hwnd, Session().input.delta);
      break;
    case AC_ROLL:
      RollWindow(hwnd, Session().input.delta);
      break;
    case AC_HSCROLL:
      ret = ScrollPointedWindow(Session().input.prevpt, -Session().input.delta,
                                WM_MOUSEHWHEEL);
      break;
    case AC_ZOOM:
      ret = ActionZoom(hwnd, Session().input.delta, 0);
      break;
    case AC_ZOOM2:
      ret = ActionZoom(hwnd, Session().input.delta, 1);
      break;
    case AC_NPSTACKED:
      ActionAltTab(Session().input.prevpt, Session().input.delta,
                   Session().input.shift, EnumStackedWindowsProc);
      break;
    case AC_NPSTACKED2:
      ActionAltTab(Session().input.prevpt, Session().input.delta,
                   !Session().input.shift, EnumStackedWindowsProc);
      break;
    case AC_NPLAYOUT: {
      action_t new_action = {0};
      new_action.ac = Session().input.delta < 0 ? AC_PLAYOUT : AC_NLAYOUT;
      SClickActions(hwnd, new_action);
    } break;
      //    case AC_BRIGHTNESS:   ActionBrightness(state.prevpt, state.delta);
      //    break;
    case AC_SHRT: {
      action_t rac = action;
      // Use Shrt(X) on WheelUp and Shrt(X+1) on Wheel Down.
      rac.fl += (Session().input.delta < 0);
      if (rac.fl < g_inputSequences.size() &&
          !g_inputSequences[rac.fl].empty()) {
        ret = 1;
        SendInputSequence(g_inputSequences[rac.fl].data());
        break;
      }
    } break;

    default:
      SClickActions(hwnd, action);
      ret = 1;
  }
  // ret is 0: next hook or 1: block whel and AltUp.
  Session().input.blockaltup =
      ret && Session().input.alt > BT_HWHEELU;  // block or not;
  return ret;                                   // block or next hook
}

// Actions to be performed in the Titlebar...
std::optional<ActionStartResult> TitleBarActions(POINT pt, action_t action,
                                                 enum button button) {
  if (MovementStartBusy()) return ActionStartResult::kBusy;
  if (!conf.TTBActions) return std::nullopt;
  const int hit = InTitlebar(pt, action, button);
  if (MovementStartBusy()) return ActionStartResult::kBusy;
  Session().input.hittest = hit;
  if (hit) return InitMovementAndActions(pt, nullptr, action, button);
  return std::nullopt;
}

// Just to be used in the ClickComboActions function
// and dispatched to the engine owner.
void DoMoveResizeMaxMinComboAction() {
  const HWND window = Session().input.hwnd;
  const uint64_t gesture = Session().gesture;
  const BYTE action = Session().input.action.ac;
  const auto current = [=] {
    return Session().gesture == gesture && Session().input.hwnd == window &&
           Session().input.action.ac == action;
  };
  if (!IsWindow(window) || !IsMovementAction(Session().input.action)) return;
  const bool zoomed = IsZoomed(window);
  if (!current()) return;
  if (zoomed) {
    if (IsSamePTT(&Session().input.clickpt, &Session().input.prevpt)) {
      Session().input.moving = CURSOR_ONLY;
      RestoreWindow(window);
    } else {
      Session().input.moving = 0;
      MouseMove(Session().input.prevpt);
    }
  } else if (Session().input.resizable) {
    LockMovement();
    if (!current()) return;
    if (IsHotclick(Session().input.alt)) {
      Session().input.action = k_action_none;
      Session().input.moving = 0;
    }
    MaximizeRestoreAtPoint(window, SW_MAXIMIZE, 2);
  }
}
static int ClickComboActions(action_t action) {
  if (!(conf.MMMaximize & 1)) return 0;
  // Maximize/Restore the window if pressing Move, Resize mouse buttons.
  if (Session().input.action.ac == AC_MOVE && action.ac == AC_RESIZE) {
    if (!PostWorkerCommand(MoveResizeComboCommand{Session().gesture})) return 0;
    Session().input.blockmouseup = 1;
    return 1;
  }
  return 0;
}

// Generalization of Click combo.
void DoComboActions(action_t action, enum button button) {
  // For safety
  if (!IsMovementAction(Session().input.action)) return;

  action_t accombo = GetActionMR(button);
  if (ActionInfo(accombo) & (ACINFO_MOVE | ACINFO_RESIZE | ACINFO_CLOSE)) {
    LockMovement();
  }
  if (BT_WHEELD <= button && button <= BT_HWHEELU) {
    // Handle wheel combo.
    if (accombo.ac) {
      DoWheelActions(Session().input.hwnd, accombo);
      // No mouseup to block for wheel actions...
    }
  } else {
    // Other buttons.
    if (accombo.ac) {
      SClickActions(Session().input.hwnd, accombo);
      Session().input.blockmouseup = 1;
    } else {
      // Try to do Move/Resize combo..
      if (ClickComboActions(action)) return;

      // Make default actions if nothing was overwritten.
      switch (button) {
        // case BT_MB4: break;
        //  MB5 toggles the snapping mode
        case BT_MB5:
          ToggleSnapState();
          break;
        // Toggle SnapToZones with RMB/MMB by default.
        default:
          ActionToggleSnapToZoneMode();
          break;
      }
      Session().input.blockmouseup = 1;  // Block anyway
    }
  }
}

}  // namespace altsnap::hooks
