// Interface of blacklist_page.cc (internal).
#ifndef ALTSNAP_BLACKLIST_PAGE_H_
#define ALTSNAP_BLACKLIST_PAGE_H_

#include "config_internal.h"

namespace altsnap::app {

INT_PTR CALLBACK BlacklistPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam);

}  // namespace altsnap::app

#endif  // ALTSNAP_BLACKLIST_PAGE_H_
