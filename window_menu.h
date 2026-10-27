// Interface of window_menu.cc (internal to hooks.dll).
#ifndef ALTSNAP_WINDOW_MENU_H_
#define ALTSNAP_WINDOW_MENU_H_

#include "hooks_internal.h"

namespace altsnap::hooks {

void KillAltSnapMenu();
void TrackMenuOfWindows(WNDENUMPROC EnumProc, LPARAM flags);
void ActionStackList(int lasermode);
void ActionMenu(HWND hwnd);
LRESULT CALLBACK MenuWindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                LPARAM lParam);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_WINDOW_MENU_H_
