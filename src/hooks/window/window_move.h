// Interface of window_move.cc (internal to hooks.dll).
#ifndef ALTSNAP_WINDOW_MOVE_H_
#define ALTSNAP_WINDOW_MOVE_H_

#include "hooks/hooks_internal.h"

namespace altsnap::hooks {

int ResizeTouchingWindows(LPVOID lwptr);
void ResizeAllSnappedWindows();
void MaximizeRestoreAtPoint(HWND hwnd, UINT sw_cmd, int origin);
void RestoreWindowTo(HWND hwnd, int x, int y, int w, int h);
BOOL LetWindowKickBack(HWND hwnd, struct windowRR* lw);
DWORD WINAPI MoveWindowNow(struct windowRR* lw);
void RestoreOldWin(const POINT pt, unsigned was_snapped, RECT* ownd);
void RestoreToMonitorSize(HWND hwnd, RECT* wnd);
void ClearRestoreFlagOnResizeIfNeeded(HWND hwnd);
void MoveToCurrentMonitorIfNeeded(HWND hwnd);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_WINDOW_MOVE_H_
