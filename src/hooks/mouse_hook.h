// Interface of mouse_hook.cc (internal to hooks.dll).
#ifndef ALTSNAP_MOUSE_HOOK_H_
#define ALTSNAP_MOUSE_HOOK_H_

#include "hooks/hooks_internal.h"

namespace altsnap::hooks {

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
void HookMouse();
void SetKeepMouseHook(bool keep);
[[nodiscard]] bool KeepMouseHook();
void UnhookMouse();

}  // namespace altsnap::hooks

#endif  // ALTSNAP_MOUSE_HOOK_H_
