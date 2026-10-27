// Interface of keyboard_hook.cc (internal to hooks.dll).
#ifndef ALTSNAP_KEYBOARD_HOOK_H_
#define ALTSNAP_KEYBOARD_HOOK_H_

#include "hooks_internal.h"

namespace altsnap::hooks {

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_KEYBOARD_HOOK_H_
