// Interface of click_actions.cc (internal to hooks.dll).
#ifndef ALTSNAP_CLICK_ACTIONS_H_
#define ALTSNAP_CLICK_ACTIONS_H_

#include <optional>

#include "hooks/hooks_internal.h"
#include "hooks/movement.h"

namespace altsnap::hooks {

void DoMoveResizeMaxMinComboAction();

void SClickActions(HWND hwnd, action_t action);
int DoWheelActions(HWND hwnd, action_t action);
std::optional<ActionStartResult> TitleBarActions(POINT pt, action_t action,
                                                 enum button button);
void DoComboActions(action_t action, enum button button);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_CLICK_ACTIONS_H_
