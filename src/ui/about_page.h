// Interface of about_page.cc (internal).
#ifndef ALTSNAP_ABOUT_PAGE_H_
#define ALTSNAP_ABOUT_PAGE_H_

#include "ui/config_internal.h"

namespace altsnap::app {

INT_PTR CALLBACK AboutPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam);

}  // namespace altsnap::app

#endif  // ALTSNAP_ABOUT_PAGE_H_
