// Interface of window_info.cc (internal to hooks.dll).
#ifndef ALTSNAP_WINDOW_INFO_H_
#define ALTSNAP_WINDOW_INFO_H_

#include "hooks/hooks_internal.h"

namespace altsnap::hooks {

[[nodiscard]] bool blacklisted(HWND hwnd, const Blacklist& list);
int isClassName(HWND hwnd, const wchar_t* str);
int IsFullscreenF(HWND hwnd, const RECT* wnd, const RECT* fmon);
int IsFullScreenBL(HWND hwnd);
void NotifySizeMoveStaEnd(HWND hwnd, UCHAR sta);
int HitTestTimeoutbl(HWND hwnd, POINT pt);
void GetMinMaxInfo(HWND hwnd, POINT* Min, POINT* Max);
void SetWindowTrans(HWND hwnd);
void OffsetRectMDI(RECT* wnd);
int ShouldSnapTo(HWND hwnd);
void GetMonitorRect(const POINT* pt, int full, RECT* _mon);
BOOL GetMDInfo(POINT* mdicpt, RECT* wnd);
int IsAltTabAble(HWND window);
int IsToolWindow(HWND hwnd);
HWND MDIorNOT(HWND hwnd, HWND* mdiclient_);
int IsMXRolled(HWND hwnd, const RECT* rc);
int IsDoubleClick(int button);
[[nodiscard]] BOOL IsRectInMonitors(const RECT* rc);
[[nodiscard]] int IsAeraCapbutton(int area);
int InTitlebar(POINT pt, action_t action, enum button button);
[[nodiscard]] int IsAreaLongClikcable(int area);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_WINDOW_INFO_H_
