// Interface of click_actions.cc (internal to hooks.dll).
#ifndef ALTSNAP_CLICK_ACTIONS_H_
#define ALTSNAP_CLICK_ACTIONS_H_

#include "hooks_internal.h"

namespace altsnap::hooks {

void SClickActions(HWND hwnd, action_t action);
int DoWheelActions(HWND hwnd, action_t action);
int TitleBarActions(POINT pt, action_t action, enum button button);
void DoComboActions(action_t action, enum button button);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_CLICK_ACTIONS_H_
