// Interface of pin_window.cc (internal to hooks.dll).
#ifndef ALTSNAP_PIN_WINDOW_H_
#define ALTSNAP_PIN_WINDOW_H_

#include "hooks/hooks_internal.h"

namespace altsnap::hooks {

BOOL CALLBACK PostPinWindowsProcMessage(HWND hwnd, LPARAM lp);
HWND GetPinWindow(HWND owner);
void TogglesAlwaysOnTop(HWND hwnd);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_PIN_WINDOW_H_
