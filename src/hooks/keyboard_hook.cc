// keyboard hook (split out of hooks.cc).

#include "hooks/keyboard_hook.h"

#include <algorithm>
#include <array>
#include <cstddef>
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

///////////////////////////////////////////////////////////////////////////
static void HotkeyUp() {
  // Prevent the alt keyup from triggering the window menu to be selected
  // The way this works is that the alt key is "disguised" by sending
  // ctrl keydown/keyup events
  LOG("HotkeyUp()");
  if (state.blockaltup || state.action.ac) {
    SendEndKey();
    state.blockaltup = 0;
    // If there is more that one key down remaining
    // then we must block the next alt up.
    if (NumKeysDown() > 1) state.blockaltup = 1;
  }

  // Hotkeys have been released
  state.alt = 0;
  state.alt1 = 0;
  if (state.action.ac && (conf.GrabWithAlt[0].ac || conf.GrabWithAlt[1].ac) &&
      (MOUVEMENT(conf.GrabWithAlt[0]) || MOUVEMENT(conf.GrabWithAlt[1]))) {
    FinishMovementAsync();
  }

  // Unhook mouse if no actions is ongoing.
  if (!state.action.ac) {
    UnhookMouse();
  }
}
///////////////////////////////////////////////////////////////////////////
// Keep this one minimalist, it is always on.
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  PKBDLLHOOKSTRUCT kbh = (reinterpret_cast<PKBDLLHOOKSTRUCT>(lParam));

  if (nCode != HC_ACTION) return CallNextHookEx(nullptr, nCode, wParam, lParam);
  // g_lastKeyboardTick = kbh->time;
  if (state.ignorekey) return CallNextHookEx(nullptr, nCode, wParam, lParam);

  unsigned char vkey = kbh->vkCode;
  //    DWORD scode = kbh->scanCode;
  int xxbtidx = 0;
  HWND fhwnd = nullptr;
  //    if (vkey!=VK_F5) { // show key codes
  //        LOGA("wp=%u, vKey=%lx, sCode=%lx, flgs=%lx, ex=%lx"
  //        , wParam, kbh->vkCode, kbh->scanCode, kbh->flags, kbh->dwExtraInfo);
  //    }
  if (vkey == VK_SCROLL) PostMessage(g_mainhwnd, WM_UPDATETRAY, 0, 0);
  if (ScrollLockState()) return CallNextHookEx(nullptr, nCode, wParam, lParam);

  if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
    if (!state.alt && !state.action.ac &&
        (!conf.KeyCombo || (state.alt1 && state.alt1 != vkey)) &&
        IsHotkey(vkey) &&
        (!conf.MaxKeysNum || TotNumberOfKeysDown() < conf.MaxKeysNum)) {
      // LOGA("\nALT DOWN");
      state.alt = vkey;
      state.blockaltup = 0;
      state.sclickhwnd = nullptr;
      KillAltSnapMenu();  // Hide unikey menu in case...

      // Release ALt even if we receave no AltUP message because of.
      // Stupid software that like to steal Alt.

      // Hook mouse
      HookMouse();
      if (conf.GrabWithAlt[0].ac || conf.GrabWithAlt[1].ac) {
        POINT pt;
        action_t action =
            conf.GrabWithAlt[IsModKey(vkey) ||
                             (!IsHotkey(conf.ModKey[0]) && ModKey())];
        if (action.ac) {
          state.blockmouseup = 0;  // In case.
          GetCursorPos(&pt);
          if (!init_movement_and_actions(pt, nullptr, action, vkey)) {
            UnhookMouse();
          }
          state.blockmouseup = 0;  // In case.
        }
      }
    } else if (conf.KeyCombo && !state.alt1 && !state.action.ac &&
               IsHotkey(vkey)) {
      state.alt1 = vkey;

    } else if (IsHotkeyy(vkey, conf.Shiftkeys)) {
      if (!state.shift && !IsModKey(vkey) /* != conf.ModKey*/) {
        if (conf.ShiftSnaps) {
          state.snap = conf.AutoSnap == 3 ? 0 : 3;
        }
        state.shift = 1;
        state.usezones = (conf.UseZones & 9) != 9;  // Zones and
        state.shiftpt = state.prevpt;  // Save point where shift was pressed.
        state.enumed = 0;              // Reset enum state.
      }

      // Block keydown to prevent Windows from changing keyboard layout
      if (state.alt && state.action.ac) {
        return 1;
      }
    } else if (vkey == VK_SPACE && state.action.ac &&
               !IsSamePTT(&state.clickpt, &state.prevpt)) {
      ToggleSnapState();
      return 1;  // Block to avoid sys menu.
    } else if (state.alt && state.action.ac == conf.GrabWithAlt[ModKey()].ac &&
               IsKillkey(vkey)) {
      // Release Hook on Alt+KillKey
      // eg: DisplayFusion Alt+Tab elevated windows captures AltUp
      HotkeyUp();
    } else if (IsHotkeyy(
                   vkey,
                   conf.ESCkeys)) {  // USER PRESSED ESCAPE! (vkey == VK_ESCAPE)
      // Alsays disable shift and ctrl, in case of Ctrl+Shift+ESC.
      // LogState("ESCAPE KEY WAS PRESSED:\n"); // Debug stuff....
      state.ctrl = 0;
      state.shift = 0;
      LastWin.hwnd = nullptr;
      state.ignorekey = 0;    // In case ...
      state.ignoreclick = 0;  // In case ...
      if (state.unikeymenu || g_mchwnd) {
        int ret = IsMenu(state.unikeymenu);
        KillAltSnapMenu();
        if (ret) return 1;
      }

      // Stop current action
      if (state.action.ac || state.alt) {
        action_t action = state.action;
        HideTransWin();
        // Send WM_EXITSIZEMOVE and EVENT_SYSTEM_MOVESIZEEND
        NotifySizeMoveStaEnd(state.hwnd, 0);

        state.alt = 0;
        state.alt1 = 0;

        UnhookMouse();

        // Block ESC if an action was ongoing
        if (action.ac) return 1;
      }
    } else if (!state.ctrl &&
               (state.alt != vkey) /* avoid cursor trapping at first Ctrl dwn if
                                      Ctrl was used as hotkey */
               && (vkey == VK_LCONTROL || vkey == VK_RCONTROL) &&
               (!IsHotkey(vkey) ||
                state.action
                    .ac) /* If Ctrl is an hotkey, skip unless already moving */
               && !(kbh->scanCode &
                    SCANCODE_SIMULATED) /* Ignore ALT GR Scan Code (&0x0200) */
               && !IsModKey(vkey) /*vkey != conf.ModKey*/) {
      RestrictToCurentMonitor();
      // If menu is present inform it that we pressed Ctrl.
      // if (state.unikeymenu) PostMessage(g_mchwnd, WM_CLOSEMODE, 1, 0);
      // LOGA("sC=%lu", kbh->scanCode);
      state.ctrl = 1;
      state.ctrlpt = state.prevpt;  // Save point where ctrl was pressed.
      if (state.action.ac) {
        SetForegroundWindowL(state.hwnd);
      }
    } else if (state.sclickhwnd && g_mchwnd && state.alt &&
               (vkey == VK_LMENU || vkey == VK_RMENU)) {
      // Block Alt down when the altsnap's menu just opened
      if (state.unikeymenu == reinterpret_cast<HMENU>(1) ||
          (IsWindow(state.sclickhwnd) && IsWindow(g_mchwnd) &&
           IsMenu(state.unikeymenu)))
        return 1;
    } else if ((xxbtidx = XXButtonIndex(vkey)) >= 0 &&
               (GetAction(BT_MMB + xxbtidx).ac ||
                GetActionT(BT_MMB + xxbtidx).ac ||
                IsHotclick(BT_MMB + xxbtidx))) {
      if (!state.xxbutton) {
        state.xxbutton = 1;  // To Ignore autorepeat...
        SimulateXButton(WM_XBUTTONDOWN, xxbtidx);
      }
      return 1;

    } else if (conf.UniKeyHoldMenu && (fhwnd = GetForegroundWindow()) &&
               !IsFullScreenBL(fhwnd) &&
               !blacklisted(fhwnd, BlkLst.Processes) &&
               !blacklisted(fhwnd, BlkLst.Windows)) {
      // Key lists used below...
      static const UCHAR ctrlaltwinkeys[] = {VK_CONTROL, VK_MENU, VK_LWIN,
                                             VK_RWIN, 0};
      static const UCHAR menupopdownkeys[] = {
          VK_BACK,  VK_TAB,   VK_APPS, VK_DELETE, VK_SPACE, VK_LEFT,
          VK_RIGHT, VK_PRIOR, VK_NEXT, VK_END,    VK_HOME,  0};
      if (state.unikeymenu && IsMenu(state.unikeymenu) &&
          !(GetKeyState(vkey) & 0x8000)) {
        if (vkey == VK_SNAPSHOT)
          return CallNextHookEx(nullptr, nCode, wParam, lParam);
        if (IsHotkeyy(vkey, menupopdownkeys)) {
          KillAltSnapMenu();
          return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        // Forward all keys to the menu...
        PostMessage(g_mchwnd, WM_KEYDOWN, vkey,
                    0);  // all keys are "directed to the Menu"
        return 1;        // block keydown
      }
      if (!state.ctrl && !state.alt && (0x41 <= vkey && vkey <= 0x5A) &&
          !IsAKeyDown(ctrlaltwinkeys)) {
        // handle long A-Z keydown.
        if (GetKeyState(vkey) & 0x8000) {  // The key is autorepeating.
          if (!IsMenu(state.unikeymenu)) {
            KillAltSnapMenu();
            state.unikeymenu = reinterpret_cast<HMENU>(1);
            g_mchwnd = KreateMsgWin(MenuWindowProc, APP_NAME L"-SClick", 2);
            UCHAR shiftdown =
                GetKeyState(VK_SHIFT) & 0x8000 || GetKeyState(VK_CAPITAL) & 1;
            PostMessage(g_mainhwnd, WM_UNIKEYMENU,
                        reinterpret_cast<WPARAM>(g_mchwnd),
                        vkey | (shiftdown << 8));
          }
          return 1;  // block keydown
        }
      }
    }
  } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
    if (IsHotkey(vkey)) {
      // LOGA("ALT UP");
      HotkeyUp();
    } else if (IsHotkeyy(vkey, conf.Shiftkeys)) {
      state.shift = 0;
      state.usezones = (conf.UseZones & 9) == 9;
      state.snap = conf.AutoSnap;
      // if an action was performed and Alt is still down.
      if (state.alt && state.blockaltup &&
          (vkey == VK_LSHIFT || vkey == VK_RSHIFT))
        SendEndKey();  // send Ctrl to avoid Alt+Shift=>switch keymap
    } else if (state.blockaltup && IsModKey(vkey)) {
      // We release ModKey before Hotkey
      // LOGA("SendCtrlM");
      SendEndKey();
      state.blockaltup = 0;
      // If there is more that one key down remaining
      // then we must block the next alt up.
      if (NumKeysDown() > 1) state.blockaltup = 1;
    } else if ((xxbtidx = XXButtonIndex(vkey)) >= 0) {
      state.xxbutton = 0;
      SimulateXButton(WM_XBUTTONUP, xxbtidx);
      return 1;
    }

    // Always process Ctrl Up.
    if (vkey == VK_LCONTROL || vkey == VK_RCONTROL) {
      // If menu is present inform it that we released Ctrl.
      // if (state.unikeymenu) PostMessage(g_mchwnd, WM_CLOSEMODE, 0, 0);
      ClipCursorOnce(nullptr);  // Release cursor trapping
      state.ctrl = 0;
      // If there is no ongoing action then Control UP prevents AltDragging...
      if (!state.action.ac) state.alt = 0;
    }
  }

  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

}  // namespace altsnap::hooks
