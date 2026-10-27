// input state (split out of hooks.cc).

#include "hooks/input_state.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "common/ini_section.h"
#include "hooks/blacklist.h"
#include "hooks/hooks_internal.h"
#include "hooks/movement.h"
#include "hooks/window/snap.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

[[nodiscard]] int IsAKeyDown(const UCHAR* k) {
  while (*k) {
    if (GetKeyState(*k++) & 0x8000) return 1;
  }
  return 0;
}
/////////////////////////////////////////////////////////////////////////////
// Mod Key can return 0 or 1, maybe more in the future...
[[nodiscard]] int ModKey() {
  return conf.ModKey[0] && IsAKeyDown(conf.ModKey);
}
///////////////////////////////////////////////////////////////////////////
// Get action of button
action_t GetAction(const int button) {
  if (button) {  // Ugly pointer arithmetic (LMB <==> button == 2)
    return conf.Mouse[(button - 2) * NACPB + ModKey()];
  }
  return k_action_none;
}
action_t GetActionT(const int button) {
  if (button) {  // Ugly pointer arithmetic +2 compared to non titlebar
    return conf.Mouse[2 + (button - 2) * NACPB + ModKey()];
  }
  return k_action_none;
}
action_t GetActionMR(const int button) {
  if (button) {
    // Ugly pointer arithmetic
    // state.action == 1 or 2
    // MB[4/5] == Action/Alt while moving
    // MB[6/7] == Action/Alt while Resizing
    int offset = state.action.ac << 1;  // 2 or 4
    return conf.Mouse[2 + offset + (button - 2) * NACPB + ModKey()];
  }
  return k_action_none;
}
[[nodiscard]] int IsHotkey(UCHAR vkey) {
  return IsHotkeyy(vkey, conf.Hotkeys);
}
[[nodiscard]] int IsHotclick(UCHAR vkey) {
  return IsHotkeyy(vkey, conf.Hotclick);
}
[[nodiscard]] int IsKillkey(unsigned char a) {
  return (0x41 <= a && a <= 0x5A)  // A-Z vkeys
         || IsHotkeyy(a, conf.Killkey);
}
[[nodiscard]] int IsModKey(const UCHAR vkey) {
  return IsHotkeyy(vkey, conf.ModKey);
}

UCHAR TotNumberOfKeysDown() {
  BYTE kb_state[256];
  GetKeyState(0);  // You need that for GetKeyboardState()
  GetKeyboardState(kb_state);
  UCHAR numkeys = 0;
  for (BYTE i = 0x13; i < 0xFF; i++) {
    // vK codes go from 0 to 254 and we must skip a few
    if ((0x3A <= i && i <= 0x40)     // Undefineds
        || i == 0x5E                 // Reserved
        || (0x88 <= i && i <= 0x8F)  // Unassigned
        || (0x97 <= i && i <= 0x9F)  // Unassigned
        || (0xB8 <= i && i <= 0xB9)  // Reserved
        || (0xC1 <= i && i <= 0xDA)  // Reserved + Unassigned (D8-DA)
        || i == 0xE0                 // Reserved
        || i == 0xE8                 // Unassigned
    )
      continue;

    numkeys += !!(kb_state[i] & 0x80);
    /*!!(GetKeyState(i)&0x8000)*/
  }
  return numkeys;
}
// Return true if required amount of hotkeys are holded.
// If KeyCombo is disabled, user needs to hold only one hotkey.
// Otherwise, user needs to hold at least two hotkeys.
int IsHotkeyDown() {
  // required keys 1 or 2
  UCHAR ckeys = 1 + conf.KeyCombo;

  // loop over all hotkeys
  const UCHAR* pos = &conf.Hotkeys[0];
  while (*pos && ckeys) {
    // check if key is held down. Use GetKeyState()?
    ckeys -= !!(GetAsyncKeyState(*pos++) & 0x8000);
  }
  // return true if required amount of hotkeys are down
  return !ckeys &&
         (!conf.MaxKeysNum || TotNumberOfKeysDown() <= conf.MaxKeysNum);
}

// returns the number of hotkeys/ModKeys that are pressed.
int NumKeysDown() {
  UCHAR keys = 0;
  // loop over all hotkeys
  const UCHAR* Hpos = &conf.Hotkeys[0];
  const UCHAR* Mpos = &conf.ModKey[0];
  while (*Hpos) {
    // check if key is held down
    keys += !!(GetKeyState(*Hpos++) & 0x8000);
    keys += !!(GetKeyState(*Mpos++) & 0x8000);
  }
  return keys;
}

// Double check if Ctrl is down
int IsCtrlDown() {
  return state.ctrl && GetAsyncKeyState(VK_CONTROL) & 0x8000;
}
// Returns true if AltDrag must be disabled based on scroll lock
// If conf.ScrollLockState&2 then Altdrag is disabled by Scroll Lock
// otherwise it is enabled by Scroll lock.
int ScrollLockState() {
  if (state.altsnaponoff) return 1;  // AltSnap was disabled by AC_ASONOFF

  if ((conf.ScrollLockState & 1) &&
      !(!(GetKeyState(VK_SCROLL) & 1) ^ !(conf.ScrollLockState & 2))) {
    if (state.action.ac) FinishMovementAsync();
    return 1;
  }
  return 0;
}
[[nodiscard]] int XXButtonIndex(UCHAR vkey) {
  for (WORD i = 0; i < MAXKEYS && conf.XXButtons[i]; i++) {
    if (conf.XXButtons[i] == vkey) return i + 3;
  }
  return -1;
}
/////////////////////////////////////////////////////////////////////////////
//
[[nodiscard]] int GetButton(WPARAM wp, LPARAM lp) {
  auto msg = reinterpret_cast<PMSLLHOOKSTRUCT>(lp);
  return (wp == WM_LBUTTONDOWN || wp == WM_LBUTTONUP)   ? BT_LMB
         : (wp == WM_MBUTTONDOWN || wp == WM_MBUTTONUP) ? BT_MMB
         : (wp == WM_RBUTTONDOWN || wp == WM_RBUTTONUP) ? BT_RMB
         : (wp == WM_MOUSEWHEEL)
             ? BT_WHEELD + (GET_WHEEL_DELTA_WPARAM(msg->mouseData) > 0)
         : (wp == WM_MOUSEHWHEEL)
             ? BT_HWHEELD + (GET_WHEEL_DELTA_WPARAM(msg->mouseData) > 0)
         : (wp == WM_XBUTTONDOWN || wp == WM_XBUTTONUP)
             ? BT_MB4 - 1 + HIWORD(msg->mouseData)
             : BT_NONE;
}
[[nodiscard]] enum buttonstate GetButtonState(WPARAM wp) {
  return (wp == WM_LBUTTONDOWN || wp == WM_MBUTTONDOWN ||
          wp == WM_RBUTTONDOWN || wp == WM_XBUTTONDOWN)
             ? STATE_DOWN
         : (wp == WM_LBUTTONUP || wp == WM_MBUTTONUP || wp == WM_RBUTTONUP ||
            wp == WM_XBUTTONUP)
             ? STATE_UP
         : (wp == WM_MOUSEWHEEL || wp == WM_MOUSEHWHEEL) ? STATE_DOWN
                                                         : STATE_NONE;
}

}  // namespace altsnap::hooks
