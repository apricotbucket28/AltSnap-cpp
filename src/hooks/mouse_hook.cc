// mouse hook (split out of hooks.cc).

#include "hooks/mouse_hook.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>
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

// This is somewhat the main function, it is active only when the ALT key is
// pressed, or is always on when g_keep_mousehook is enabled.
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

namespace {

// A mouse button/wheel event as seen by the low-level hook.
struct MouseEvent {
  int nCode;
  WPARAM wParam;
  LPARAM lParam;
  POINT pt;
  enum buttonstate buttonstate;
  enum button button;
  action_t action;  // The action mapped to this button.
  action_t ttbact;  // The titlebar action mapped to this button.
};

// Lets the event through to the next hook (or blocks it for the buttons that
// are only used for manual calls).
LRESULT PassOn(const MouseEvent& e) {
  return CallNextHook(e.button, e.nCode, e.wParam, e.lParam);
}
LRESULT PassBusyDown(const MouseEvent& e) {
  if (e.button >= BT_LMB && e.button <= BT_MB20)
    Session().passed_buttons[e.button] = true;
  return PassOn(e);
}

// Moves or resizes the window when the mouse moves, but only every few events
// (see RezTimer, MoveRate and ResizeRate).
void MoveWindowForMouseEvent(const MSLLHOOKSTRUCT& msg, POINT pt) {
  DWORD& oldtime = Session().mouse_event_time;
  if (conf.RezTimer == 1) {
    // Only move window if the EVENT TIME is different.
    if (msg.time != oldtime) {
      MouseMove(pt);
      oldtime = msg.time;
    }
    return;
  }
  UCHAR& updaterate = Session().mouse_update_rate;
  updaterate =
      (updaterate + 1) %
      (Session().input.action.ac == AC_MOVE ? conf.MoveRate : conf.ResizeRate);
  if (!updaterate) {
    MouseMove(pt);
  } else if (conf.RezTimer == 3 && msg.time != oldtime) {
    MouseMove(pt);
    oldtime = msg.time;
  }
}

// WM_MOUSEMOVE: only if it is not exactly the same point than before.
void HandleMouseMove(const MSLLHOOKSTRUCT& msg) {
  const POINT pt = msg.pt;
  if (SamePt(pt, Session().input.prevpt)) return;
  // Store prevpt so we can check if the hook goes stale
  if (Session().input.moving == DRAG_WAIT) {
    if (IsPtDragOut(&Session().input.prevpt, &pt)) return;
    Session().input.moving = 0;
    MouseMove(Session().input.prevpt);
  }
  Session().input.prevpt = pt;

  // Reset double-click time
  if (!IsSamePTT(&pt, &Session().input.clickpt)) Session().input.clicktime = 0;

  if (Session().input.action.ac &&
      !Session().input.blockmouseup) {  // resize or move...
    MoveWindowForMouseEvent(msg, pt);
  }
}

// Another click while we are already busy with an action: a click combo.
std::optional<LRESULT> HandleComboClick(const MouseEvent& e) {
  if (e.buttonstate != STATE_DOWN || !Session().input.action.ac ||
      Session().input.action.ac ==
          conf.actions.grab_with_alt.Get(ActionContext::kNormal, ModKey()).ac) {
    return std::nullopt;
  }
  DoComboActions(e.action, e.button);
  return 1;  // Block mousedown so altsnap does not remove g_mainhwnd
}

// Titlebar actions, and scrolling the inactive window under the cursor.
std::optional<LRESULT> HandleTitlebarActions(MouseEvent& e) {
  if (!e.ttbact.ac || e.buttonstate != STATE_DOWN) return std::nullopt;

  const auto result = TitleBarActions(e.pt, e.ttbact, e.button);
  if (result == ActionStartResult::kBusy) return PassBusyDown(e);
  int ret = !result ? -1 : (*result == ActionStartResult::kConsumed ? 1 : 0);
  // If we have nothing to do in the titlebar
  if (ret < 0 && conf.InactiveScroll && !Session().input.alt &&
      !Session().input.action.ac &&
      (e.wParam == WM_MOUSEWHEEL || e.wParam == WM_MOUSEHWHEEL)) {
    // Scroll inactive window with wheel action...
    ret = ScrollPointedWindow(e.pt, Session().input.delta, e.wParam);
  }
  if (ret == 0) return PassOn(e);
  if (ret == 1) return 1;
  e.ttbact = k_action_none;  // No titlebar action to be done.
  return std::nullopt;
}

// A "hotclick" is a mouse button that enables Alt while it is held; it may
// also be mapped to an action itself.
std::optional<LRESULT> HandleHotclick(const MouseEvent& e) {
  if (!IsHotclick(e.button)) return std::nullopt;

  if (!Session().input.alt && e.buttonstate == STATE_DOWN) {
    Session().input.alt = e.button;
    // Start an action now if hotclick is also an action.
    // If action == AC_NONE, we are checking for blacklists...
    if (!e.action.ac) {
      // If no action is to be made,we must reset
      // clickpt and actiondone for the Click UP
      // to be forwarded.
      Session().input.sactiondone = k_action_none;
      Session().input.clickpt = e.pt;
    }
    const auto result =
        InitMovementAndActions(e.pt, nullptr, e.action, e.button);
    if (result == ActionStartResult::kBusy) {
      Session().input.alt = 0;
      return PassBusyDown(e);
    }
    if (result == ActionStartResult::kConsumed) {
      // Not blacklisted, action may have been performed!
      if (e.action.ac) Session().input.alt = 0;  // Done!
      return 1;
    }

    // Window is blacklisted, so we must forward the click...
    Session().input.alt = 0;        // release alt!
    Session().input.fwmouseup = 1;  // Forward up click...
    return PassOn(e);               // forward down click
  }
  if (Session().input.alt == e.button && e.buttonstate == STATE_UP) {
    Session().input.alt = 0;
    // Block hotclick up if not an action
    // Because it will not be done by state.blockmouseup
    if (!e.action.ac && (conf.AblockHotclick || Session().input.sactiondone.ac))
      return 1;
    // If no action is to be done, we forward the click
    SendClickThread(e.button);
    return 1;
  }
  return std::nullopt;
}

// Mouse-up events that must be forwarded or swallowed (after releasing
// hotclicks or finishing an action).
std::optional<LRESULT> HandleBlockedMouseUp(const MouseEvent& e) {
  if (e.buttonstate != STATE_UP) return std::nullopt;

  // fw/block mouse up and decrement counter.
  if (Session().input.fwmouseup) {
    Session().input.fwmouseup = 0;
    return PassOn(e);
  }
  if (Session().input.blockmouseup) {
    Session().input.blockmouseup--;
    if (!Session().input.blockmouseup && !Session().input.action.ac &&
        !Session().input.alt) {
      UnhookMouseOnly();  // We no longer need the hook.
    }
    return 1;
  }
  return std::nullopt;
}

// Moving a window by pressing the left button for a long time.
std::optional<LRESULT> HandleLongClickGrab(const MouseEvent& e) {
  if (!conf.LongClickMove || Session().input.action.ac || Session().input.alt)
    return std::nullopt;

  if (e.wParam == WM_LBUTTONDOWN) {
    Session().input.clickpt = e.pt;
    // Start Grab timer
    SetTimer(g_mainhwnd, GRAB_TIMER, conf.LongClickMoveDelay,
             static_cast<TIMERPROC>(TimerWindowProc));
    return std::nullopt;
  }
  // Cancel Grab timer.
  KillTimer(g_mainhwnd, GRAB_TIMER);
  return PassOn(e);
}

// A button was pressed while Alt (or a hotkey) is down: start the action.
std::optional<LRESULT> HandleButtonDownWithAlt(const MouseEvent& e) {
  if (e.buttonstate != STATE_DOWN || !Session().input.alt) return std::nullopt;

  // Double check some hotkey is pressed.
  if (!Session().input.action.ac && !IsHotclick(Session().input.alt) &&
      !IsHotkeyDown()) {
    UnhookMouse();
    return PassOn(e);
  }
  // Start an action (alt is down)
  const auto result = InitMovementAndActions(e.pt, nullptr, e.action, e.button);
  if (result == ActionStartResult::kBusy) return PassBusyDown(e);
  if (result == ActionStartResult::kNotHandled) {
    return PassOn(e);
  }
  return 1;  // block mousedown
}

// True if the release of the button that started a move/resize is a plain
// click (nothing was dragged).
bool IsClickWithoutDrag(const MouseEvent& e) {
  return e.action.ac &&
         (Session().input.action.ac == e.action.ac ||
          (Session().input.action.ac == AC_MOVE && e.action.ac == AC_RESIZE)) &&
         (!Session().input.moving ||
          Session().input.moving == DRAG_WAIT)  // No drag occured
         && Session().input.sactiondone.ac <=
                AC_RESIZE  // Only move/resize may have happened meanwhile
         && !IsCtrlDown()  // Ctrl is not down (because of focusing)
         &&
         IsSamePTT(&e.pt, &Session().input.clickpt)  // same point (within drag)
         && !IsDoubleClick(e.button);  // Long click unless PiercingClick=1
}

// A button was released: finish the movement or run the click action.
std::optional<LRESULT> HandleButtonUp(const MouseEvent& e) {
  if (e.buttonstate != STATE_UP) return std::nullopt;

  SetWindowTrans(nullptr);  // Reset window transparency

  if (IsClickWithoutDrag(e)) {
    FinishMovementAsync();
    // Mouse UP actions here only in case of MOVEMENT!:
    // Perform an action on mouse up without drag on move/resize
    action_t action = e.action;
    const ActionContext context = Session().input.hittest
                                      ? ActionContext::kTitlebar
                                      : ActionContext::kNormal;
    if (action.ac == AC_MOVE)
      action = conf.actions.move_up.Get(context, ModKey());
    if (action.ac == AC_RESIZE)
      action = conf.actions.resize_up.Get(context, ModKey());

    if (action.ac > AC_RESIZE) {
      SClickActions(Session().input.hwnd, action);
    } else {
      LOG("Forwarding the %d the click!", e.button);
      // Forward the click if no action was Mapped!
      // Win 10+ does not like receaving button down
      // when the button is already down, so we create a thread.
      SendClickThread(e.button);
    }
    return 1;  // block mouseup
  }
  // If a button performing an action is released,
  // we finish all movement and proceed.
  if (e.action.ac && Session().input.action.ac) {
    FinishMovementAsync();
    return 1;
  }
  return std::nullopt;
}

}  // namespace

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
  const auto* msg = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
  HookSession* session = TrySession();
  if (nCode != HC_ACTION || !HooksActive() || !session ||
      session->input.ignoreclick || ScrollLockState()) {
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
  }

  SessionExecutionScope execution_scope;
  if (wParam == WM_MOUSEMOVE) {
    HandleMouseMove(*msg);
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
  }

  // Button state and data; the actions depend on ModKey().
  MouseEvent e;
  e.nCode = nCode;
  e.wParam = wParam;
  e.lParam = lParam;
  e.pt = msg->pt;
  e.buttonstate = GetButtonState(wParam);
  e.button = static_cast<enum button>(GetButton(wParam, lParam));
  // Admission precedes titlebar hit testing, hotclick activation and combos.
  // Remember pass-through downs so a later finish cannot swallow their ups.
  if (e.button >= BT_LMB && e.button <= BT_MB20) {
    bool& passed = Session().passed_buttons[e.button];
    if (e.buttonstate == STATE_UP && std::exchange(passed, false))
      return PassOn(e);
    if (e.buttonstate == STATE_DOWN) passed = false;
  }
  if (e.buttonstate == STATE_DOWN && MovementStartBusy())
    return PassBusyDown(e);
  Session().input.delta = GET_WHEEL_DELTA_WPARAM(msg->mouseData);
  e.action = GetAction(e.button);   // Normal action
  e.ttbact = GetActionT(e.button);  // Titlebar action
  if (e.buttonstate == STATE_DOWN && MovementStartBusy())
    return PassBusyDown(e);

  if (auto handled = HandleComboClick(e)) return *handled;
  if (auto handled = HandleTitlebarActions(e)) return *handled;
  if (auto handled = HandleHotclick(e)) return *handled;
  if (auto handled = HandleBlockedMouseUp(e)) return *handled;
  if (auto handled = HandleLongClickGrab(e)) return *handled;

  // Nothing to do...
  if (!e.action.ac && !e.ttbact.ac && e.buttonstate == STATE_DOWN) {
    return PassOn(e);
  }

  if (auto handled = HandleButtonDownWithAlt(e)) return *handled;
  if (auto handled = HandleButtonUp(e)) return *handled;
  return PassOn(e);
}

// Whether the mouse hook stays installed permanently (needed by some options)
// instead of only while a button is pressed. Set from the configuration.
static std::atomic<bool> g_keep_mousehook{false};
void SetKeepMouseHook(bool keep) {
  g_keep_mousehook = keep;
}
bool KeepMouseHook() {
  return g_keep_mousehook;
}

void HookMouse() {
  if (!Session().finish_pending && !Session().move_running)
    Session().input.moving = 0;  // First MouseMove initializes the drag.
  if (g_keep_mousehook) {
    PostMessage(g_mainhwnd, WM_TIMER, REHOOK_TIMER, 0);
  }

  // Check if mouse is already hooked
  if (mousehook) return;

  // Set up the mouse hook
  mousehook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hinstDLL, 0);
}

static void UnhookMouseOnly() {
  // Do not unhook if not hooked or if the hook is still used for something
  if (!mousehook || g_keep_mousehook || Session().input.blockmouseup) return;

  // Remove mouse hook
  if (HHOOK hook = mousehook.exchange(nullptr)) UnhookWindowsHookEx(hook);
}
void UnhookMouse() {
  // Stop action
  Session().input.action = k_action_none;
  Session().input.ctrl = 0;
  Session().input.shift = 0;
  Session().input.moving = 0;

  SetWindowTrans(nullptr);
  StopSpeedMes();

  HideCursor();

  // Release cursor trapping in case...
  ClipCursorOnce(nullptr);

  UnhookMouseOnly();
}

}  // namespace altsnap::hooks
