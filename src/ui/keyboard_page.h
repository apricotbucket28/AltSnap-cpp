// Interface of keyboard_page.cc (internal).
#ifndef ALTSNAP_KEYBOARD_PAGE_H_
#define ALTSNAP_KEYBOARD_PAGE_H_

#include "ui/config_internal.h"

namespace altsnap::app {

void SaveHotKeys(const struct hk_struct* const hotkeys, HWND hwnd,
                 const wchar_t* const name);
void CheckConfigHotKeys(const struct hk_struct* hotkeys, HWND hwnd,
                        const wchar_t* hotkeystr, const wchar_t* def);
INT_PTR CALLBACK KeyboardPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam);

}  // namespace altsnap::app

#endif  // ALTSNAP_KEYBOARD_PAGE_H_
