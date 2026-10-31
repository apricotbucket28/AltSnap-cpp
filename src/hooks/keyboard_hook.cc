// keyboard hook (split out of hooks.cc).

#include "hooks/keyboard_hook.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "common/ini_section.h"
#include "hooks/actions/window_actions.h"
#include "hooks/blacklist.h"
#include "hooks/hooks_internal.h"
#include "hooks/input_state.h"
#include "hooks/mouse_hook.h"
#include "hooks/movement.h"
#include "hooks/overlay/trans_window.h"
#include "hooks/overlay/window_menu.h"
#include "hooks/send_input.h"
#include "hooks/window/snap.h"
#include "hooks/window/window_info.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

static void HotkeyUp() {
  // Prevent the alt keyup from triggering the window menu to be selected
  // The way this works is that the alt key is "disguised" by sending
  // ctrl keydown/keyup events
  LOG("HotkeyUp()");
  if (Session().input.blockaltup || Session().input.action.ac) {
    SendEndKey();
    Session().input.blockaltup = 0;
    // If there is more that one key down remaining
    // then we must block the next alt up.
    if (NumKeysDown() > 1) Session().input.blockaltup = 1;
  }

  // Hotkeys have been released
  Session().input.alt = 0;
  Session().input.alt1 = 0;
  if (Session().input.action.ac &&
      (conf.actions.grab_with_alt.normal[0].ac ||
       conf.actions.grab_with_alt.normal[1].ac) &&
      (IsMovementAction(conf.actions.grab_with_alt.normal[0]) ||
       IsMovementAction(conf.actions.grab_with_alt.normal[1]))) {
    FinishMovementAsync();
  }

  // Unhook mouse if no actions is ongoing.
  if (!Session().input.action.ac) {
    UnhookMouse();
  }
}

// Keep this one minimalist, it is always on.
namespace {

// ---- Key down ---------------------------------------------------------------
// The HandleKeyDown() helpers return true if the key must be swallowed.

// Alt (or any other hotkey) goes down: start waiting for a mouse action.
void StartAltAction(UCHAR vkey) {
  Session().input.alt = vkey;
  Session().input.blockaltup = 0;
  Session().input.sclickhwnd = nullptr;
  KillAltSnapMenu();  // Hide unikey menu in case...
  if (MovementStartBusy()) {
    Session().deferred_alt = vkey;
    return;
  }

  // Release ALt even if we receave no AltUP message because of.
  // Stupid software that like to steal Alt.

  // Hook mouse
  HookMouse();
  if (conf.actions.grab_with_alt.normal[0].ac ||
      conf.actions.grab_with_alt.normal[1].ac) {
    const action_t action = conf.actions.grab_with_alt.Get(
        ActionContext::kNormal,
        IsModKey(vkey) || (!IsHotkey(conf.ModKey[0]) && ModKey()));
    if (action.ac) {
      Session().input.blockmouseup = 0;  // In case.
      POINT pt;
      GetCursorPos(&pt);
      if (InitMovementAndActions(pt, nullptr, action, vkey) ==
          ActionStartResult::kNotHandled) {
        UnhookMouse();
      }
      Session().input.blockmouseup = 0;  // In case.
    }
  }
}

// A Shift key goes down. Blocks the key while moving, to prevent Windows from
// changing the keyboard layout.
bool OnShiftDown(UCHAR vkey) {
  if (!Session().input.shift && !IsModKey(vkey)) {
    if (conf.ShiftSnaps) {
      Session().input.snap = conf.AutoSnap == 3 ? 0 : 3;
    }
    Session().input.shift = 1;
    Session().input.usezones = (conf.UseZones & 9) != 9;  // Zones and
    Session().input.shiftpt =
        Session().input.prevpt;  // Save point where shift was pressed.
    Session().input.enumed = 0;  // Reset enum state.
  }
  return Session().input.alt && Session().input.action.ac;
}

// The user pressed Escape: cancel the current action. Returns true if the key
// was consumed by the menu or by an ongoing action.
bool OnEscapeDown() {
  // Alsays disable shift and ctrl, in case of Ctrl+Shift+ESC.
  Session().input.ctrl = 0;
  Session().input.shift = 0;
  Session().last_window.hwnd = nullptr;
  Session().input.ignorekey = 0;    // In case ...
  Session().input.ignoreclick = 0;  // In case ...
  if (Session().input.unikeymenu || g_mchwnd) {
    const bool menu_was_open = IsMenu(Session().input.unikeymenu);
    KillAltSnapMenu();
    if (menu_was_open) return true;
  }

  // Stop current action
  if (Session().input.action.ac || Session().input.alt) {
    const action_t action = Session().input.action;
    HideTransWin();
    // Send WM_EXITSIZEMOVE and EVENT_SYSTEM_MOVESIZEEND
    NotifySizeMoveStaEnd(Session().input.hwnd, 0);

    Session().input.alt = 0;
    Session().input.alt1 = 0;

    UnhookMouse();

    // Block ESC if an action was ongoing
    if (action.ac) return true;
  }
  return false;
}

// Ctrl goes down: trap the cursor in the current monitor.
bool IsCtrlTrappingCursor(UCHAR vkey, const KBDLLHOOKSTRUCT& kb) {
  return !Session().input.ctrl &&
         (Session().input.alt != vkey)  // avoid cursor trapping at first Ctrl
                                        // down if Ctrl was used as hotkey
         && (vkey == VK_LCONTROL || vkey == VK_RCONTROL) &&
         (!IsHotkey(vkey) ||
          Session().input.action.ac)  // If Ctrl is a hotkey, skip unless
                                      // already moving
         && !(kb.scanCode & SCANCODE_SIMULATED)  // Ignore AltGr scan code
         && !IsModKey(vkey);
}
void OnCtrlDown() {
  RestrictToCurentMonitor();
  Session().input.ctrl = 1;
  Session().input.ctrlpt =
      Session().input.prevpt;  // Save point where ctrl was pressed.
  if (Session().input.action.ac) {
    SetForegroundWindowL(Session().input.hwnd);
  }
}

// Alt goes down again right after AltSnap's menu opened: block it.
bool IsAltDownBlockedByMenu(UCHAR vkey) {
  if (!(Session().input.sclickhwnd && g_mchwnd && Session().input.alt &&
        (vkey == VK_LMENU || vkey == VK_RMENU))) {
    return false;
  }
  return Session().input.unikeymenu == reinterpret_cast<HMENU>(1) ||
         (IsWindow(Session().input.sclickhwnd) && IsWindow(g_mchwnd) &&
          IsMenu(Session().input.unikeymenu));
}

// Extra mouse buttons (X1, X2) emulated with keys.
bool IsMappedXButtonKey(UCHAR vkey, int& index) {
  index = XXButtonIndex(vkey);
  return index >= 0 &&
         (GetAction(BT_MMB + index).ac || GetActionT(BT_MMB + index).ac ||
          IsHotclick(BT_MMB + index));
}

// Holding a letter key opens a menu of accented characters ("UniKey menu").
bool IsUniKeyMenuEnabledHere() {
  HWND foreground = GetForegroundWindow();
  return conf.UniKeyHoldMenu && foreground && !IsFullScreenBL(foreground) &&
         !IsBlacklisted(foreground, BlkLst.Processes) &&
         !IsBlacklisted(foreground, BlkLst.Windows);
}
bool HandleUniKeyMenuKey(UCHAR vkey) {
  static const UCHAR ctrlaltwinkeys[] = {VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN,
                                         0};
  static const UCHAR menupopdownkeys[] = {
      VK_BACK,  VK_TAB,   VK_APPS, VK_DELETE, VK_SPACE, VK_LEFT,
      VK_RIGHT, VK_PRIOR, VK_NEXT, VK_END,    VK_HOME,  0};
  if (Session().input.unikeymenu && IsMenu(Session().input.unikeymenu) &&
      !(GetKeyState(vkey) & 0x8000)) {
    if (vkey == VK_SNAPSHOT) return false;
    if (IsHotkeyy(vkey, menupopdownkeys)) {
      KillAltSnapMenu();
      return false;
    }
    // Forward all keys to the menu: all keys are "directed to the Menu".
    PostMessage(g_mchwnd, WM_KEYDOWN, vkey, 0);
    return true;  // block keydown
  }
  if (!Session().input.ctrl && !Session().input.alt &&
      (0x41 <= vkey && vkey <= 0x5A) && !IsAKeyDown(ctrlaltwinkeys)) {
    // handle long A-Z keydown.
    if (GetKeyState(vkey) & 0x8000) {  // The key is autorepeating.
      if (!IsMenu(Session().input.unikeymenu)) {
        KillAltSnapMenu();
        Session().input.unikeymenu = reinterpret_cast<HMENU>(1);
        g_mchwnd = KreateMsgWin(MenuWindowProc, APP_NAME L"-SClick", 2);
        const UCHAR shiftdown =
            GetKeyState(VK_SHIFT) & 0x8000 || GetKeyState(VK_CAPITAL) & 1;
        PostMessage(g_mainhwnd, WM_UNIKEYMENU,
                    reinterpret_cast<WPARAM>(g_mchwnd),
                    vkey | (shiftdown << 8));
      }
      return true;  // block keydown
    }
  }
  return false;
}

// The first branch that applies handles the key, as in the original else-if
// chain.
bool HandleKeyDown(const KBDLLHOOKSTRUCT& kb) {
  const UCHAR vkey = kb.vkCode;

  if (!Session().input.alt &&
      (!Session().input.action.ac || Session().finish_pending) &&
      (!conf.KeyCombo ||
       (Session().input.alt1 && Session().input.alt1 != vkey)) &&
      IsHotkey(vkey) &&
      (!conf.MaxKeysNum || TotNumberOfKeysDown() < conf.MaxKeysNum)) {
    StartAltAction(vkey);
    return false;
  }
  if (conf.KeyCombo && !Session().input.alt1 && !Session().input.action.ac &&
      IsHotkey(vkey)) {
    Session().input.alt1 = vkey;
    return false;
  }
  if (IsHotkeyy(vkey, conf.Shiftkeys)) return OnShiftDown(vkey);
  if (vkey == VK_SPACE && Session().input.action.ac &&
      !IsSamePTT(&Session().input.clickpt, &Session().input.prevpt)) {
    ToggleSnapState();
    return true;  // Block to avoid sys menu.
  }
  if (Session().input.alt &&
      Session().input.action.ac ==
          conf.actions.grab_with_alt.Get(ActionContext::kNormal, ModKey()).ac &&
      IsKillkey(vkey)) {
    // Release Hook on Alt+KillKey
    // eg: DisplayFusion Alt+Tab elevated windows captures AltUp
    HotkeyUp();
    return false;
  }
  if (IsHotkeyy(vkey, conf.ESCkeys)) return OnEscapeDown();
  if (IsCtrlTrappingCursor(vkey, kb)) {
    OnCtrlDown();
    return false;
  }
  if (Session().input.sclickhwnd && g_mchwnd && Session().input.alt &&
      (vkey == VK_LMENU || vkey == VK_RMENU)) {
    return IsAltDownBlockedByMenu(vkey);
  }
  int xbutton_index;
  if (IsMappedXButtonKey(vkey, xbutton_index)) {
    if (!Session().input.xxbutton) {
      Session().input.xxbutton = 1;  // To Ignore autorepeat...
      SimulateXButton(WM_XBUTTONDOWN, xbutton_index);
    }
    return true;
  }
  if (IsUniKeyMenuEnabledHere()) return HandleUniKeyMenuKey(vkey);
  return false;
}

// ---- Key up
// -------------------------------------------------------------------

// Returns true if the key must be swallowed.
bool HandleKeyUp(UCHAR vkey) {
  int xbutton_index;
  if (IsHotkey(vkey)) {
    HotkeyUp();
  } else if (IsHotkeyy(vkey, conf.Shiftkeys)) {
    Session().input.shift = 0;
    Session().input.usezones = (conf.UseZones & 9) == 9;
    Session().input.snap = conf.AutoSnap;
    // if an action was performed and Alt is still down.
    if (Session().input.alt && Session().input.blockaltup &&
        (vkey == VK_LSHIFT || vkey == VK_RSHIFT)) {
      SendEndKey();  // send Ctrl to avoid Alt+Shift=>switch keymap
    }
  } else if (Session().input.blockaltup && IsModKey(vkey)) {
    // We release ModKey before Hotkey
    SendEndKey();
    Session().input.blockaltup = 0;
    // If there is more that one key down remaining
    // then we must block the next alt up.
    if (NumKeysDown() > 1) Session().input.blockaltup = 1;
  } else if ((xbutton_index = XXButtonIndex(vkey)) >= 0) {
    Session().input.xxbutton = 0;
    SimulateXButton(WM_XBUTTONUP, xbutton_index);
    return true;
  }

  // Always process Ctrl Up.
  if (vkey == VK_LCONTROL || vkey == VK_RCONTROL) {
    ClipCursorOnce(nullptr);  // Release cursor trapping
    Session().input.ctrl = 0;
    // If there is no ongoing action then Control UP prevents AltDragging...
    if (!Session().input.action.ac) Session().input.alt = 0;
  }
  return false;
}

}  // namespace

void ResumeDeferredAltAction() {
  const auto key = std::exchange(Session().deferred_alt, std::nullopt);
  if (key && HooksActive() && Session().input.alt == *key &&
      !Session().input.action.ac)
    StartAltAction(*key);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);

  HookSession* session = TrySession();
  if (nCode != HC_ACTION || !HooksActive() || !session ||
      session->input.ignorekey) {
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
  }

  SessionExecutionScope execution_scope;
  const UCHAR vkey = kb->vkCode;
  if (vkey == VK_SCROLL) PostMessage(g_mainhwnd, WM_UPDATETRAY, 0, 0);
  if (ScrollLockState()) return CallNextHookEx(nullptr, nCode, wParam, lParam);

  bool swallow = false;
  if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
    swallow = HandleKeyDown(*kb);
  } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
    swallow = HandleKeyUp(vkey);
  }
  return swallow ? 1 : CallNextHookEx(nullptr, nCode, wParam, lParam);
}

}  // namespace altsnap::hooks
