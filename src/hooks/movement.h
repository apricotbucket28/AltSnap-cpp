// Interface of movement.cc (internal to hooks.dll).
#ifndef ALTSNAP_MOVEMENT_H_
#define ALTSNAP_MOVEMENT_H_

#include "hooks/hooks_internal.h"

namespace altsnap::hooks {

void StopSpeedMes();
int init_movement_and_actions(POINT pt, HWND hwnd, action_t action,
                              int buttonX);
void FinishMovementAsync();
void LockMovement();

}  // namespace altsnap::hooks

#endif  // ALTSNAP_MOVEMENT_H_
