// Interface of general_page.cc (internal).
#ifndef ALTSNAP_GENERAL_PAGE_H_
#define ALTSNAP_GENERAL_PAGE_H_

#include "ui/config_internal.h"

namespace altsnap::app {

INT_PTR CALLBACK GeneralPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam);

}  // namespace altsnap::app

#endif  // ALTSNAP_GENERAL_PAGE_H_
