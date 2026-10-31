// Interface of trans_window.cc (internal to hooks.dll).
#ifndef ALTSNAP_TRANS_WINDOW_H_
#define ALTSNAP_TRANS_WINDOW_H_

#include "hooks/hooks_internal.h"

namespace altsnap::hooks {

void HideCursor();
void ClipCursorOnce(const RECT* clip);
void RestrictCursorToMon();
void ShowTransWin(int nCmdShow);
void HideTransWin();
BOOL IsTransWinVisible();
void MoveTransWin(struct windowRR* lw);
void RestrictToCurentMonitor();
void UpdateCursor(POINT pt);
[[nodiscard]] bool CreateTransWin(const IniSection& section);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_TRANS_WINDOW_H_
