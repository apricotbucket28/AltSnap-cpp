#ifndef ALTSNAP_HOOKS_HOOK_STARTUP_H_
#define ALTSNAP_HOOKS_HOOK_STARTUP_H_

#include "hooks/hooks.h"
#include "hooks/overlay/trans_window.h"
#include "hooks/worker.h"

namespace altsnap::hooks {
// Internal startup seam; the DLL's Load export always uses the Win32 defaults.
struct HookStartup {
  Worker::Startup worker;
  decltype(&SetWindowsHookExW) keyboard_hook = SetWindowsHookExW;
  bool (*create_trans_window)(const IniSection&) = CreateTransWin;
};
WNDPROC LoadSession(HWND window, const wchar_t* path,
                    const HookStartup& startup);
}  // namespace altsnap::hooks

#endif  // ALTSNAP_HOOKS_HOOK_STARTUP_H_
