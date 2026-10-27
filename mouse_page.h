// Interface of mouse_page.cc (internal).
#ifndef ALTSNAP_MOUSE_PAGE_H_
#define ALTSNAP_MOUSE_PAGE_H_

#include "config_internal.h"

namespace altsnap::app {

void FillActionDropListS(HWND hwnd, int idc, const wchar_t* inioption,
                         const actiondl_t* actions);
void WriteActionDropListS(HWND hwnd, int idc, const wchar_t* inioption,
                          const actiondl_t* actions);
INT_PTR CALLBACK MousePageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam);
LRESULT WINAPI PickShortcutWinProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
INT_PTR CALLBACK AdvancedActionDlgProc(HWND hwnd, UINT msg, WPARAM wp,
                                       LPARAM lp);

}  // namespace altsnap::app

#endif  // ALTSNAP_MOUSE_PAGE_H_
