// Interface of advanced_page.cc (internal).
#ifndef ALTSNAP_ADVANCED_PAGE_H_
#define ALTSNAP_ADVANCED_PAGE_H_

#include "config_internal.h"

namespace altsnap::app {

INT_PTR CALLBACK AdvancedPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam);

}  // namespace altsnap::app

#endif  // ALTSNAP_ADVANCED_PAGE_H_
