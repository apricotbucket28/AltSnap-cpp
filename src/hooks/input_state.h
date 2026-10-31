// Interface of input_state.cc (internal to hooks.dll).
#ifndef ALTSNAP_INPUT_STATE_H_
#define ALTSNAP_INPUT_STATE_H_

#include "hooks/hooks_internal.h"

namespace altsnap::hooks {

[[nodiscard]] int IsAKeyDown(const UCHAR* k);
[[nodiscard]] int ModKey();
action_t GetAction(const int button);
action_t GetActionT(const int button);
action_t GetActionMR(const int button);
[[nodiscard]] int IsHotkey(UCHAR vkey);
[[nodiscard]] int IsHotclick(UCHAR vkey);
[[nodiscard]] int IsKillkey(unsigned char a);
[[nodiscard]] int IsModKey(const UCHAR vkey);
UCHAR TotNumberOfKeysDown();
int IsHotkeyDown();
int NumKeysDown();
int IsCtrlDown();
int ScrollLockState();
[[nodiscard]] int XXButtonIndex(UCHAR vkey);
[[nodiscard]] int GetButton(WPARAM wp, LPARAM lp);
[[nodiscard]] enum buttonstate GetButtonState(WPARAM wp);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_INPUT_STATE_H_
