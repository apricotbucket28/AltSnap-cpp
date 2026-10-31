#ifndef ALTSNAP_APP_HOOK_SHUTDOWN_H_
#define ALTSNAP_APP_HOOK_SHUTDOWN_H_

#include "hooks/hooks.h"

namespace altsnap::app {
// Sent messages are serviced while joining the hooks worker. Do not enter
// menus, dialogs or a recursive settings reload from that restricted pump.
// HookLifecycle captures these protocols and posts them after shutdown.
inline bool DeferDuringHookShutdown(bool shutting_down, UINT message) {
  if (!shutting_down) return false;
  return message == WM_TRAY || message == WM_SCLICK ||
         message == WM_UNIKEYMENU || message == WM_OPENCONFIG ||
         message == WM_UPDATESETTINGS || message == WM_COMMAND;
}
}  // namespace altsnap::app

#endif  // ALTSNAP_APP_HOOK_SHUTDOWN_H_
