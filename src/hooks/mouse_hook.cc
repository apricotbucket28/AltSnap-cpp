// mouse hook (split out of hooks.cc).

#include "hooks/mouse_hook.h"

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
#include "hooks/mouse_move.h"
#include "hooks/movement.h"
#include "hooks/overlay/trans_window.h"
#include "hooks/send_input.h"
#include "hooks/window/snap.h"
#include "hooks/window/window_info.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

static void UnhookMouseOnly();
/////////////////////////////////////////////////////////////////////////////
// This is somewhat the main function, it is active only when the ALT key is
// pressed, or is always on when conf.keepMousehook is enabled.
//
// We should not call the next Hook for button 6-20 (manual call only).

//
// Buttons 6-20 are for manual calls only: they are never passed to the next
// hook.
static LRESULT CallNextHook(enum button button, int nCode, WPARAM wParam,
                            LPARAM lParam) {
  if (button > BT_MB5 && button <= BT_MB20) return 1;
  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
  //    if (state.ignoreclick) LOGA("IgnoreClick")
  // Set up some variables
  auto msg = reinterpret_cast<PMSLLHOOKSTRUCT>(lParam);
  POINT pt = msg->pt;
  //    if ((0x201 > wParam || wParam > 0x205) && wParam != 0x20a && wParam !=
  //    WM_MOUSEMOVE)
  //        LOGA("wParam=%lx, data=%lx, time=%lu, extra=%lx, block?=%d,
  //        ignored?=%d", (DWORD)wParam
  //            , (DWORD)msg->mouseData, (DWORD)msg->time,
  //            (DWORD)msg->dwExtraInfo , (int)state.blockmouseup,
  //            (int)state.ignoreclick);
  if (nCode != HC_ACTION || state.ignoreclick || ScrollLockState())
    return CallNextHookEx(nullptr, nCode, wParam, lParam);

  // Mouse move, only if it is not exactly the same point than before
  if (wParam == WM_MOUSEMOVE) {
    if (SamePt(pt, state.prevpt))
      return CallNextHookEx(nullptr, nCode, wParam, lParam);
    // Store prevpt so we can check if the hook goes stale
    if (state.moving == DRAG_WAIT) {
      if (IsPtDragOut(&state.prevpt, &pt))
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
      state.moving = 0;

      MouseMove(state.prevpt);
    }
    state.prevpt = pt;

    // Reset double-click time
    if (!IsSamePTT(&pt, &state.clickpt)) {
      state.clicktime = 0;
    }
    // Move the window  && (state.moving || !IsSamePTT(&pt, &state.clickpt))
    if (state.action.ac && !state.blockmouseup) {  // resize or move...
      // Move the window every few frames.
      static DWORD oldtime;
      if (conf.RezTimer == 1) {
        // Only move window if the EVENT TIME is different.
        // LOGA("msg->time=%lu", msg->time);
        if (msg->time != oldtime) {
          MouseMove(pt);
          oldtime = msg->time;
        }
      } else {
        static UCHAR updaterate;
        updaterate =
            (updaterate + 1) %
            (state.action.ac == AC_MOVE ? conf.MoveRate : conf.ResizeRate);
        if (!updaterate) {
          MouseMove(pt);
        } else if (conf.RezTimer == 3 && msg->time != oldtime) {
          MouseMove(pt);
          oldtime = msg->time;
        }
      }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
  }

  // Get Button state and data.
  enum buttonstate buttonstate = GetButtonState(wParam);
  auto button = static_cast<enum button>(GetButton(wParam, lParam));
  // Get wheel delta
  state.delta = GET_WHEEL_DELTA_WPARAM(msg->mouseData);

  //    if (button<=BT_MB5)
  //        LOGA("button=%d, %s", button,
  //        buttonstate==STATE_DOWN?"DOWN":buttonstate==STATE_UP?"UP":"NONE");

  // Get actions or alternate (depends on ModKey())!
  action_t action = GetAction(button);   // Normal action
  action_t ttbact = GetActionT(button);  // Titlebar action

  // Handle another click if we are already busy with an action
  if (buttonstate == STATE_DOWN && state.action.ac &&
      state.action.ac != conf.GrabWithAlt[ModKey()].ac) {
    // Handle click combo action!
    DoComboActions(action, button);
    return 1;  // Block mousedown so altsnap does not remove g_mainhwnd
  }

  // Handle Titlebars actions if any
  if (ttbact.ac && buttonstate == STATE_DOWN) {
    int ret = TitleBarActions(pt, ttbact, button);
    // If we have nothing to do in the titlebar
    if (ret < 0 && conf.InactiveScroll && !state.alt && !state.action.ac &&
        (wParam == WM_MOUSEWHEEL || wParam == WM_MOUSEHWHEEL)) {
      // Scroll inactive window with wheel action...
      ret = ScrollPointedWindow(pt, state.delta, wParam);
    }
    if (ret == 0) return CallNextHook(button, nCode, wParam, lParam);
    if (ret == 1) return 1;
    ttbact = k_action_none;  // No titlebar action to be done.
  }

  // Check if the click is is a Hotclick and should enable ALT.
  // If the hotclick is also mapped to an action, then we execute it.
  int is_hotclick = IsHotclick(button);
  if (!state.alt && is_hotclick && buttonstate == STATE_DOWN) {
    state.alt = button;
    // Start an action now if hotclick is also an action.
    // If action == AC_NONE, we are checking for blacklists...
    if (!action.ac) {
      // If no action is to be made,we must reset
      // clickpt and actiondone for the Click UP
      // to be forwarded.
      state.sactiondone = k_action_none;
      state.clickpt = pt;
    }
    int ret = init_movement_and_actions(pt, nullptr, action, button);
    if (ret) {
      // Not balcklisted, action may have been performed!
      if (action.ac) state.alt = 0;  // Done!
      return 1;
    }

    // Window is blacklisted.
    // So me must forward the click...
    state.alt = 0;                                       // release alt!
    state.fwmouseup = 1;                                 // Forward up click...
    return CallNextHook(button, nCode, wParam, lParam);  // forward down click
  }
  if (state.alt == button && is_hotclick && buttonstate == STATE_UP) {
    state.alt = 0;
    // Block hotclick up if not an action
    // Because it will not be done by state.blockmouseup
    // if (!action) return 1;
    if (!action.ac && (conf.AblockHotclick || state.sactiondone.ac)) return 1;
    // If no action is to be done, we forward the click
    Send_Click_Thread(button);
    return 1;
  }

  // Check if we must BLOCK MOUSE UP... (after releasing hotclicks)
  if (buttonstate == STATE_UP) {
    // fw/block mouse up and decrement counter.
    if (state.fwmouseup) {
      state.fwmouseup = 0;
      // LOGA("forwarded BT%d mouse up", button);
      return CallNextHook(button, nCode, wParam, lParam);
    }
    if (state.blockmouseup) {
      state.blockmouseup--;
      if (!state.blockmouseup && !state.action.ac && !state.alt)
        UnhookMouseOnly();  // We no longer need the hook.
      // LOGA("blocked BT%d mouse up", button);
      return 1;
    }
  }

  // Long click grab timer
  if (conf.LongClickMove && !state.action.ac && !state.alt) {
    if (wParam == WM_LBUTTONDOWN) {
      state.clickpt = pt;
      // Start Grab timer
      SetTimer(g_mainhwnd, GRAB_TIMER, conf.LongClickMoveDelay,
               static_cast<TIMERPROC>(TimerWindowProc));
    } else {
      // Cancel Grab timer.
      KillTimer(g_mainhwnd, GRAB_TIMER);
      return CallNextHook(button, nCode, wParam, lParam);
    }
  }

  // Nothing to do...
  if (!action.ac && !ttbact.ac && buttonstate == STATE_DOWN)
    return CallNextHook(
        button, nCode, wParam,
        lParam);  // CallNextHookEx(NULL, nCode, wParam, lParam);

  // INIT ACTIONS on mouse down if Alt is down...
  if (buttonstate == STATE_DOWN && state.alt) {
    // LogState("BUTTON DOWN:\n");
    //  Double ckeck some hotkey is pressed.
    if (!state.action.ac && !IsHotclick(state.alt) && !IsHotkeyDown()) {
      UnhookMouse();
      return CallNextHook(
          button, nCode, wParam,
          lParam);  // CallNextHookEx(NULL, nCode, wParam, lParam);
    }
    // Start an action (alt is down)
    int ret = init_movement_and_actions(pt, nullptr, action, button);
    if (!ret)
      return CallNextHook(
          button, nCode, wParam,
          lParam);  // CallNextHookEx(NULL, nCode, wParam, lParam);

    return 1;  // block mousedown

    // BUTTON UP
  } else if (buttonstate == STATE_UP) {
    // LogState("BUTTON UP:\n");
    SetWindowTrans(nullptr);  // Reset window transparency

    if (action.ac &&
        (state.action.ac == action.ac ||
         (state.action.ac == AC_MOVE && action.ac == AC_RESIZE)) &&
        (!state.moving || state.moving == DRAG_WAIT)  // No drag occured
        && state.sactiondone.ac <=
               AC_RESIZE  // Only move/resize may have happened in the meantime
        && !IsCtrlDown()  // Ctrl is not down (because of focusing)
        && IsSamePTT(&pt, &state.clickpt)  // same point (within drag)
        && !IsDoubleClick(button)) {       // Long click unless PiercingClick=1
      FinishMovementAsync();
      // Mouse UP actions here only in case of MOVEMENT!:
      // Perform an action on mouse up without drag on move/resize
      int inTTB = 2 * (!!state.hittest);  // If we are in the titlebar add two
      if (action.ac == AC_MOVE) action = conf.MoveUp[ModKey() + inTTB];
      if (action.ac == AC_RESIZE) action = conf.ResizeUp[ModKey() + inTTB];

      if (action.ac > AC_RESIZE) {
        SClickActions(state.hwnd, action);
      } else {
        LOG("Forwarding the %d the click!", button);
        // Forward the click if no action was Mapped!
        // Win 10+ does not like receaving button down
        // when the button is already down, so we create a thread.
        Send_Click_Thread(button);
      }
      return 1;  // block mouseup
    }
    // If a button performing an action is released,
    // we finish all moveent and proceed.
    if (action.ac && state.action.ac) {
      FinishMovementAsync();
      return 1;
    }
  }
  return CallNextHook(button, nCode, wParam,
                      lParam);  // CallNextHookEx(NULL, nCode, wParam, lParam);
}  // END OF LL MOUSE PROCK

/////////////////////////////////////////////////////////////////////////////
void HookMouse() {
  state.moving = 0;  // Used to know the first time we call MouseMove.
  if (conf.keepMousehook) {
    PostMessage(g_mainhwnd, WM_TIMER, REHOOK_TIMER, 0);
  }

  // Check if mouse is already hooked
  if (mousehook) return;

  // Set up the mouse hook
  mousehook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hinstDLL, 0);
}
/////////////////////////////////////////////////////////////////////////////
static void UnhookMouseOnly() {
  // Do not unhook if not hooked or if the hook is still used for something
  if (!mousehook || conf.keepMousehook || state.blockmouseup) return;

  // Remove mouse hook
  UnhookWindowsHookEx(mousehook);
  mousehook = nullptr;
}
void UnhookMouse() {
  // Stop action
  state.action = k_action_none;
  state.ctrl = 0;
  state.shift = 0;
  state.moving = 0;

  SetWindowTrans(nullptr);
  StopSpeedMes();

  HideCursor();

  // Release cursor trapping in case...
  ClipCursorOnce(nullptr);

  UnhookMouseOnly();
}

}  // namespace altsnap::hooks
