// Interface of mouse_hook.cc (internal to hooks.dll).
#ifndef ALTSNAP_MOUSE_HOOK_H_
#define ALTSNAP_MOUSE_HOOK_H_

#include "hooks_internal.h"

namespace altsnap::hooks {

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
void HookMouse();
void UnhookMouse();

}  // namespace altsnap::hooks

#endif  // ALTSNAP_MOUSE_HOOK_H_
