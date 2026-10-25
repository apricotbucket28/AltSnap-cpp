#ifndef ALTSNAP_ALTSNAP_H_
#define ALTSNAP_ALTSNAP_H_

#include "hooks.h"

extern HINSTANCE hinstDLL;
extern DWORD ACMenuItems;
extern WNDPROC G_HooksWinProc;
int HookSystem();
extern unsigned char ScrollLockState;
extern char SnapGap;
int UnhookSystem();
void UpdateSettings();
extern UINT WM_TASKBARCREATED;
extern unsigned char elevated;
extern HINSTANCE g_hinst;
extern HWND g_hwnd;
extern wchar_t inipath[MAX_PATH];

// True while the hooks are installed.
[[nodiscard]] inline bool IsEnabled() {
  return G_HooksWinProc != nullptr;
}

// GetWindowRect() that applies the user's snap gap (see unfuck.h).
inline BOOL GetWindowRectL(HWND hwnd, RECT* rect) {
  return GetWindowRectLL(hwnd, rect, SnapGap);
}

#endif  // ALTSNAP_ALTSNAP_H_
