// Interface of window_actions.cc (internal to hooks.dll).
#ifndef ALTSNAP_WINDOW_ACTIONS_H_
#define ALTSNAP_WINDOW_ACTIONS_H_

#include "hooks/hooks_internal.h"

namespace altsnap::hooks {

int ActionPause(HWND hwnd, UCHAR pause);
int ActionKill(HWND hwnd);
void ReallySetForegroundWindow(HWND hwnd);
void SetForegroundWindowL(HWND hwnd);
void ToggleSnapState();
int ScrollPointedWindow(POINT pt, int delta, WPARAM wParam);
int ActionAltTab(POINT pt, short delta, short laser, WNDENUMPROC lpEnumFunc);
int ActionTransparency(HWND hwnd, short delta);
void ActionTransparency2(HWND hwnd, action_t action);
void ActionLower(HWND hwnd, short delta, UCHAR shift, UCHAR fg);
void ActionMaxRestMin(HWND hwnd, int delta);
void ActionBrightness(const POINT pt, const short delta);
void RollWindow(HWND hwnd, int delta);
int ActionZoom(HWND hwnd, short delta, short center);
int ActionMove(POINT pt, int button);
int ActionResize(POINT pt, const RECT* wnd, int button);
void ActionBorderless(HWND hwnd);
void CenterWindow(HWND hwnd, unsigned flags, int full_monitor);
void ActionMaximize(HWND hwnd);
void MaximizeHV(HWND hwnd, int horizontal);
void MinimizeAllOtherWindows(HWND hwnd, int CurrentMonOnly);
void StepWindow(HWND hwnd, action_t action);
void ActionASOnOff();
void ActionMoveOnOff(HWND hwnd);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_WINDOW_ACTIONS_H_
