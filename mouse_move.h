// Interface of mouse_move.cc (internal to hooks.dll).
#ifndef ALTSNAP_MOUSE_MOVE_H_
#define ALTSNAP_MOUSE_MOVE_H_

#include "hooks_internal.h"

namespace altsnap::hooks {

void MouseMoveNow(POINT pt);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_MOUSE_MOVE_H_
