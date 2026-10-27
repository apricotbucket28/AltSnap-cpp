/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright (C) 2015    Stefan Sundin                                   *
 * This program is free software: you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation, either version 3 or later.              *
 * Modified By Raymond Gillibert in 2020                                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "config.h"

#include <algorithm>

#include "about_page.h"
#include "advanced_page.h"
#include "altsnap.h"
#include "blacklist_page.h"
#include "config_internal.h"
#include "dialog_util.h"
#include "general_page.h"
#include "hooks.h"
#include "ini_section.h"
#include "keyboard_page.h"
#include "languages.h"
#include "mouse_page.h"
#include "resource.h"
#include "tray.h"

namespace altsnap::app {

#ifndef TTM_SETMAXTIPWIDTH
#endif
#ifndef PSH_NOCONTEXTHELP
#endif

static BOOL CALLBACK PropSheetProc(HWND, UINT, LPARAM);

HWND g_cfgwnd = nullptr;
/////////////////////////////////////////////////////////////////////////////
// Entry point
void OpenConfig(int startpage) {
  ListAllTranslations();  // In case
  if (IsWindow(g_cfgwnd)) {
    PropSheet_SetCurSel(g_cfgwnd, 0, startpage);
    SetForegroundWindow(g_cfgwnd);
    return;
  }
  // Define the pages
  static const struct {
    WORD pszTemplate;
    DLGPROC pfnDlgProc;
  } pages[] = {
      {IDD_GENERALPAGE, static_cast<DLGPROC>(GeneralPageDialogProc)},
      {IDD_MOUSEPAGE, static_cast<DLGPROC>(MousePageDialogProc)},
      {IDD_KBPAGE, static_cast<DLGPROC>(KeyboardPageDialogProc)},
      {IDD_BLACKLISTPAGE, static_cast<DLGPROC>(BlacklistPageDialogProc)},
      {IDD_ADVANCEDPAGE, static_cast<DLGPROC>(AdvancedPageDialogProc)},
      {IDD_ABOUTPAGE, static_cast<DLGPROC>(AboutPageDialogProc)}};
  PROPSHEETPAGE psp[std::size(pages)];
  ZeroObject(psp);
  for (size_t i = 0; i < std::size(pages); i++) {
    psp[i].dwSize = sizeof(*psp);
    psp[i].hInstance = g_hinst;
    psp[i].pszTemplate = MAKEINTRESOURCE(pages[i].pszTemplate);
    psp[i].pfnDlgProc = pages[i].pfnDlgProc;
  }

  // Define the property sheet
  PROPSHEETHEADER psh;
  ZeroObject(psh);
  psh.dwSize = sizeof(psh);
  psh.dwFlags =
      PSH_PROPSHEETPAGE | PSH_USECALLBACK | PSH_USEHICON | PSH_NOCONTEXTHELP;
  psh.hwndParent = nullptr;
  psh.hInstance = g_hinst;
  psh.hIcon = g_icons[1];  // LoadIcon(g_hinst, iconstr[1]);
  psh.pszCaption = APP_NAME;
  psh.nPages = std::size(psp);
  psh.ppsp = psp;
  psh.pfnCallback = PropSheetProc;
  psh.nStartPage = startpage;

  // Open the property sheet
  InitCommonControls();
  PropertySheet(&psh);
}
/////////////////////////////////////////////////////////////////////////////
void CloseConfig() {
  PostMessage(g_cfgwnd, WM_CLOSE, 0, 0);
}
/////////////////////////////////////////////////////////////////////////////
void UpdateStrings() {
  // Update window title
  PropSheet_SetTitle(g_cfgwnd, 0, l10n->ConfigTitle);

  // Update tab titles
  // tc = PropSheet_GetTabControl(g_cfgwnd);
  HWND tc =
      reinterpret_cast<HWND>(SendMessage(g_cfgwnd, PSM_GETTABCONTROL, 0, 0));
  int numrows_prev = TabCtrl_GetRowCount(tc);
  const wchar_t* titles[6];
  titles[0] = l10n->ConfigTabGeneral;
  titles[1] = l10n->ConfigTabMouse;
  titles[2] = l10n->ConfigTabKeyboard;
  titles[3] = l10n->ConfigTabBlacklist;
  titles[4] = l10n->ConfigTabAdvanced;
  titles[5] = l10n->ConfigTabAbout;
  for (size_t i = 0; i < std::size(titles); i++) {
    TCITEM ti;
    ti.mask = TCIF_TEXT;
    ti.pszText = const_cast<wchar_t*>(titles[i]);
    TabCtrl_SetItem(tc, i, &ti);
  }

  // Modify UI if number of rows have changed
  int numrows = TabCtrl_GetRowCount(tc);
  if (numrows_prev != numrows) {
    HWND page = PropSheet_GetCurrentPageHwnd(g_cfgwnd);
    if (page != nullptr) {
      int diffrows = numrows - numrows_prev;
      WINDOWPLACEMENT wndpl;
      wndpl.length = sizeof(wndpl);
      // Resize window
      GetWindowPlacement(g_cfgwnd, &wndpl);
      wndpl.rcNormalPosition.bottom += 18 * diffrows;
      SetWindowPlacement(g_cfgwnd, &wndpl);
      // Resize tabcontrol
      GetWindowPlacement(tc, &wndpl);
      wndpl.rcNormalPosition.bottom += 18 * diffrows;
      SetWindowPlacement(tc, &wndpl);
      // Move button
      MoveButtonUporDown(IDOK, &wndpl, diffrows);
      MoveButtonUporDown(IDCANCEL, &wndpl, diffrows);
      MoveButtonUporDown(IDAPPLY, &wndpl, diffrows);
      // Re-select tab
      PropSheet_SetCurSel(g_cfgwnd, page, 0);
      // Invalidate region
      GetWindowPlacement(g_cfgwnd, &wndpl);
      InvalidateRect(g_cfgwnd, &wndpl.rcNormalPosition, TRUE);
    }
  }
}

/////////////////////////////////////////////////////////////////////////////
static BOOL CALLBACK PropSheetProc(HWND hwnd, UINT msg, LPARAM lParam) {
  if (msg == PSCB_INITIALIZED) {
    g_cfgwnd = hwnd;
    UpdateStrings();

    // Set new icon specifically for the taskbar and Alt+Tab, without changing
    // window icon
    // HICON taskbar_icon = (HICON)LoadImageA(g_hinst,
    // MAKEINTRESOURCEA(APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR);
    HICON taskbar_icon = LoadIcon(g_hinst, MAKEINTRESOURCE(APP_ICON));
    SendMessage(g_cfgwnd, WM_SETICON, ICON_BIG,
                reinterpret_cast<LPARAM>(taskbar_icon));
  }
  return TRUE;
}

}  // namespace altsnap::app
