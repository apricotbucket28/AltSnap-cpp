/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright (C) 2015    Stefan Sundin                                   *
 * This program is free software: you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation, either version 3 or later.              *
 * Modified By Raymond Gillibert in 2020                                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "config.h"

#include <commctrl.h>

#include <algorithm>

#include "altsnap.h"
#include "hooks.h"
#include "ini_section.h"
#include "languages.h"
#include "resource.h"
#include "tray.h"

#ifndef TTM_SETMAXTIPWIDTH
#define TTM_SETMAXTIPWIDTH (WM_USER + 24)
#endif
#ifndef PSH_NOCONTEXTHELP
#define PSH_NOCONTEXTHELP 0x02000000
#endif

static BOOL CALLBACK PropSheetProc(HWND, UINT, LPARAM);
static INT_PTR CALLBACK GeneralPageDialogProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK MousePageDialogProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK KeyboardPageDialogProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK BlacklistPageDialogProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK AboutPageDialogProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK AdvancedPageDialogProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK FindWindowProc(HWND, UINT, WPARAM, LPARAM);

static HWND g_cfgwnd = nullptr;

/////////////////////////////////////////////////////////////////////////////
// No error reporting since we don't want the user to be interrupted
static void CheckAutostart(int* _on, int* _hidden, int* _elevated) {
  *_on = *_hidden = *_elevated = 0;
  // Read registry
  HKEY key = nullptr;
  wchar_t value[MAX_PATH + 20];
  value[0] = L'\0';
  DWORD len = sizeof(value);
  RegOpenKeyEx(HKEY_CURRENT_USER,
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
               KEY_QUERY_VALUE, &key);
  RegQueryValueEx(key, APP_NAME, nullptr, nullptr,
                  reinterpret_cast<LPBYTE>(value), &len);
  RegCloseKey(key);

  // Compare to what it should be
  wchar_t compare[MAX_PATH + 20];
  GetModuleFileName(nullptr, &compare[1], MAX_PATH);
  compare[0] = '\"';
  unsigned ll = lstrlen(compare);
  compare[ll] = '\"';
  compare[++ll] = '\0';

  if (lstrstr(value, compare) != value) {
    return;
  }
  // Autostart is on, check arguments
  *_on = 1;
  if (lstrstr(value, L" -hide") != nullptr) {
    *_hidden = 1;
  }
  if (lstrstr(value, L" -elevate") != nullptr) {
    *_elevated = 1;
  }
}

/////////////////////////////////////////////////////////////////////////////
static void SetAutostart(int on, int hhide, int eelevate) {
  // Open key
  HKEY key = nullptr;
  int error = RegCreateKeyEx(
      HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
      0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
  if (error != ERROR_SUCCESS) return;
  if (on) {
    // Get path
    wchar_t value[MAX_PATH + 20];
    GetModuleFileName(nullptr, &value[1], MAX_PATH);
    value[0] = '\"';
    unsigned ll = lstrlen(value);
    value[ll] = '\"';
    value[++ll] = '\0';
    // Add -hide or -elevate flags
    if (hhide) lstrcat_s(value, std::size(value), L" -hide");
    if (eelevate) lstrcat_s(value, std::size(value), L" -elevate");
    // Set autostart
    RegSetValueEx(key, APP_NAME, 0, REG_SZ, reinterpret_cast<LPBYTE>(value),
                  (lstrlen(value) + 1) * sizeof(value[0]));
  } else {
    // Remove
    RegDeleteValue(key, APP_NAME);
  }
  // Close key
  RegCloseKey(key);
}

/////////////////////////////////////////////////////////////////////////////
// Only used in the case of Vista+
BOOL ElevateNow(int showconfig) {
  wchar_t path[MAX_PATH];
  GetModuleFileName(nullptr, path, std::size(path));
  INT_PTR ret = 0;
  if (showconfig)
    ret = reinterpret_cast<INT_PTR>(ShellExecute(
        nullptr, L"runas", path, L"-config -multi", nullptr, SW_SHOWNORMAL));
  else
    ret = reinterpret_cast<INT_PTR>(ShellExecute(
        nullptr, L"runas", path, L"-multi", nullptr, SW_SHOWNORMAL));

  if (ret > 32) {
    PostMessage(g_hwnd, WM_CLOSE, 0, 0);
  } else {
    MessageBox(nullptr, l10n->GeneralElevationAborted, APP_NAME,
               MB_ICONINFORMATION | MB_OK);
  }
  return FALSE;
}
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

static void MoveButtonUporDown(WORD id, WINDOWPLACEMENT* wndpl, int diffrows) {
  HWND button = GetDlgItem(g_cfgwnd, id);
  GetWindowPlacement(button, wndpl);
  int height = wndpl->rcNormalPosition.bottom - wndpl->rcNormalPosition.top;
  wndpl->rcNormalPosition.top += 18 * diffrows;
  wndpl->rcNormalPosition.bottom = wndpl->rcNormalPosition.top + height;
  SetWindowPlacement(button, wndpl);
}
/////////////////////////////////////////////////////////////////////////////
static void UpdateStrings() {
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
static void MoveToCorner(HWND hwnd) {
  hwnd = GetAncestor(hwnd, GA_ROOT);
  static HWND ohwnd;
  if (hwnd == ohwnd) return;
  ohwnd = hwnd;

  RECT rc;
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  POINT pt;
  GetCursorPos(&pt);
  GetMonitorInfo(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);

  GetWindowRect(hwnd, &rc);
  int x = 0, y = 0;
  long h = rc.bottom - rc.top;
  long w = rc.right - rc.left;
  if (pt.x < (mi.rcWork.right - mi.rcWork.left) / 2) {
    x = mi.rcWork.left;
  } else {
    x = mi.rcWork.right - w;
  }
  if (pt.y < (mi.rcWork.bottom - mi.rcWork.top) / 2) {
    y = mi.rcWork.top;
  } else {
    y = mi.rcWork.bottom - h;
  }
  SetWindowPos(hwnd, nullptr, x, y, w, h,
               SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOSIZE |
                   SWP_ASYNCWINDOWPOS);
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
/////////////////////////////////////////////////////////////////////////////
static DWORD IsUACEnabled() {
  DWORD uac_enabled = 0;
  if (elevated) {
    DWORD len = sizeof(uac_enabled);
    HKEY key = nullptr;
    RegOpenKeyEx(
        HKEY_LOCAL_MACHINE,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0,
        KEY_QUERY_VALUE, &key);

    RegQueryValueEx(key, L"EnableLUA", nullptr, nullptr,
                    reinterpret_cast<LPBYTE>(&uac_enabled), &len);
    RegCloseKey(key);
  }
  return uac_enabled;
}
/////////////////////////////////////////////////////////////////////////////
// Helper functions and Macro
#define IsChecked(idc) IsDlgButtonChecked(hwnd, idc)
// #define IsCheckedW(idc) itostr(IsChecked(idc), txt, 10)

#define CB_ResetContent(hwnd) SendMessage(hwnd, CB_RESETCONTENT, 0, 0)
#define CB_AddString(hwnd, txt) \
  SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)(const wchar_t*)txt)
#define CB_DeleteString(hwnd, i) SendMessage(hwnd, CB_DELETESTRING, i, 0)
#define CB_SetCurSel(hwnd, i) SendMessage(hwnd, CB_SETCURSEL, i, 0)
#define CB_GetCurSel(hwnd) (int)(DWORD)SendMessage(hwnd, CB_GETCURSEL, 0, 0)
static int CB_GetCurSelDlgItem(HWND hwnd, UINT id) {
  return static_cast<int>(static_cast<DWORD>(
      SendMessage(GetDlgItem(hwnd, id), CB_GETCURSEL, 0, 0)));
}
#define CB_GetCurSelId(id) CB_GetCurSelDlgItem(hwnd, id)
#define CB_GetText(id, txt, txtlen)                                         \
  (int)(DWORD)SendMessage(GetDlgItem(hwnd, id), WM_GETTEXT, (WPARAM)txtlen, \
                          (LPARAM)txt)

static LRESULT CB_GetTextAndIdx(HWND hwnd, wchar_t* txt, size_t txtlen) {
  LRESULT idx = SendMessage(hwnd, CB_GETCURSEL, 0, 0);
  if (idx < 0) {
    GetWindowText(hwnd, txt, txtlen);  // Edit mode
  } else {
    size_t itemlen = SendMessage(hwnd, CB_GETLBTEXTLEN, idx, 0);
    if (txtlen > itemlen)  // We have enough space
      SendMessage(hwnd, CB_GETLBTEXT, idx, reinterpret_cast<LPARAM>(txt));
  }
  return idx;
}

static LRESULT CB_AddStringIfNotPresentAndGetIdx(HWND hwnd,
                                                 const wchar_t* txt) {
  LRESULT new_index =
      SendMessage(hwnd, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                  reinterpret_cast<LPARAM>(txt));
  if (new_index < 0) new_index = CB_AddString(hwnd, txt);
  return new_index;
}
static void WriteOptionBoolW(HWND hwnd, WORD id, const wchar_t* section,
                             const char* name_s) {
  wchar_t name[64];
  str2tchar(name, name_s);
  WritePrivateProfileString(
      section, name, IsDlgButtonChecked(hwnd, id) ? L"1" : L"0", inipath);
}
#define WriteOptionBool(id, section, name) \
  WriteOptionBoolW(hwnd, id, section, name)
static int WriteOptionBoolBW(HWND hwnd, WORD id, const wchar_t* section,
                             const char* name_s, int bit) {
  wchar_t txt[kUintDigits + 1];
  wchar_t name[64];
  str2tchar(name, name_s);
  int val = GetPrivateProfileInt(section, name, 0, inipath);
  if (IsDlgButtonChecked(hwnd, id))
    val = SetBit(val, bit);
  else
    val = ClearBit(val, bit);

  WritePrivateProfileString(section, name, Uint2lStr(txt, val), inipath);
  return val;
}
#define WriteOptionBoolB(id, section, name, bit) \
  WriteOptionBoolBW(hwnd, id, section, name, bit)

static void WriteOptionStrW(HWND hwnd, WORD id, const wchar_t* section,
                            const char* name_s) {
  wchar_t name[64];
  str2tchar(name, name_s);
  HWND item = GetDlgItem(hwnd, id);
  const int len = GetWindowTextLength(item);
  std::wstring text(static_cast<size_t>(len) + 1, L'\0');
  GetWindowText(item, text.data(), len + 1);
  WritePrivateProfileString(section, name, text.c_str(), inipath);
}
#define WriteOptionStr(id, section, name) \
  WriteOptionStrW(hwnd, id, section, name)

static void ReadOptionStrW(HWND hwnd, WORD id, const wchar_t* section,
                           const char* name_s, const wchar_t* def) {
  wchar_t name[64];
  str2tchar(name, name_s);
  SetDlgItemText(hwnd, id, ReadIniString(section, name, def, inipath).c_str());
}
#define ReadOptionStr(id, section, name, def) \
  ReadOptionStrW(hwnd, id, section, name, def)

static int ReadOptionIntW(HWND hwnd, WORD id, const wchar_t* section,
                          const char* name_s, int def, int mask) {
  wchar_t name[64];
  str2tchar(name, name_s);
  int ret = GetPrivateProfileInt(section, name, def, inipath);
  CheckDlgButton(hwnd, id, (ret & mask) ? BST_CHECKED : BST_UNCHECKED);
  return ret;
}
#define ReadOptionInt(id, section, name, def, mask) \
  ReadOptionIntW(hwnd, id, section, name, def, mask)

/////////////////////////////////////////////////////////////////////////////
// Description:
//   Creates a tooltip for an item in a dialog box.
// Parameters:
//   idTool - identifier of an dialog box item.
//   nDlg - window handle of the dialog box.
//   pszText - string to use as the tooltip text.
// Returns:
//   The handle to the tooltip.
//
static HWND CreateInfoTip(HWND hDlg, int toolID, const wchar_t* const pszText) {
  if (!toolID || !hDlg || !pszText || !*pszText) return nullptr;

  // Get the window of the tool.
  HWND hwndTool = GetDlgItem(hDlg, toolID);

  // Create the tooltip. g_hInst is the global instance handle.
  // Windows are owned by the dialog box so we do not need
  // to explicitely  destroy them.
  HWND hwndTip =
      CreateWindowEx(0, TOOLTIPS_CLASS, nullptr, WS_POPUP | TTS_ALWAYSTIP,
                     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                     hDlg, nullptr, g_hinst, nullptr);

  if (!hwndTool || !hwndTip) return nullptr;

  // Associate the tooltip with the tool.
  TOOLINFO toolInfo = {0};
  toolInfo.cbSize = sizeof(toolInfo);
  toolInfo.hwnd = hDlg;
  toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  toolInfo.uId = reinterpret_cast<UINT_PTR>(hwndTool);
  toolInfo.lpszText = const_cast<wchar_t*>(pszText);

  wchar_t buf[16];
  if (GetClassName(hwndTool, buf, std::size(buf)) > 0 &&
      !lstrcmpi(L"Static", buf)) {
    // Use the RECT for STATIC controls
    toolInfo.uFlags = TTF_SUBCLASS;
    GetWindowRect(hwndTool, &toolInfo.rect);
    MapWindowPoints(nullptr, hDlg, reinterpret_cast<LPPOINT>(&toolInfo.rect),
                    2);
  }

  SendMessage(hwndTip, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&toolInfo));
  SendMessage(hwndTip, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELONG(32767, 0));
  RECT rc;
  GetClientRect(hDlg, &rc);
  SendMessage(hwndTip, TTM_SETMAXTIPWIDTH, 0, (rc.right - rc.left) * 3 / 4);

  return hwndTip;
}
struct dialogstring {
  short idc;
  short l10nidx; /* const wchar_t *const helpstr; */
};
using dialogstring_t = dialogstring;
static void UpdateDialogStrings(HWND hwnd, const dialogstring_t* const strlst,
                                size_t size) {
  for (size_t i = 0; i < size; i++) {
    SetDlgItemText(hwnd, strlst[i].idc, l10n->Get(strlst[i].l10nidx));
    CreateInfoTip(hwnd, strlst[i].idc, l10n->Get(strlst[i].l10nidx + 1));
  }
}
// Options to bead or written...
enum opttype { T_BOL = 0, T_BMK = 1, T_STR = 2 };
struct optlst {
  short idc;
  UCHAR type;
  UCHAR bitN;
  const wchar_t* section;
  const char* name;
  const void* def;
};
using optlst_t = optlst;
static void ReadDialogOptions(HWND hwnd, const optlst_t* ol, size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (ol[i].type == T_BOL)
      ReadOptionIntW(hwnd, ol[i].idc, ol[i].section, ol[i].name,
                     static_cast<int>(reinterpret_cast<DorQWORD>(ol[i].def)),
                     -1);
    else if (ol[i].type == T_BMK)
      ReadOptionIntW(hwnd, ol[i].idc, ol[i].section, ol[i].name,
                     static_cast<int>(reinterpret_cast<DorQWORD>(ol[i].def)),
                     1 << ol[i].bitN);
    else
      ReadOptionStrW(hwnd, ol[i].idc, ol[i].section, ol[i].name,
                     static_cast<const wchar_t*>(ol[i].def));
  }
}
static void WriteDialogOptions(HWND hwnd, const optlst_t* ol, size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (ol[i].type == T_BOL)
      WriteOptionBoolW(hwnd, ol[i].idc, ol[i].section, ol[i].name);
    else if (ol[i].type == T_BMK)
      WriteOptionBoolBW(hwnd, ol[i].idc, ol[i].section, ol[i].name, ol[i].bitN);
    else
      WriteOptionStrW(hwnd, ol[i].idc, ol[i].section, ol[i].name);
  }
}

/////////////////////////////////////////////////////////////////////////////
// to be used with EnumDesktopWindows()
static BOOL CALLBACK RefreshTestWin(HWND hwnd, LPARAM lp) {
  wchar_t classn[256];
  if (GetClassName(hwnd, classn, std::size(classn)) &&
      !lstrcmp(APP_NAME L"-Test", classn)) {
    PostMessage(hwnd, WM_UPDCFRACTION, 0, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOSIZE);
  }
  return TRUE;
}

static int FindIDCStrIDX(const dialogstring_t sl[], size_t len, int idc) {
  for (size_t i = 0; i < len; i++) {
    if (idc == static_cast<int>(sl[i].idc)) return sl[i].l10nidx;
  }
  return -1;
}
static void ShowContextHelp(const dialogstring_t sl[], size_t len, HWND hwnd,
                            LPHELPINFO hi) {
  if (hi->iContextType == HELPINFO_WINDOW) {
    int id = FindIDCStrIDX(sl, len, hi->iCtrlId);
    if (id >= 0) {
      SetDlgItemText(hwnd, IDC_HELPPANNEL, l10n->Get(id));
      // MessageBox(hwnd, l10n->Get(id), NULL, 0);
    }
  }
}
/////////////////////////////////////////////////////////////////////////////
static INT_PTR CALLBACK GeneralPageDialogProc(HWND hwnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam) {
#define V (void*)
  // Options to bead or written...
  static const optlst_t optlst[] = {
      // dialog id, type, bit number, section name, option name, def val.
      {IDC_AUTOFOCUS, T_BOL, 0, L"General", "AutoFocus", V nullptr},
      {IDC_AERO, T_BOL, 0, L"General", "Aero", V 1},
      {IDC_SMARTAERO, T_BMK, 0, L"General", "SmartAero", V 1},
      {IDC_SMARTERAERO, T_BMK, 1, L"General", "SmartAero", V nullptr},
      {IDC_STICKYRESIZE, T_BOL, 0, L"General", "StickyResize", V 1},
      {IDC_INACTIVESCROLL, T_BOL, 0, L"General", "InactiveScroll", V nullptr},
      {IDC_MDI, T_BOL, 0, L"General", "MDI", V 1},
      {IDC_RESIZEALL, T_BOL, 0, L"Advanced", "ResizeAll", V 1},
      {IDC_USEZONES, T_BMK, 0, L"Zones", "UseZones", V nullptr},
      {IDC_PIERCINGCLICK, T_BOL, 0, L"Advanced", "PiercingClick", V nullptr},
  };
#undef V

  static const dialogstring_t strlst[] = {
      {IDC_GENERAL_BOX, L10nId::GeneralBox},
      {IDC_AUTOFOCUS, L10nId::GeneralAutoFocus},
      {IDC_AERO, L10nId::GeneralAero},
      {IDC_SMARTAERO, L10nId::GeneralSmartAero},
      {IDC_SMARTERAERO, L10nId::GeneralSmarterAero},
      {IDC_STICKYRESIZE, L10nId::GeneralStickyResize},
      {IDC_INACTIVESCROLL, L10nId::GeneralInactiveScroll},
      {IDC_MDI, L10nId::GeneralMDI},
      {IDC_AUTOSNAP_HEADER, L10nId::GeneralAutoSnap},
      {IDC_LANGUAGE_HEADER, L10nId::GeneralLanguage},
      {IDC_USEZONES, L10nId::GeneralUseZones},
      {IDC_PIERCINGCLICK, L10nId::GeneralPiercingClick},
      {IDC_RESIZEALL, L10nId::GeneralResizeAll},
      {IDC_RESIZECENTER, L10nId::GeneralResizeCenter},
      {IDC_RZCENTER_NORM, L10nId::GeneralResizeCenterNorm},
      {IDC_RZCENTER_BR, L10nId::GeneralResizeCenterBr},
      {IDC_RZCENTER_MOVE, L10nId::GeneralResizeCenterMove},
      {IDC_RZCENTER_CLOSE, L10nId::GeneralResizeCenterClose},
      {IDC_AUTOSTART_BOX, L10nId::GeneralAutostartBox},
      {IDC_AUTOSTART, L10nId::GeneralAutostart},
      {IDC_AUTOSTART_HIDE, L10nId::GeneralAutostartHide},
      {IDC_AUTOSTART_ELEVATE, L10nId::GeneralAutostartElevate}};

  int updatestrings = 0;
  static int have_to_apply = 0;
  if (msg == WM_INITDIALOG) {
    MoveToCorner(g_cfgwnd);
    int ret = 0;
    ReadDialogOptions(hwnd, optlst, std::size(optlst));

    ret = GetPrivateProfileInt(L"General", L"ResizeCenter", 1, inipath);
    ret = ret == 1   ? IDC_RZCENTER_NORM
          : ret == 2 ? IDC_RZCENTER_MOVE
          : ret == 3 ? IDC_RZCENTER_CLOSE
                     : IDC_RZCENTER_BR;
    CheckRadioButton(hwnd, IDC_RZCENTER_NORM, IDC_RZCENTER_CLOSE, ret);

    HWND control = GetDlgItem(hwnd, IDC_LANGUAGE);
    CB_ResetContent(control);
    EnableWindow(control, TRUE);

    for (size_t i = 0; i < langinfo.size(); i++) {
      CB_AddString(control, langinfo[i].lang.c_str());
      if (!lstrcmpi(l10n->Code, langinfo[i].code.c_str())) {
        CB_SetCurSel(control, i);
      }
    }
    EnableDlgItem(hwnd, IDC_ELEVATE, !elevated);
    //    } else if (msg == WM_HELP) {
    //        ShowContextHelp(strlst, std::size(strlst), hwnd,
    //        (LPHELPINFO)lParam);
  } else if (msg == WM_COMMAND) {
    int id = LOWORD(wParam);
    int event = HIWORD(wParam);
    int val = IsDlgButtonChecked(hwnd, id);
    if (id == IDC_SMARTAERO) {
      EnableDlgItem(hwnd, IDC_SMARTERAERO, IsChecked(IDC_SMARTAERO));
    }

    if (id != IDC_ELEVATE && (event == 0 || event == CBN_SELCHANGE)) {
      PropSheet_Changed(g_cfgwnd, hwnd);
      have_to_apply = 1;
    }

    if (id == IDC_AUTOSTART) {
      EnableDlgItem(hwnd, IDC_AUTOSTART_HIDE, val);
      EnableDlgItem(hwnd, IDC_AUTOSTART_ELEVATE, val);
      if (!val) {
        CheckDlgButton(hwnd, IDC_AUTOSTART_HIDE, BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_AUTOSTART_ELEVATE, BST_UNCHECKED);
      }
    } else if (id == IDC_AUTOSTART_ELEVATE) {
      if (val && IsUACEnabled()) {
        MessageBox(nullptr, l10n->GeneralAutostartElevateTip, APP_NAME,
                   MB_ICONINFORMATION | MB_OK);
      }
    } else if (id == IDC_ELEVATE) {
      return ElevateNow(1);
    }
  } else if (msg == WM_NOTIFY) {
    auto pnmh = reinterpret_cast<LPNMHDR>(lParam);

    if (pnmh->code == PSN_SETACTIVE) {
      updatestrings = 1;

      // Autostart
      int autostart = 0, hidden = 0, eelevated = 0;
      CheckAutostart(&autostart, &hidden, &eelevated);
      CheckDlgButton(hwnd, IDC_AUTOSTART,
                     autostart ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_AUTOSTART_HIDE,
                     hidden ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_AUTOSTART_ELEVATE,
                     eelevated ? BST_CHECKED : BST_UNCHECKED);
      EnableDlgItem(hwnd, IDC_AUTOSTART_HIDE, autostart);
      EnableDlgItem(hwnd, IDC_AUTOSTART_ELEVATE, autostart);
      EnableDlgItem(hwnd, IDC_INACTIVESCROLL, IsChecked(IDC_INACTIVESCROLL));
    } else if (pnmh->code == PSN_APPLY && have_to_apply) {
      // all bool options (checkboxes).
      WriteDialogOptions(hwnd, optlst, std::size(optlst));

      wchar_t txt[kUintDigits + 1];
      int val = CB_GetCurSelId(IDC_AUTOSNAP);
      WritePrivateProfileString(L"General", L"AutoSnap", Uint2lStr(txt, val),
                                inipath);

      val = IsChecked(IDC_RZCENTER_NORM)    ? 1
            : IsChecked(IDC_RZCENTER_MOVE)  ? 2
            : IsChecked(IDC_RZCENTER_CLOSE) ? 3
                                            : 0;
      WritePrivateProfileString(L"General", L"ResizeCenter",
                                Uint2lStr(txt, val), inipath);

      // Load selected Language
      int i = CB_GetCurSelId(IDC_LANGUAGE);
      if (static_cast<size_t>(i) < langinfo.size() &&
          lstrcmpi(l10n->Code, langinfo[i].code.c_str())) {
        LoadTranslation(langinfo[i].fn.empty() ? nullptr
                                               : langinfo[i].fn.c_str());
#ifdef UNICODE
        wchar_t curlang[16];
        GetCUserLanguage_xx_XX(curlang);
        if (!lstrcmpi(l10n->Code, curlang))  // Use Auto if selected language is
                                             // the current user's one.
          WritePrivateProfileString(L"General", L"Language", L"Auto", inipath);
        else
          WritePrivateProfileString(L"General", L"Language", l10n->Code,
                                    inipath);
#else
        WritePrivateProfileString(L"General", L"Language", l10n->Code, inipath);
#endif
        updatestrings = 1;
        UpdateStrings();
      }

      // Autostart
      SetAutostart(IsChecked(IDC_AUTOSTART), IsChecked(IDC_AUTOSTART_HIDE),
                   IsChecked(IDC_AUTOSTART_ELEVATE));

      UpdateSettings();
      // Update Test windows in if open.
      EnumThreadWindows(GetCurrentThreadId(), RefreshTestWin, 0);

      have_to_apply = 0;
    }
  }
  if (updatestrings) {
    // Update text
    UpdateDialogStrings(hwnd, strlst, std::size(strlst));
    // spetial case...
    // CreateToolTip(IDC_AUTOFOCUS, hwnd, L"String\nExample");
    SetDlgItemText(hwnd, IDC_ELEVATE,
                   elevated ? l10n->GeneralElevated : l10n->GeneralElevate);

    // AutoSnap
    HWND control = GetDlgItem(hwnd, IDC_AUTOSNAP);
    CB_ResetContent(control);
    CB_AddString(control, l10n->GeneralAutoSnap0);
    CB_AddString(control, l10n->GeneralAutoSnap1);
    CB_AddString(control, l10n->GeneralAutoSnap2);
    CB_AddString(control, l10n->GeneralAutoSnap3);
    wchar_t txt[8];
    GetPrivateProfileString(L"General", L"AutoSnap", L"0", txt, std::size(txt),
                            inipath);
    CB_SetCurSel(control, strtoi(txt));

    // Language
    control = GetDlgItem(hwnd, IDC_LANGUAGE);
    CB_DeleteString(control, langinfo.size());
  }
  return FALSE;
}
/////////////////////////////////////////////////////////////////////////////
[[nodiscard]] static int IsKeyInList(wchar_t* keys, unsigned vkey) {
  unsigned temp = 0, numread = 0;
  wchar_t* pos = keys;
  while (*pos != '\0') {
    numread = 0;
    temp = lstrhex2u(pos);
    while (pos[numread] && pos[numread] != ' ') numread++;
    while (pos[numread] == ' ') numread++;
    if (temp == vkey) {
      return 1;
    }
    pos += numread;
  }
  return 0;
}
static void AddvKeytoList(wchar_t keys[32], unsigned vkey) {
  // Check if it is already in the list.
  if (IsKeyInList(keys, vkey)) return;
  // Add a key to the hotkeys list
  if (*keys != '\0') {
    lstrcat_s(keys, 32, L" ");
  }
  wchar_t buf[kPtrHexDigits + 1];
  lstrcat_s(keys, 32, LPTR2Hex(buf, vkey));
}
static void RemoveKeyFromList(wchar_t keys[32], unsigned vkey) {
  // Remove the key from the hotclick list
  unsigned temp = 0, numread = 0;
  wchar_t* pos = keys;
  while (*pos != '\0') {
    numread = 0;
    temp = lstrhex2u(pos);
    while (pos[numread] && pos[numread] != ' ') numread++;
    while (pos[numread] == ' ') numread++;
    if (temp == vkey) {
      keys[pos - keys] = '\0';
      lstrcat_s(keys, 32, pos + numread);
      break;
    }
    pos += numread;
  }
  // Strip eventual remaining spaces
  unsigned ll = lstrlen(keys);
  while (ll > 0 && keys[--ll] == ' ') keys[ll] = '\0';
}
/////////////////////////////////////////////////////////////////////////////
struct hk_struct {
  unsigned control;
  unsigned vkey;
};
static void SaveHotKeys(const struct hk_struct* const hotkeys, HWND hwnd,
                        const wchar_t* const name) {
  wchar_t keys[32];
  // Get the current config in case there are some user added keys.
  GetPrivateProfileString(L"Input", name, L"", keys, std::size(keys), inipath);
  for (size_t i = 0; hotkeys[i].control; i++) {
    if (IsChecked(hotkeys[i].control)) {
      AddvKeytoList(keys, hotkeys[i].vkey);
    } else {
      RemoveKeyFromList(keys, hotkeys[i].vkey);
    }
  }
  WritePrivateProfileString(L"Input", name, keys, inipath);
}
/////////////////////////////////////////////////////////////////////////////
static void CheckConfigHotKeys(const struct hk_struct* hotkeys, HWND hwnd,
                               const wchar_t* hotkeystr, const wchar_t* def) {
  // Hotkeys
  unsigned temp = 0;
  wchar_t txt[32];
  GetPrivateProfileString(L"Input", hotkeystr, def, txt, std::size(txt),
                          inipath);
  wchar_t* pos = txt;
  while (*pos != '\0') {
    temp = lstrhex2u(pos);
    while (*pos && *pos != ' ') pos++;
    while (*pos == ' ') pos++;

    // What key was that?
    for (size_t i = 0; hotkeys[i].control; i++) {
      if (temp == hotkeys[i].vkey) {
        CheckDlgButton(hwnd, hotkeys[i].control, BST_CHECKED);
        break;
      }
    }
  }
}

struct tagActiondl {
  const wchar_t* action;
  short l10nidx;
  BYTE param1_type;
  BYTE param2_type;
};
using actiondl_t = tagActiondl;
static void FillActionDropListS(HWND hwnd, int idc, const wchar_t* inioption,
                                const actiondl_t* actions) {
  HWND control = GetDlgItem(hwnd, idc);
  wchar_t txt[64];
  int sel = 0, j = 0;
  CB_ResetContent(control);
  if (inioption) {
    sel = -1;  // Selection to be made
    GetPrivateProfileString(L"Input", inioption, L"Nothing", txt,
                            std::size(txt), inipath);
  }
  for (j = 0; actions[j].action; j++) {
    wchar_t action_name[256];
    lstrcpy_noaccel(action_name, std::size(action_name),
                    l10n->Get(actions[j].l10nidx));
    CB_AddString(control, action_name);
    if (inioption && !lstrcmpi(txt, actions[j].action)) {
      sel = j;
    }
  }
  if (sel < 0) {
    // sel is negative if the string was not found in the actiondl_t:
    // UNKNOWN ACTION, so we add it manually at the end of the list
    CB_AddString(control, txt);
    sel = j;  // And select this unknown action.
  }
  CB_SetCurSel(control, sel);
}

static int GetActionStringFromDropList(HWND hwnd, int idc,
                                       const actiondl_t* actions, wchar_t* obuf,
                                       size_t obuflen) {
  HWND control = GetDlgItem(hwnd, idc);
  int j = SendMessage(control, CB_GETCURSEL, 0, 0);
  if (j >= 0 && actions[j].action) {              // Inside of known values
    lstrcpy_s(obuf, obuflen, actions[j].action);  // DONE!
    return j;
  }

  // User directly Wrote the specified string?
  int len = static_cast<int>(static_cast<DWORD>(SendMessage(
      control, WM_GETTEXT, obuflen, reinterpret_cast<LPARAM>(obuf))));
  if (0 < len && *obuf) {
    // Action was direcly written!
    j = SendMessage(control, CB_FINDSTRINGEXACT, /*start index=*/-1,
                    reinterpret_cast<LPARAM>(obuf));
    if (j >= 0 && actions[j].action)                // Found index.
      lstrcpy_s(obuf, obuflen, actions[j].action);  // DONE!

    return j;
  }
  return -1;
}
static void WriteActionDropListS(HWND hwnd, int idc, const wchar_t* inioption,
                                 const actiondl_t* actions) {
  wchar_t txt[128];
  txt[0] = L'\0';
  GetActionStringFromDropList(hwnd, idc, actions, txt, std::size(txt));
  WritePrivateProfileString(L"Input", inioption, txt, inipath);
}
/////////////////////////////////////////////////////////////////////////////
static INT_PTR CALLBACK MousePageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                            LPARAM lParam) {
  static int have_to_apply = 0;
  // Mouse actions
  static const struct {
    int control;               // Same control
    const wchar_t* option[5];  // Prim/alt/TTB/WM/WR
  } mouse_buttons[] = {
      {IDC_LMB, {L"LMB", L"LMBB", L"LMBT", L"LMBM", L"LMBR"}},
      {IDC_MMB, {L"MMB", L"MMBB", L"MMBT", L"MMBM", L"MMBR"}},
      {IDC_RMB, {L"RMB", L"RMBB", L"RMBT", L"RMBM", L"RMBR"}},
      {IDC_MB4, {L"MB4", L"MB4B", L"MB4T", L"MB4M", L"MB4R"}},
      {IDC_MB5, {L"MB5", L"MB5B", L"MB5T", L"MB5M", L"MB5M"}},
  };
  static const struct {
    int control;               // Same control
    const wchar_t* option[5];  // Prim/alt/TTB/WM/WR
  } mouse_buttonsUP[] = {
      {IDC_MOVEUP, {L"MoveUp", L"MoveUpB", L"MoveUpT", L"MoveUp", L"MoveUp"}},
      {IDC_RESIZEUP,
       {L"ResizeUp", L"ResizeUpB", L"ResizeUpT", L"ResizeUp", L"ResizeUp"}},
  };

#define COMMON_ACTIONS                                                      \
  {L"Close", L10nId::InputActionClose}, {L"Kill", L10nId::InputActionKill}, \
      {L"Minimize", L10nId::InputActionMinimize},                           \
      {L"Maximize", L10nId::InputActionMaximize},                           \
      {L"Lower", L10nId::InputActionLower},                                 \
      {L"Focus", L10nId::InputActionFocus},                                 \
      {L"NStacked", L10nId::InputActionNStacked},                           \
      {L"PStacked", L10nId::InputActionPStacked},                           \
      {L"StackList", L10nId::InputActionStackList},                         \
      {L"StackList2", L10nId::InputActionStackList2},                       \
      {L"AltTabList", L10nId::InputActionAltTabList},                       \
      {L"AltTabFullList", L10nId::InputActionAltTabFullList},               \
      {L"Roll", L10nId::InputActionRoll},                                   \
      {L"AlwaysOnTop", L10nId::InputActionAlwaysOnTop},                     \
      {L"Borderless", L10nId::InputActionBorderless},                       \
      {L"Center", L10nId::InputActionCenter},                               \
      {L"Center2", L10nId::InputActionCenter2},                             \
      {L"MaximizeHV", L10nId::InputActionMaximizeHV},                       \
      {L"SideSnap", L10nId::InputActionSideSnap},                           \
      {L"ExtendSnap", L10nId::InputActionExtendSnap},                       \
      {L"ExtendTNEdge", L10nId::InputActionExtendTNEdge},                   \
      {L"MoveTNEdge", L10nId::InputActionMoveTNEdge},                       \
      {L"NLayout", L10nId::InputActionNLayout},                             \
      {L"PLayout", L10nId::InputActionPLayout},                             \
      {L"MinAllOther", L10nId::InputActionMinAllOther},                     \
      {L"Mute", L10nId::InputActionMute}, {L"Menu", L10nId::InputActionMenu},

  static const actiondl_t mouse_actions[] = {
      // Specific to the Primary/Alternate/Titlebar
      // And not available for the MoveUp/ResizeUp
      {L"Move", L10nId::InputActionMove},
      {L"Resize", L10nId::InputActionResize},
      {L"Restore", L10nId::InputActionRestore},
      // Common mouse actions
      COMMON_ACTIONS

      {L"Nothing", L10nId::InputActionNothing},
      {nullptr, 0}};

  // Actions on MoveUp/ResizeUP
  const actiondl_t* mouse_actionsUP = &mouse_actions[3];

  // While moving/resizing
  const actiondl_t* mouse_actionsWMR = &mouse_actions[3];

  //    static const actiondl_t mouse_actionsWMR[] = {
  //        // Spetial actions first
  //        {L"ZoneSnapTogg", L10nId::input_actions_zonesnaptog },
  //        {L"SnapTogg",     L10nId::input_actions_snaptogg },
  //        // Then the common mouse actions
  //        COMMON_ACTIONS
  //
  //        {L"Nothing",     L10nId::input_actions_nothing },
  //        {NULL, 0}
  //    };

  // Scroll
  static const struct {
    int control;               // Same control
    const wchar_t* option[5];  // Prim/alt/TTB/WM/WR
  } mouse_wheels[] = {
      {IDC_SCROLL, {L"Scroll", L"ScrollB", L"ScrollT", L"ScrollM", L"ScrollR"}},
      {IDC_HSCROLL,
       {L"HScroll", L"HScrollB", L"HScrollT", L"HScrollM", L"HScrollR"}}};

  static const actiondl_t scroll_actions[] = {
      {L"AltTab", L10nId::InputActionAltTab},
      {L"Volume", L10nId::InputActionVolume},
      {L"Transparency", L10nId::InputActionTransparency},
      {L"Zoom", L10nId::InputActionZoom},
      {L"Zoom2", L10nId::InputActionZoom2},
      {L"Lower", L10nId::InputActionLower},
      {L"Roll", L10nId::InputActionRoll},
      {L"Maximize", L10nId::InputActionMaximize},
      {L"NPStacked", L10nId::InputActionNStacked},
      {L"NPStacked2", L10nId::InputActionNStacked2},
      {L"HScroll", L10nId::InputActionHScroll},
      {L"NPLayout", L10nId::InputActionNPLayout},
      {L"Nothing", L10nId::InputActionNothing},
      {nullptr, 0}};

  // Hotkeys
  static const struct hk_struct hotclicks[] = {
      {IDC_MMB_HC, 0x04}, {IDC_MB4_HC, 0x05}, {IDC_MB5_HC, 0X06}, {0, 0}};

  static const optlst_t optlst[] = {
      {IDC_TTBACTIONSNA, T_BMK, 0, L"Input", "TTBActions", nullptr},
      {IDC_TTBACTIONSWA, T_BMK, 1, L"Input", "TTBActions", nullptr},
      {IDC_LONGCLICKMOVE, T_BOL, 0, L"Input", "LongClickMove", nullptr}};

  auto pnmh = reinterpret_cast<LPNMHDR>(lParam);

  // Body shared by several message paths (was a goto target).
  auto fill_actions = [&]() {
    unsigned i = 0;
    // Mouse actions
    int optoff = IsChecked(IDC_MBA2)     ? 1
                 : IsChecked(IDC_INTTB)  ? 2
                 : IsChecked(IDC_WHILEM) ? 3
                 : IsChecked(IDC_WHILER) ? 4
                                         : 0;

    // We must disable MoveUp and ResizeUp for the
    // While Moving/While resizing opts.
    EnableDlgItem(hwnd, IDC_MOVEUP, optoff < 3);
    EnableDlgItem(hwnd, IDC_RESIZEUP, optoff < 3);
    for (i = 0; i < std::size(mouse_buttons); i++) {
      FillActionDropListS(hwnd, mouse_buttons[i].control,
                          mouse_buttons[i].option[optoff],
                          optoff < 3 ? mouse_actions : mouse_actionsWMR);
    }
    if (optoff < 3) {
      for (i = 0; i < std::size(mouse_buttonsUP); i++) {
        FillActionDropListS(hwnd, mouse_buttonsUP[i].control,
                            mouse_buttonsUP[i].option[optoff], mouse_actionsUP);
      }
    }
    // Scroll actions, always the same.
    for (i = 0; i < std::size(mouse_wheels); i++) {
      FillActionDropListS(hwnd, mouse_wheels[i].control,
                          mouse_wheels[i].option[optoff], scroll_actions);
    }

    // Update text
    static const dialogstring_t strlst[] = {
        {IDC_MOUSE_BOX, L10nId::InputMouseBox},
        {IDC_MBA1, L10nId::InputMouseBtAc1},
        {IDC_MBA2, L10nId::InputMouseBtAc2},
        {IDC_INTTB, L10nId::InputMouseINTTB},
        {IDC_WHILEM, L10nId::InputMouseWhileM},
        {IDC_WHILER, L10nId::InputMouseWhileR},

        {IDC_LMB_HEADER, L10nId::InputMouseLMB},
        {IDC_MMB_HEADER, L10nId::InputMouseMMB},
        {IDC_RMB_HEADER, L10nId::InputMouseRMB},
        {IDC_MB4_HEADER, L10nId::InputMouseMB4},
        {IDC_MB5_HEADER, L10nId::InputMouseMB5},
        {IDC_SCROLL_HEADER, L10nId::InputMouseScroll},
        {IDC_HSCROLL_HEADER, L10nId::InputMouseHScroll},
        {IDC_MOVEUP_HEADER, L10nId::InputMouseMoveUp},
        {IDC_RESIZEUP_HEADER, L10nId::InputMouseResizeUp},
        {IDC_TTBACTIONS_BOX, L10nId::InputMouseTTBActionBox},
        {IDC_TTBACTIONSNA, L10nId::InputMouseTTBActionNA},
        {IDC_TTBACTIONSWA, L10nId::InputMouseTTBActionWA},

        {IDC_HOTCLICKS_BOX, L10nId::InputHotclicksBox},
        {IDC_HOTCLICKS_MORE, L10nId::InputHotclicksMore},
        {IDC_MMB_HC, L10nId::InputMouseMMBHC},
        {IDC_MB4_HC, L10nId::InputMouseMB4HC},
        {IDC_MB5_HC, L10nId::InputMouseMB5HC},
        {IDC_LONGCLICKMOVE, L10nId::InputMouseLongClickMove},
    };
    UpdateDialogStrings(hwnd, strlst, std::size(strlst));
  };

  if (msg == WM_INITDIALOG) {
    // Hotclicks buttons
    ReadDialogOptions(hwnd, optlst, std::size(optlst));
    CheckRadioButton(hwnd, IDC_MBA1, IDC_WHILER,
                     IDC_MBA1);  // Check the primary action
    CheckConfigHotKeys(hotclicks, hwnd, L"Hotclicks", L"");
  } else if (msg == WM_COMMAND) {
    int event = HIWORD(wParam);
    int id = LOWORD(wParam);
    if (id >= IDC_MBA1 && id <= IDC_WHILER) {
      // CheckRadioButton(hwnd, IDC_MBA1, IDC_WHILER, id); // Check the selected
      // action
      fill_actions();
      return FALSE;
    }

    if (event == 0 || event == CBN_SELCHANGE || event == CBN_EDITCHANGE) {
      PropSheet_Changed(g_cfgwnd, hwnd);
      have_to_apply = 1;
    }
  } else if (msg == WM_NOTIFY) {
    if (pnmh->code == PSN_SETACTIVE) {
      wchar_t txt[8];
      GetPrivateProfileString(L"Input", L"ModKey", L"", txt, std::size(txt),
                              inipath);
      EnableDlgItem(hwnd, IDC_MBA2, txt[0]);
      // Disable inside ttb
      EnableDlgItem(hwnd, IDC_INTTB,
                    IsChecked(IDC_TTBACTIONSNA) || IsChecked(IDC_TTBACTIONSWA));

      fill_actions();

    } else if (pnmh->code == PSN_APPLY && have_to_apply) {
      // Mouse actions, for all mouse buttons...
      unsigned i = 0;
      // Add 2 if in titlear add one for secondary action.
      // Mouse actions
      int optoff = IsChecked(IDC_MBA2)     ? 1
                   : IsChecked(IDC_INTTB)  ? 2
                   : IsChecked(IDC_WHILEM) ? 3
                   : IsChecked(IDC_WHILER) ? 4
                                           : 0;
      for (i = 0; i < std::size(mouse_buttons); i++) {
        WriteActionDropListS(hwnd, mouse_buttons[i].control,
                             mouse_buttons[i].option[optoff],
                             optoff < 3 ? mouse_actions : mouse_actionsWMR);
      }
      if (optoff < 3) {
        for (i = 0; i < std::size(mouse_buttonsUP); i++) {
          WriteActionDropListS(hwnd, mouse_buttonsUP[i].control,
                               mouse_buttonsUP[i].option[optoff],
                               mouse_actionsUP);
        }
      }
      // Scroll
      for (i = 0; i < std::size(mouse_wheels); i++) {
        WriteActionDropListS(hwnd, mouse_wheels[i].control,
                             mouse_wheels[i].option[optoff], scroll_actions);
      }

      // Checkboxes...
      WriteDialogOptions(hwnd, optlst, std::size(optlst));
      // Hotclicks
      SaveHotKeys(hotclicks, hwnd, L"Hotclicks");
      UpdateSettings();
      have_to_apply = 0;
    }
  }
  return FALSE;
}

static BOOL CALLBACK EnableAllControlsProc(HWND hwnd, LPARAM lp) {
  if (IsWindowVisible(hwnd)) {
    EnableWindow(hwnd, lp);
  }
  return TRUE;
}
static LRESULT WINAPI PickShortcutWinProc(HWND hwnd, UINT msg, WPARAM wp,
                                          LPARAM lp) {
  static const UCHAR vkbl[] = {VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_SHIFT,
                               VK_LSHIFT,  VK_RSHIFT,   VK_MENU,     VK_LMENU,
                               VK_RMENU,   VK_LWIN,     VK_RWIN,     '\0'};
  //    static int was_enabled;

  switch (msg) {
      //    case WM_CREATE: {
      //        was_enabled = IsEnabled();
      //        if (was_enabled) UnhookSystem();
      //    } break;
    case WM_CHAR:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_SYSDEADCHAR:
    case WM_SYSCHAR:
      break;
    case WM_KEYUP: {
      if (!IsHotkeyy(static_cast<UCHAR>(wp), vkbl)) {
        wchar_t txt[kPtrHexDigits + 1];
        HWND phwnd = GetParent(hwnd);
        SetDlgItemText(phwnd, IDC_SHORTCUTS_VK, LPTR2Hex(txt, wp));

        CheckDlgButton(
            phwnd, IDC_SHIFT,
            GetKeyState(VK_SHIFT) & 0x8000 ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(
            phwnd, IDC_CONTROL,
            GetKeyState(VK_CONTROL) & 0x8000 ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(
            phwnd, IDC_WINKEY,
            GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000
                ? BST_CHECKED
                : BST_UNCHECKED);
        CheckDlgButton(
            phwnd, IDC_ALT,
            GetKeyState(VK_MENU) & 0x8000 ? BST_CHECKED : BST_UNCHECKED);
        DestroyWindow(hwnd);
      }
    } break;
    case WM_LBUTTONDOWN:
    case WM_KILLFOCUS: {
      DestroyWindow(hwnd);
    } break;

    case WM_DESTROY: {
      HWND phwnd = GetParent(hwnd);
      EnableDlgItem(phwnd, IDC_SHORTCUTS_PICK, 1);
      EnumChildWindows(phwnd, EnableAllControlsProc, 1);
      //        NMHDR lpn;
      //        lpn.hwndFrom = NULL; lpn.code = PSN_SETACTIVE;
      //        SendMessage(phwnd, WM_NOTIFY, 0, (LPARAM)&lpn);
      HWND sethwnd = GetDlgItem(phwnd, IDC_SHORTCUTS_SET);
      EnableWindow(sethwnd, TRUE);
      SetFocus(sethwnd);
      //        if (was_enabled) HookSystem();
    } break;
    default:
      return DefWindowProc(hwnd, msg, wp, lp);
  }
  return 0;
}

// Dialog procedure for the Advanced Action construction from base action
struct tagAdvancedActionParam {
  const actiondl_t* base_action_lst;
  wchar_t outbuf[64];
};
using advancedActionParam_t = tagAdvancedActionParam;

enum {
  ACPARAM_NONE = 0,
  ACPARAM_DIRECTION,
  ACPARAM_NUMBER,
  ACPARAM_UPDOWN,
  ACPARAM_UPDOWNSETTOGGLE,
  ACPARAM_LAST,
};
static const char* action_fl_param1_maps[ACPARAM_LAST] = {
    /* ACPARAM_NONE            */ nullptr,  // Nothing...
    /* ACPARAM_DIRECTION       */ "0LURD",  // 0 Left, Up, Right, Down
    /* ACPARAM_NUMBER          */ nullptr,  // Nothing
    /* ACPARAM_UPDOWN          */ "0UD",    // 0 Up, Down
    /* ACPARAM_UPDOWNSETTOGGLE */ "0UDST",  // 0 Up, Down, Set to, Toggle
};
static const short action_fl_strings_direction[] = {
    0, L10nId::WayLeft, L10nId::WayUp, L10nId::WayRight, L10nId::WayDown,
};
static const short action_fl_strings_updown_set_toggle[] = {
    0, L10nId::WayUp, L10nId::WayDown, L10nId::KwdSetTo, L10nId::KwdToggle,
};
static const struct {
  size_t num;
  const short* l10nIdx;
} action_fl_l10n_maps[ACPARAM_LAST] = {
    /* ACPARAM_NONE           */ {0, nullptr},
    /* ACPARAM_DIRECTION      */
    {std::size(action_fl_strings_direction), action_fl_strings_direction},
    /* ACPARAM_NUMBER         */ {0, nullptr},
    /* ACPARAM_UPDOWN         */ {3, action_fl_strings_updown_set_toggle},
    /* ACPARAM_UPDOWNSETTOGGLE*/
    {std::size(action_fl_strings_updown_set_toggle),
     action_fl_strings_updown_set_toggle},
};

static INT_PTR CALLBACK AdvancedActionDlgProc(HWND hwnd, UINT msg, WPARAM wp,
                                              LPARAM lp) {
  switch (msg) {
    case WM_INITDIALOG: {
      auto* acp = reinterpret_cast<advancedActionParam_t*>(lp);
      SetWindowLongPtr(hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(lp));

      FillActionDropListS(hwnd, IDC_ACTIONP0, nullptr, acp->base_action_lst);
      SendDlgItemMessage(hwnd, IDC_ACTIONP0, CB_SETCURSEL,
                         static_cast<WPARAM>(-1), 0);

      SetWindowText(hwnd, l10n->TtlAdvancedAction);
      SetDlgItemText(hwnd, IDC_ACTIONT, l10n->TtlAction);

      // Copy localized OK, CANCEL button from parent
      HWND parent = GetAncestor(hwnd, GA_ROOTOWNER);
      if (parent) {
        wchar_t txt[32];
        if (GetDlgItemText(parent, IDOK, txt, std::size(txt)))
          SetDlgItemText(hwnd, IDOK, txt);
        if (GetDlgItemText(parent, IDCANCEL, txt, std::size(txt)))
          SetDlgItemText(hwnd, IDCANCEL, txt);
      }

      return TRUE; /* Autofocus first element in dialog */
    } break;

    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (id == IDOK) {
        auto* acp = reinterpret_cast<advancedActionParam_t*>(
            GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (!acp) return 0;
        ZeroObject(acp->outbuf);
        // Get the text from the action string display to return
        GetDlgItemText(hwnd, IDC_ACTIONTSTRING, acp->outbuf,
                       std::size(acp->outbuf) - 1);
        EndDialog(hwnd, IDOK);
        return TRUE;
      }
      if (id == IDCANCEL) {
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
      } else {
        int ev = HIWORD(wp);
        int update_action_string =
            (ev == CBN_SELCHANGE || ev == CBN_EDITCHANGE || ev == EN_UPDATE) &&
            (id == IDC_ACTIONP0 || id == IDC_ACTIONP1 || IDC_ACTIONP2);

        if (update_action_string) {
          auto* acp = reinterpret_cast<advancedActionParam_t*>(
              GetWindowLongPtr(hwnd, GWLP_USERDATA));
          wchar_t acstr[128], param1[16], param2[32];
          acstr[0] = L'\0';

          // ACTION PARAM0: base action
          int p0idx = GetActionStringFromDropList(hwnd, IDC_ACTIONP0,
                                                  acp->base_action_lst, acstr,
                                                  std::size(acstr) - 8);
          BYTE param1_type = acp->base_action_lst[p0idx].param1_type;
          BYTE param2_type = acp->base_action_lst[p0idx].param2_type;

          if (id == IDC_ACTIONP0 /*|| id == IDC_ACTIONP1*/) {
            // Adjust visibility/settings of param 1 and param 2.
            EnableDlgItem(hwnd, IDC_ACTIONP1, p0idx >= 0 && param1_type != 0);
            EnableDlgItem(hwnd, IDC_ACTIONP2, p0idx >= 0 && param2_type != 0);
            SetDlgItemText(hwnd, IDC_ACTIONP2,
                           L"");  // Clear number content

            // Fill Drop List according to PARAM0 of the action.
            HWND ctrl = GetDlgItem(hwnd, IDC_ACTIONP1);
            CB_ResetContent(ctrl);
            for (size_t i = 0; i < action_fl_l10n_maps[param1_type].num; i++) {
              short l10nIdx = action_fl_l10n_maps[param1_type].l10nIdx[i];
              const wchar_t* str = l10nIdx == 0 ? L"---" : l10n->Get(l10nIdx);
              CB_AddString(ctrl, str);
            }
          }

          // ACTION PARAM1: direction flags
          param1[0] = L'0';
          param1[1] = L'\0';
          if (action_fl_param1_maps[param1_type]) {
            // This action type can have parameter1.
            int direction =
                SendDlgItemMessage(hwnd, IDC_ACTIONP1, CB_GETCURSEL, 0, 0);
            if (direction < 0) {
              GetDlgItemText(hwnd, IDC_ACTIONP1, param1, std::size(param1));
            } else if (direction <
                       lstrlenA(action_fl_param1_maps[param1_type])) {
              // Put the correct character in the action string.
              // We use a single character for each kind of param
              param1[0] = action_fl_param1_maps[param1_type][direction];
              param1[1] = L'\0';
            }
          }
          // ACTION PARAM2 number...
          int param2len =
              GetDlgItemText(hwnd, IDC_ACTIONP2, param2, std::size(param2));

          // Add PARAM1 if needed
          if ((*param1 && *param1 != L'0') || (param2len && *param2)) {
            lstrcat_s(acstr, std::size(acstr), L"_");
            lstrcat_s(acstr, std::size(acstr), param1);
            // Add PARAM2 if needed
            if (param2len && *param2) {
              lstrcat_s(acstr, std::size(acstr), L"_");
              lstrcat_s(acstr, std::size(acstr), param2);
            }
          }

          // show the whole string
          SetDlgItemText(hwnd, IDC_ACTIONTSTRING, acstr);
          return TRUE;
        }
      }
    } break;

    case WM_SYSCOMMAND: {
      if (wp == SC_CLOSE) {
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
      }
    } break;

  } /* end switch(msg) */

  return FALSE; /* non processed */
}

/////////////////////////////////////////////////////////////////////////////
static INT_PTR CALLBACK KeyboardPageDialogProc(HWND hwnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam) {
  static int have_to_apply = 0;
  static int edit_shortcut_idx = 0;
  // Hotkeys
  static const struct hk_struct hotkeys[] = {{IDC_LEFTALT, VK_LMENU},
                                             {IDC_RIGHTALT, VK_RMENU},
                                             {IDC_LEFTWINKEY, VK_LWIN},
                                             {IDC_RIGHTWINKEY, VK_RWIN},
                                             {IDC_LEFTCTRL, VK_LCONTROL},
                                             {IDC_RIGHTCTRL, VK_RCONTROL},
                                             {0, 0}};
  static const actiondl_t kb_actions[] = {
      {L"Move", L10nId::InputActionMove},
      {L"Resize", L10nId::InputActionResize},
      {L"Close", L10nId::InputActionClose},
      {L"Minimize", L10nId::InputActionMinimize},
      {L"Maximize", L10nId::InputActionMaximize},
      {L"Lower", L10nId::InputActionLower},
      {L"Roll", L10nId::InputActionRoll},
      {L"AlwaysOnTop", L10nId::InputActionAlwaysOnTop},
      {L"Borderless", L10nId::InputActionBorderless},
      {L"Center", L10nId::InputActionCenter},
      {L"Center2", L10nId::InputActionCenter2},
      {L"MaximizeHV", L10nId::InputActionMaximizeHV},
      {L"MinAllOther", L10nId::InputActionMinAllOther},
      {L"Menu", L10nId::InputActionMenu},
      {L"Nothing", L10nId::InputActionNothing},
      {nullptr, 0}};
  static const actiondl_t kbshortcut_actions[] = {
      {L"Kill", L10nId::InputActionKill},
      {L"Pause", L10nId::InputActionPause},
      {L"Resume", L10nId::InputActionResume},
      {L"ASOnOff", L10nId::InputActionASOnOff},
      {L"Close", L10nId::InputActionClose},
      {L"Minimize", L10nId::InputActionMinimize},
      {L"Maximize", L10nId::InputActionMaximize},
      {L"Lower", L10nId::InputActionLower},
      {L"Roll", L10nId::InputActionRoll, ACPARAM_UPDOWN},
      {L"Transparency", L10nId::InputActionTransparency,
       ACPARAM_UPDOWNSETTOGGLE, ACPARAM_NUMBER},
      {L"AlwaysOnTop", L10nId::InputActionAlwaysOnTop},
      {L"Borderless", L10nId::InputActionBorderless},
      {L"Center", L10nId::InputActionCenter},
      {L"Center2", L10nId::InputActionCenter2},
      {L"Mute", L10nId::InputActionMute},
      {L"Menu", L10nId::InputActionMenu},

      {L"NStacked", L10nId::InputActionNStacked},
      {L"NStacked2", L10nId::InputActionNStacked2},
      {L"PStacked", L10nId::InputActionPStacked},
      {L"PStacked2", L10nId::InputActionPStacked2},
      {L"StackList", L10nId::InputActionStackList},
      {L"StackList2", L10nId::InputActionStackList2},
      {L"AltTabList", L10nId::InputActionAltTabList},
      {L"AltTabFullList", L10nId::InputActionAltTabFullList},

      {L"ExtendTNEdge", L10nId::InputActionExtendTNEdge, ACPARAM_DIRECTION, 0},
      {L"MoveTNEdge", L10nId::InputActionMoveTNEdge, ACPARAM_DIRECTION, 0},

      {L"MZone", L10nId::InputActionMZone, ACPARAM_DIRECTION, 0},
      {L"XZone", L10nId::InputActionXZone, ACPARAM_DIRECTION, 0},

      {L"Step", L10nId::InputActionStep, ACPARAM_DIRECTION, ACPARAM_NUMBER},

      {L"Focus", L10nId::InputActionFocus, ACPARAM_DIRECTION, 0},

      {L"NLayout", L10nId::InputActionNLayout},
      {L"PLayout", L10nId::InputActionPLayout},

      {nullptr, 0}};

  // Hotkeys
  static const actiondl_t togglekeys[] = {
      {L"", L10nId::InputActionNothing},
      {L"A4 A5", L10nId::InputHotkeysAlt},
      {L"5B 5C", L10nId::InputHotkeysWinkey},
      {L"A2 A3", L10nId::InputHotkeysCtrl},
      {L"A0 A1", L10nId::InputHotkeysShift},
      {L"A4", L10nId::InputHotkeysLeftAlt},
      {L"A5", L10nId::InputHotkeysRightAlt},
      {L"5B", L10nId::InputHotkeysLeftWinkey},
      {L"5C", L10nId::InputHotkeysRightWinkey},
      {L"A2", L10nId::InputHotkeysLeftCtrl},
      {L"A3", L10nId::InputHotkeysRightCtrl},
      {L"A0", L10nId::InputHotkeysLeftShift},
      {L"A1", L10nId::InputHotkeysRightShift},
      {nullptr, 0},
  };
  static const optlst_t optlst[] = {
      {IDC_SCROLLLOCKSTATE, T_BMK, 0, L"Input", "ScrollLockState", nullptr},
      {IDC_UNIKEYHOLDMENU, T_BOL, 0, L"Input", "UniKeyHoldMenu", nullptr},
      {IDC_KEYCOMBO, T_BOL, 0, L"Input", "KeyCombo", nullptr},
      {IDC_USEPTWINDOW, T_BOL, 0, L"KBShortcuts", "UsePtWindow", nullptr},
  };

  auto pnmh = reinterpret_cast<LPNMHDR>(lParam);

  // Body shared by several message paths (was a goto target).
  auto apply_all = [&]() {
    //            int i;
    // Action without click
    WriteActionDropListS(hwnd, IDC_GRABWITHALT, L"GrabWithAlt", kb_actions);
    WriteActionDropListS(hwnd, IDC_GRABWITHALTB, L"GrabWithAltB", kb_actions);

    WriteDialogOptions(hwnd, optlst, std::size(optlst));
    ScrollLockState =
        WriteOptionBoolB(IDC_SCROLLLOCKSTATE, L"Input", "ScrollLockState", 0);
    // Modifier key (similar to action list)
    WriteActionDropListS(hwnd, IDC_MODKEY, L"ModKey", togglekeys);
    // Hotkeys
    SaveHotKeys(hotkeys, hwnd, L"Hotkeys");
    WriteOptionBool(IDC_KEYCOMBO, L"Input", "KeyCombo");
    UpdateSettings();
    have_to_apply = 0;
  };

  if (msg == WM_INITDIALOG) {
    edit_shortcut_idx = 0;
    ReadDialogOptions(hwnd, optlst, std::size(optlst));
    // Agressive Pause
    CheckConfigHotKeys(hotkeys, hwnd, L"Hotkeys", L"A4 A5");

#ifndef _WIN64
    // Always enabled in 64 bit mode.
    EnableDlgItem(hwnd, IDC_AGGRESSIVEPAUSE, TRUE);
    EnableDlgItem(hwnd, IDC_UNIKEYHOLDMENU, TRUE);
#endif

  } else if (msg == WM_COMMAND) {
    int event = HIWORD(wParam);
    int id = LOWORD(wParam);
    if (id == IDC_ALT || id == IDC_WINKEY || id == IDC_CONTROL ||
        id == IDC_SHIFT) {
      EnableDlgItem(hwnd, IDC_SHORTCUTS_SET, 1);
    } else if (event == EN_UPDATE && id == IDC_SHORTCUTS_VK) {
      wchar_t txt[4];
      GetDlgItemText(hwnd, IDC_SHORTCUTS_VK, txt, 3);
      BYTE vKey = lstrhex2u(txt);
      wchar_t keyname[32];
      keyname[0] = L'\0';
      GetKeyNameText(MapVirtualKey(vKey, 0) << 16, keyname,
                     std::size(keyname) - 8);
      SetDlgItemText(hwnd, IDC_SHORTCUTS, keyname);

      EnableDlgItem(hwnd, IDC_SHORTCUTS_SET, 1);
    } else if (event == CBN_EDITCHANGE ||
               ((event == 0 || event == EN_UPDATE || event == CBN_SELCHANGE) &&
                (IDC_SHORTCUTS > id || id > IDC_SHORTCUTS_CLEAR))) {
      PropSheet_Changed(g_cfgwnd, hwnd);
      have_to_apply = 1;
    }

    // KEYBOARD SHORTCUTS HANDLING
    // READ Keyboard Shortcut
    if (id == IDC_SHORTCUTS_AC &&
        (event == CBN_SELCHANGE || event == CBN_EDITCHANGE)) {
      // Update the shortcut with the current one.
      wchar_t txt[64];
      int i =
          CB_GetTextAndIdx(reinterpret_cast<HWND>(lParam), txt, std::size(txt));
      edit_shortcut_idx = i;
      // MessageBox(hwnd, txt, NULL, 0);
      WORD shortcut = GetPrivateProfileInt(L"KBShortcuts", txt, 0, inipath);
      BYTE vKey = LOBYTE(shortcut);
      BYTE mod = HIBYTE(shortcut);
      SetDlgItemText(hwnd, IDC_SHORTCUTS_VK, LPTR2Hex(txt, vKey));

      CheckDlgButton(hwnd, IDC_CONTROL,
                     (mod & MOD_CONTROL) ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_SHIFT,
                     (mod & MOD_SHIFT) ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_WINKEY,
                     (mod & MOD_WIN) ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_ALT,
                     (mod & MOD_ALT) ? BST_CHECKED : BST_UNCHECKED);
      EnableDlgItem(hwnd, IDC_SHORTCUTS_SET, 0);
    }
    // WRITE Current Keyboard Shortcut to ini file
    if (id == IDC_SHORTCUTS_SET) {
      wchar_t txt[kUintDigits + 1], action_string_buf[64];
      // MOD_ALT=1, MOD_CONTROL=2, MOD_SHIFT=4, MOD_WIN=8
      GetDlgItemText(hwnd, IDC_SHORTCUTS_VK, txt, 3);
      BYTE vKey = lstrhex2u(txt);
      BYTE mod = IsChecked(IDC_ALT) | (IsChecked(IDC_CONTROL) << 1) |
                 (IsChecked(IDC_SHIFT) << 2) | (IsChecked(IDC_WINKEY) << 3);
      WORD shortcut = vKey | (mod << 8);
      action_string_buf[0] = L'\0';
      int astrlen = GetDlgItemText(hwnd, IDC_SHORTCUTS_AC, action_string_buf,
                                   std::size(action_string_buf));
      if (0 < astrlen && *action_string_buf) {
        const wchar_t* intstr = shortcut ? Uint2lStr(txt, shortcut)
                                         : nullptr;  // NULL will remove the KEY
        edit_shortcut_idx = CB_AddStringIfNotPresentAndGetIdx(
            GetDlgItem(hwnd, IDC_SHORTCUTS_AC), action_string_buf);
        WritePrivateProfileString(L"KBShortcuts", action_string_buf, intstr,
                                  inipath);
      }
      EnableDlgItem(hwnd, IDC_SHORTCUTS_SET, 0);
      SetFocus(GetDlgItem(hwnd, IDC_SHORTCUTS_AC));
      apply_all();
      return FALSE;
    }
    if (id == IDC_SHORTCUTS_PICK) {
      HWND pickhwnd = nullptr;
      WNDCLASS wnd;
      if (!GetClassInfo(g_hinst, APP_NAME L"-PickShortcut", &wnd)) {
        WNDCLASS wndd = {CS_PARENTDC,
                         PickShortcutWinProc,
                         0,
                         0,
                         g_hinst,
                         nullptr  // LoadIcon(g_hinst, iconstr[1])
                         ,
                         nullptr  // LoadCursor(NULL, IDC_ARROW)
                         ,
                         nullptr  //(HBRUSH)(COLOR_HIGHLIGHT+1)
                         ,
                         nullptr,
                         APP_NAME L"-PickShortcut"};
        RegisterClass(&wndd);
      }
      RECT rc;
      GetClientRect(hwnd, &rc);
      pickhwnd = CreateWindowEx(WS_EX_TOPMOST, APP_NAME L"-PickShortcut",
                                nullptr, WS_CHILD | WS_VISIBLE, rc.left, rc.top,
                                rc.right - rc.left, rc.bottom - rc.top, hwnd,
                                nullptr, g_hinst, nullptr);
      // Disable all other child windows
      EnumChildWindows(hwnd, EnableAllControlsProc, 0);
      EnableWindow(pickhwnd, TRUE);
      SetFocus(pickhwnd);
      SetActiveWindow(pickhwnd);
      // SetDlgItemText(hwnd, IDC_SHORTCUTS, L"Press Keys...");
      EnableDlgItem(hwnd, IDC_SHORTCUTS_PICK, 0);
    }
    if (id == IDC_SHORTCUTS_CLEAR) {
      // We must clear the current shotrcut
      CheckDlgButton(hwnd, IDC_SHIFT, BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_CONTROL, BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_WINKEY, BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_ALT, BST_UNCHECKED);
      SetDlgItemText(hwnd, IDC_SHORTCUTS_VK, L"");
      EnableDlgItem(hwnd, IDC_SHORTCUTS_SET, 1);
    }
    if (id == IDC_ADVANCEDBT_AC) {
      // We must show the advanced action modal dialog
      // and fill the IDC_SHORTCUTS_AC combobox with it.
      advancedActionParam_t acp;
      acp.base_action_lst = kbshortcut_actions;
      acp.outbuf[0] = L'\0';
      int code = DialogBoxParam(g_hinst, MAKEINTRESOURCE(IDD_ACTIONPARAM), hwnd,
                                static_cast<DLGPROC>(AdvancedActionDlgProc),
                                reinterpret_cast<LPARAM>(&acp));
      if (code == IDOK && acp.outbuf[0]) {
        HWND control = GetDlgItem(hwnd, IDC_SHORTCUTS_AC);
        int new_index = CB_AddStringIfNotPresentAndGetIdx(control, acp.outbuf);
        CB_SetCurSel(control, new_index);
        PostMessage(
            hwnd, WM_COMMAND, IDC_SHORTCUTS_AC | CBN_SELCHANGE << 16,
            reinterpret_cast<LPARAM>(GetDlgItem(hwnd, IDC_SHORTCUTS_AC)));
        edit_shortcut_idx = new_index;
        SetFocus(control);
      }
    }

  } else if (msg == WM_NOTIFY) {
    if (pnmh->code == PSN_SETACTIVE) {
      // GrabWithAlt
      wchar_t txt[64];

      GetPrivateProfileString(L"Input", L"ModKey", L"", txt, std::size(txt),
                              inipath);
      EnableDlgItem(hwnd, IDC_GRABWITHALTB_H, txt[0]);
      EnableDlgItem(hwnd, IDC_GRABWITHALTB, txt[0]);

      FillActionDropListS(hwnd, IDC_GRABWITHALT, L"GrabWithAlt", kb_actions);
      FillActionDropListS(hwnd, IDC_GRABWITHALTB, L"GrabWithAltB", kb_actions);

      // FillActionDropListS(hwnd, IDC_SHORTCUTS_AC, NULL, kbshortcut_actions);
      {  // Fill combobox with all the shortcuts that are currently configured
        wchar_t kbsec[1600];
        HWND control = GetDlgItem(hwnd, IDC_SHORTCUTS_AC);
        CB_ResetContent(control);
        GetPrivateProfileSection(L"KBShortcuts", kbsec, std::size(kbsec),
                                 inipath);
        for (wchar_t* p = kbsec; *p != L'\0'; p += lstrlen(p) + 1) {
          // MessageBox(hwnd, p, L"KBShortcuts", 0);
          wchar_t* pp = lstrchr(p, L'=');
          if (!pp) continue;  // No equal sign...
          *pp = L'\0';
          if (!lstrcmpi(p, L"UsePtWindow")) continue;
          CB_AddString(control, p);
          p = pp + 1;
        }
      }
      CB_SetCurSel(GetDlgItem(hwnd, IDC_SHORTCUTS_AC), edit_shortcut_idx);
      EnableDlgItem(hwnd, IDC_SHORTCUTS_SET, 0);
      if (pnmh->hwndFrom != nullptr)
        PostMessage(
            hwnd, WM_COMMAND, IDC_SHORTCUTS_AC | CBN_SELCHANGE << 16,
            reinterpret_cast<LPARAM>(GetDlgItem(hwnd, IDC_SHORTCUTS_AC)));

      // ModKey init
      HWND control = GetDlgItem(hwnd, IDC_MODKEY);
      CB_ResetContent(control);
      unsigned j = 0, sel = 0;
      for (j = 0; j < std::size(togglekeys) - 1; j++) {
        wchar_t key_name[256];
        lstrcpy_noaccel(key_name, std::size(key_name),
                        l10n->Get(togglekeys[j].l10nidx));
        CB_AddString(control, key_name);
        if (!lstrcmpi(txt, togglekeys[j].action)) {
          sel = j;
        }
      }
      // Add the current ModKey string to the list if not found!
      if (sel == 0 && txt[0]) {
        CB_AddString(control, &txt[0]);
        sel = std::size(togglekeys) - 1;
      }
      CB_SetCurSel(control, sel);  // select current ModKey

      // Update text
      static const dialogstring_t strlst[] = {
          {IDC_KEYBOARD_BOX, L10nId::ConfigTabKeyboard},
          {IDC_SCROLLLOCKSTATE, L10nId::InputScrollLockState},
          {IDC_UNIKEYHOLDMENU, L10nId::InputUniKeyHoldMenu},
          {IDC_HOTKEYS_BOX, L10nId::InputHotkeysBox},
          {IDC_MODKEY_H, L10nId::InputHotkeysModKey},

          {IDC_ALT, L10nId::InputHotkeysAlt},
          {IDC_SHIFT, L10nId::InputHotkeysShift},
          {IDC_CONTROL, L10nId::InputHotkeysCtrl},
          {IDC_WINKEY, L10nId::InputHotkeysWinkey},
          {IDC_SHORTCUTS_H, L10nId::InputHotkeysShortcuts},
          {IDC_SHORTCUTS_PICK, L10nId::InputHotkeysShortcutsPick},
          {IDC_SHORTCUTS_CLEAR, L10nId::InputHotkeysShortcutsClear},
          {IDC_SHORTCUTS_SET, L10nId::InputHotkeysShortcutsSet},
          {IDC_USEPTWINDOW, L10nId::InputHotkeysUsePtWindow},
          {IDC_LEFTALT, L10nId::InputHotkeysLeftAlt},
          {IDC_RIGHTALT, L10nId::InputHotkeysRightAlt},
          {IDC_LEFTWINKEY, L10nId::InputHotkeysLeftWinkey},
          {IDC_RIGHTWINKEY, L10nId::InputHotkeysRightWinkey},
          {IDC_LEFTCTRL, L10nId::InputHotkeysLeftCtrl},
          {IDC_RIGHTCTRL, L10nId::InputHotkeysRightCtrl},
          {IDC_HOTKEYS_MORE, L10nId::InputHotkeysMore},
          {IDC_KEYCOMBO, L10nId::InputKeyCombo},
          {IDC_GRABWITHALT_H, L10nId::InputGrabWithAlt},
          {IDC_GRABWITHALTB_H, L10nId::InputGrabWithAltB}};
      UpdateDialogStrings(hwnd, strlst, std::size(strlst));

    } else if (pnmh->code == PSN_APPLY && have_to_apply) {
      apply_all();
    }
  }
  return FALSE;
}

/////////////////////////////////////////////////////////////////////////////
static INT_PTR CALLBACK BlacklistPageDialogProc(HWND hwnd, UINT msg,
                                                WPARAM wParam, LPARAM lParam) {
  static const optlst_t optlst[] = {
      {IDC_PROCESSBLACKLIST, T_STR, 0, L"Blacklist", "Processes", L""},
      {IDC_BLACKLIST, T_STR, 0, L"Blacklist", "Windows", L""},
      {IDC_SCROLLLIST, T_STR, 0, L"Blacklist", "Scroll", L""},
      {IDC_MDIS, T_STR, 0, L"Blacklist", "MDIs", L""},
      {IDC_PAUSEBL, T_STR, 0, L"Blacklist", "Pause", L""},
  };

  static int have_to_apply = 0;

  if (msg == WM_INITDIALOG) {
    ReadDialogOptions(hwnd, optlst, std::size(optlst));
    BOOL haveProcessBL = TRUE;
    EnableDlgItem(hwnd, IDC_PROCESSBLACKLIST, haveProcessBL);
    EnableDlgItem(hwnd, IDC_PAUSEBL, haveProcessBL);
  } else if (msg == WM_COMMAND) {
    int id = LOWORD(wParam);
    int event = HIWORD(wParam);
    if (event == EN_UPDATE && id != IDC_NEWRULE && id != IDC_NEWPROGNAME &&
        id != IDC_DWMCAPBUTTON && id != IDC_GWLEXSTYLE && id != IDC_NCHITTEST &&
        id != IDC_GWLSTYLE && id != IDC_RECT) {
      PropSheet_Changed(g_cfgwnd, hwnd);  // Enable the Apply Button
      have_to_apply = 1;
    } else if (event == STN_CLICKED && id == IDC_FINDWINDOW) {
      // Get size of workspace
      int left = 0, top = 0, width = 0, height = 0;
      if (GetSystemMetrics(SM_CMONITORS) >= 1) {
        left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
      } else {  // NT4...
        width = GetSystemMetrics(SM_CXFULLSCREEN) + 32;
        height = GetSystemMetrics(SM_CYFULLSCREEN) + 256;
      }

      // Create Transparent window covering the whole workspace
      WNDCLASS wnd = {0,       FindWindowProc,
                      0,       0,
                      g_hinst, nullptr,
                      nullptr, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
                      nullptr, APP_NAME L"-find"};
      wnd.hCursor = LoadCursor(g_hinst, MAKEINTRESOURCE(IDI_FIND));
      RegisterClass(&wnd);
      HWND findhwnd =
          CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
                         wnd.lpszClassName, nullptr, WS_POPUP, 0, 0, 0, 0,
                         nullptr, nullptr, g_hinst, nullptr);
      ShowWindow(findhwnd, SW_SHOWNA);  // And show it
      MoveWindow(findhwnd, left, top, width, height, FALSE);

      // Hide icon
      ShowWindowAsync(GetDlgItem(hwnd, IDC_FINDWINDOW), SW_HIDE);
    }

  } else if (msg == WM_NOTIFY) {
    auto pnmh = reinterpret_cast<LPNMHDR>(lParam);
    if (pnmh->code == PSN_SETACTIVE) {
      // Update text
      static const dialogstring_t strlst[] = {
          {IDC_BLACKLIST_BOX, L10nId::BlacklistBox},
          {IDC_PROCESSBLACKLIST_HEADER, L10nId::BlacklistProcessBlacklist},
          {IDC_BLACKLIST_HEADER, L10nId::BlacklistBlacklist},
          {IDC_SCROLLLIST_HEADER, L10nId::BlacklistScrolllist},
          {IDC_MDISBL_HEADER, L10nId::BlacklistMDIs},
          {IDC_PAUSEBL_HEADER, L10nId::BlacklistPause},
          {IDC_FINDWINDOW_BOX, L10nId::BlacklistFindWindowBox}};
      UpdateDialogStrings(hwnd, strlst, std::size(strlst));
      for (const auto& i : optlst)
        CreateInfoTip(hwnd, i.idc, l10n->BlacklistFormat);

      // Enable or disable buttons if needed
      EnableDlgItem(hwnd, IDC_MDIS,
                    GetPrivateProfileInt(L"General", L"MDI", 1, inipath));
      EnableDlgItem(
          hwnd, IDC_PAUSEBL,
          GetPrivateProfileInt(L"KBShortcuts", L"Kill", 0, inipath) ||
              GetPrivateProfileInt(L"Advanced", L"ACMenuItems", -1, inipath) &
                  8192);
    } else if (pnmh->code == PSN_APPLY && have_to_apply) {
      // Save to the config
      WriteDialogOptions(hwnd, optlst, std::size(optlst));
      UpdateSettings();
      have_to_apply = 0;
    }
  }

  return FALSE;
}
/////////////////////////////////////////////////////////////////////////////
static LRESULT CALLBACK FindWindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam) {
  if (msg == WM_LBUTTONUP || msg == WM_MBUTTONUP || msg == WM_RBUTTONUP) {
    DWORD msgpos = GetMessagePos();
    POINT pt = {GET_X_LPARAM(msgpos), GET_Y_LPARAM(msgpos)};
    RECT fwRc;
    HWND page = PropSheet_GetCurrentPageHwnd(g_cfgwnd);
    HWND findHwnd = GetDlgItem(page, IDC_FINDWINDOW);
    GetWindowRect(findHwnd, &fwRc);
    if (PtInRect(&fwRc, pt)) return 0;

    ShowWindow(hwnd, SW_HIDE);
    if (msg == WM_LBUTTONUP) {
      //            POINT pt;
      //            GetCursorPos(&pt);
      HWND nwindow = WindowFromPoint(pt);
      HWND window = GetAncestor(nwindow, GA_ROOT);

      wchar_t title[256], classname[256];
      GetWindowText(window, title, std::size(title));
      GetClassName(window, classname, std::size(classname));

      wchar_t txt[512];
      txt[0] = '\0';
      lstrcat_s(txt, std::size(txt), title);
      lstrcat_s(txt, std::size(txt), L"|");
      lstrcat_s(txt, std::size(txt), classname);
      SetDlgItemText(page, IDC_NEWRULE, txt);

      if (GetWindowProgName(window, txt, std::size(txt))) {
        SetDlgItemText(page, IDC_NEWPROGNAME, txt);
      }
      SetDlgItemText(page, IDC_GWLSTYLE,
                     LPTR2Hex(txt, GetWindowLongPtr(window, GWL_STYLE)));
      SetDlgItemText(page, IDC_GWLEXSTYLE,
                     LPTR2Hex(txt, GetWindowLongPtr(window, GWL_EXSTYLE)));
      // WM_NCHITTEST messages info at current pt
      wchar_t tt[32];
      lstrcpy_s(txt, std::size(txt),
                Int2lStr(tt, HitTestTimeout(nwindow, pt.x, pt.y)));
      lstrcat_s(txt, std::size(txt), L"/");
      lstrcat_s(txt, std::size(txt),
                Int2lStr(tt, HitTestTimeout(window, pt.x, pt.y)));
      SetDlgItemText(page, IDC_NCHITTEST, txt);
      // IDC_DWMCAPBUTTON
      RECT rc;
      SetDlgItemText(page, IDC_DWMCAPBUTTON,
                     (GetCaptionButtonsRect(window, &rc) && PtInRect(&rc, pt))
                         ? L"Yes"
                         : L"No");

      // Window rectangle info
      if (GetWindowRectL(window, &rc)) {
        SetDlgItemText(page, IDC_RECT, RectToStr(&rc, txt));
      }
      // IDC_WINHANDLES
      lstrcpy_s(txt, std::size(txt), L"Hwnd: ");
      lstrcat_s(txt, std::size(txt),
                LPTR2Hex(tt, reinterpret_cast<ULONG_PTR>(nwindow)));
      lstrcat_s(txt, std::size(txt), L", Root: ");
      lstrcat_s(txt, std::size(txt),
                LPTR2Hex(tt, reinterpret_cast<ULONG_PTR>(window)));
      lstrcat_s(txt, std::size(txt), L", Owner: ");
      lstrcat_s(txt, std::size(txt),
                LPTR2Hex(tt, reinterpret_cast<ULONG_PTR>(
                                 GetWindow(window, GW_OWNER))));
      SetDlgItemText(page, IDC_WINHANDLES, txt);
    }
    // Show icon again
    ShowWindowAsync(findHwnd, SW_SHOW);

    DestroyWindow(hwnd);
    UnregisterClass(APP_NAME L"-find", g_hinst);
    return 0;
  }
  if (wParam && msg == WM_ERASEBKGND) {
    return 1;
  } else if (wParam && msg == WM_PAINT) {
    return 0;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}
/////////////////////////////////////////////////////////////////////////////
static INT_PTR CALLBACK AboutPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                            LPARAM lParam) {
  if (msg == WM_INITDIALOG) {
    MoveToCorner(g_cfgwnd);
  } else if (msg == WM_NOTIFY) {
    auto pnmh = reinterpret_cast<LPNMHDR>(lParam);
    if (pnmh->code == PSN_SETACTIVE) {
      wchar_t buf[128];
      // Update text
      SetDlgItemText(hwnd, IDC_ABOUT_BOX, l10n->AboutBox);
      // Append version number to version....
      lstrcpy_s(buf, std::size(buf), l10n->AboutVersion);
      lstrcat_s(buf, std::size(buf), L" " APP_VERSION);
      SetDlgItemText(hwnd, IDC_VERSION, buf /*AboutVersion X.XX*/);

      SetDlgItemText(hwnd, IDC_URL, L"https://github.com/RamonUnch/AltSnap");
      SetDlgItemText(hwnd, IDC_AUTHOR, l10n->AboutAuthor);
      SetDlgItemText(hwnd, IDC_AUTHOR2, l10n->AboutAuthor2);
      SetDlgItemText(hwnd, IDC_LICENSE, l10n->AboutLicense);
      SetDlgItemText(hwnd, IDC_TRANSLATIONS_BOX, l10n->AboutTranslationCredit);

      wchar_t txt[1024];
      txt[0] = L'\0';
      for (size_t i = 0; i < langinfo.size(); i++) {
        lstrcat_s(txt, std::size(txt), langinfo[i].lang_english.c_str());
        lstrcat_s(txt, std::size(txt), L": ");
        lstrcat_s(txt, std::size(txt), langinfo[i].author.c_str());
        if (i + 1 != langinfo.size()) {
          lstrcat_s(txt, std::size(txt), L"\r\n");
        }
      }
      SetDlgItemText(hwnd, IDC_TRANSLATIONS, txt);
    }
  }

  return FALSE;
}
static void ToggleFullScreen(HWND hwnd) {
  LONG_PTR fs = GetWindowLongPtr(hwnd, 0);
  LONG_PTR fl = GetWindowLongPtr(hwnd, GWL_STYLE);
  if (fs) {
    // We are fullscreen, we need to restore olfd style
    if (!(fs & WS_MAXIMIZE)) SendMessage(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
    if (!(fl & WS_MAXIMIZE)) fs &= ~WS_MAXIMIZE;  //
    SetWindowLongPtr(hwnd, GWL_STYLE, fs | (WS_CAPTION | WS_THICKFRAME));
    SetWindowLongPtr(hwnd, 0, 0);  // Clear fs falg.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_ASYNCWINDOWPOS | SWP_NOSIZE | SWP_NOMOVE |
                     SWP_FRAMECHANGED | SWP_NOZORDER);
  } else {
    SetWindowLongPtr(hwnd, 0, fl);  // store fs falg.
    SetWindowLongPtr(hwnd, GWL_STYLE, fl & ~(WS_CAPTION | WS_THICKFRAME));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_ASYNCWINDOWPOS | SWP_NOSIZE | SWP_NOMOVE |
                     SWP_FRAMECHANGED | SWP_NOZORDER);
    SendMessage(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
  }
}
/////////////////////////////////////////////////////////////////////////////
// Simple windows proc that draws the resizing regions.
// Label of a mouse message in the "test window" event log; null for messages
// that are not mouse events.
static const wchar_t* MouseEventName(UINT msg, WPARAM wParam) {
  switch (msg) {
    case WM_LBUTTONDOWN:
      return L"LClick D";
    case WM_RBUTTONDOWN:
      return L"RClick D";
    case WM_MBUTTONDOWN:
      return L"MClick D";
    case WM_XBUTTONDOWN:
      return HIWORD(wParam) == 1 ? L"XClick 1 D" : L"XClick 2 D";
    case WM_LBUTTONUP:
      return L"LClick U";
    case WM_RBUTTONUP:
      return L"RClick U";
    case WM_MBUTTONUP:
      return L"MClick U";
    case WM_XBUTTONUP:
      return HIWORD(wParam) == 1 ? L"XClick 1 U" : L"XClick 2 U";
    case WM_MOUSEWHEEL:
      return L"Wheel";
    case WM_MOUSEHWHEEL:
      return L"HWheel";
    default:
      return nullptr;
  }
}

static LRESULT CALLBACK TestWindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam) {
  enum { MAXLINES = 16, MAXLL = 48 };
  static UCHAR centerfrac = 24;
  static UCHAR sidefrac = 100;
  static UCHAR centermode = 1;
  static UCHAR uDarkMode = 0;  // Using dark mode?
  struct lastkeyss {
    UINT pMSG;
    WPARAM pWP;
    LPARAM pLP;
    int idx;
    wchar_t lastkey[MAXLINES][MAXLL];
  };
  const wchar_t* const buttonstr = MouseEventName(msg, wParam);

  switch (msg) {
    case WM_CREATE: {
      // uDarkMode = AllowDarkTitlebar(hwnd);

      // Allocate space for the list of last keys.
      // Ownership moves into the window; released again in WM_DESTROY.
      SetWindowLongPtr(
          hwnd, GWLP_USERDATA,
          reinterpret_cast<LONG_PTR>(std::make_unique<lastkeyss>().release()));

    } break;

    case WM_KEYDOWN:

      // Ctrl+N (VK_N = 0x4E)
      if (wParam == 0x4E && (GetKeyState(VK_CONTROL) & 0x8000)) {
        NewTestWindow();
        break;
      }
      if (wParam == VK_F11) ToggleFullScreen(hwnd);

      [[fallthrough]];
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    case WM_SYSKEYDOWN: {
      enum { invalidate_all, invalidate_lastline, invalidate_nothing };
      wchar_t txt[32];
      auto* lks = reinterpret_cast<struct lastkeyss*>(
          GetWindowLongPtr(hwnd, GWLP_USERDATA));
      if (!lks) break;
      int idx = lks->idx;
      wchar_t(*lastkey)[MAXLL] = lks->lastkey;
      char zone_to_invalidate = invalidate_all;

      if (!buttonstr) {
        // Key
        if (lParam & (1u << 30) && wParam == lks->pWP && lParam == lks->pLP) {
          // Same key repeated, Append dost to the previous idx.
          idx = (idx - 1 + MAXLINES) % MAXLINES;
          if (lstrlen(lastkey[idx]) == MAXLL - 1) {
            zone_to_invalidate = invalidate_nothing;
          } else {
            lstrcat_s(lastkey[idx], MAXLL, L".");
            zone_to_invalidate = invalidate_lastline;
          }

        } else {
          lstrcpy_s(lastkey[idx], MAXLL, L"vK=");
          lstrcat_s(lastkey[idx], MAXLL,
                    LPTR2Hex(txt, static_cast<BYTE>(wParam)));
          lstrcat_s(lastkey[idx], MAXLL,
                    lParam & (1u << 31)   ? L" U"
                    : lParam & (1u << 30) ? L" R"
                                          : L" D");
          lstrcat_s(lastkey[idx], MAXLL, L" sC=");
          lstrcat_s(lastkey[idx], MAXLL,
                    LPTR2Hex(txt, HIWORD(lParam) & 0x00FF));
          lstrcat_s(lastkey[idx], MAXLL, L", LP=");
          lstrcat_s(lastkey[idx], MAXLL, LPTR2Hex(txt, lParam));
          txt[0] = L',';
          txt[1] = L' ';
          txt[2] = L'\0';
          if (GetKeyNameText(lParam, txt + 2, std::size(txt) - 2))
            lstrcat_s(lastkey[idx], MAXLL, txt);
        }
      } else {
        // Mouse Button
        short x = LOWORD(lParam);
        short y = HIWORD(lParam);
        lstrcpy_s(lastkey[idx], MAXLL, buttonstr);
        if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
          short delta = HIWORD(wParam);
          if (delta >= 0) {
            lstrcat_s(lastkey[idx], MAXLL, L" +");
          } else {
            delta = -delta;
            lstrcat_s(lastkey[idx], MAXLL, L" -");
          }
          lstrcat_s(lastkey[idx], MAXLL, Int2lStr(txt, delta));
        }
        lstrcat_s(lastkey[idx], MAXLL, L" (");
        lstrcat_s(lastkey[idx], MAXLL, Int2lStr(txt, x));
        lstrcat_s(lastkey[idx], MAXLL, L", ");
        lstrcat_s(lastkey[idx], MAXLL, Int2lStr(txt, y));
        lstrcat_s(lastkey[idx], MAXLL, L"), WP=");
        lstrcat_s(lastkey[idx], MAXLL, LPTR2Hex(txt, wParam));
      }

      if (zone_to_invalidate != invalidate_nothing) {
        RECT crc;
        GetClientRect(hwnd, &crc);
        long lineheight = MulDiv(11, ReallyGetDpiForWindow(hwnd), 72);
        lineheight = lineheight + lineheight / 8;
        long splitheight = zone_to_invalidate == invalidate_lastline
                               ? crc.bottom - lineheight
                               : crc.bottom - lineheight * MAXLINES;
        RECT trc = {lineheight / 2, splitheight, crc.right, crc.bottom};
        InvalidateRect(hwnd, &trc, FALSE);
      }
      idx++;
      // Save to the lks struct
      idx = idx % MAXLINES;
      lks->idx = idx;
      lks->pMSG = msg;
      lks->pWP = wParam;
      lks->pLP = lParam;
    } break;

    case WM_MOVE:
    case WM_SIZE: {
      wchar_t num[kIntDigits * 4 + 4 + 1];
      wchar_t title[256 + kIntDigits * 4 + 4];
      RECT rc;
      GetWindowRectL(hwnd, &rc);
      lstrcpy_noaccel(title, std::size(title), l10n->AdvancedTestWindow);
      lstrcat_s(title, std::size(title), L": ");
      lstrcat_s(title, std::size(title), RectToStr(&rc, num));

      lstrcat_s(title, std::size(title), L" (");
      lstrcat_s(title, std::size(title), Int2lStr(num, rc.right - rc.left));
      lstrcat_s(title, std::size(title), L"x");
      lstrcat_s(title, std::size(title), Int2lStr(num, rc.bottom - rc.top));
      lstrcat_s(title, std::size(title), L")");
      SetWindowText(hwnd, title);

    } break;
    case WM_PAINT: {
      if (!GetUpdateRect(hwnd, nullptr, FALSE)) return 0;
      /* We must keep track of pens and delete them. */
      const UINT dpi = GetDpiForWindow(hwnd);
      const UINT penwidth = GetSystemMetricsForDpi(SM_CXEDGE, dpi);

      const COLORREF txtcolor =
          uDarkMode ? RGB(255, 255, 255) : GetSysColor(COLOR_BTNTEXT);
      const HBRUSH bgbrush = uDarkMode
                                 ? CreateSolidBrush(RGB(32, 32, 32))
                                 : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
      const HPEN pen = CreatePen(PS_SOLID, penwidth, txtcolor);

      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);

      RECT wRect;
      GetWindowRect(hwnd, &wRect);
      POINT Offset = {wRect.left, wRect.top};
      ScreenToClient(hwnd, &Offset);

      SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
      HPEN oripen = static_cast<HPEN>(SelectObject(hdc, pen));
      SetROP2(hdc, R2_COPYPEN);

      const int width = wRect.right - wRect.left;
      const int height = wRect.bottom - wRect.top;
      const int cwidth = width * centerfrac / 100;
      const int cheight = height * centerfrac / 100;

      FillRect(hdc, &ps.rcPaint, bgbrush);
      if (centermode != 3)
        Rectangle(hdc  // Draw central rectangle
                  ,
                  Offset.x + (width - cwidth) / 2,
                  Offset.y + (height - cheight) / 2,
                  Offset.x + (width + cwidth) / 2 + 1,
                  Offset.y + (height + cheight) / 2 + 1);

      // Side lines
      const int swidth = width * sidefrac / 100;
      const int sheight = height * sidefrac / 100;
      const int kwidth = std::max(cwidth, swidth);
      const int kheight = std::max(cheight, sheight);
      static const DWORD ptsslens[8] = {2, 2, 2, 2, 2, 2, 2, 2};
      POINT ptss[16] = {
          // Left
          {0, (height - sheight) / 2},
          {(width - kwidth) / 2, (height - sheight) / 2},
          {0, (height + sheight) / 2},
          {(width - kwidth) / 2, (height + sheight) / 2},
          // Right
          {(width + kwidth) / 2, (height - sheight) / 2},
          {width, (height - sheight) / 2},
          {(width + kwidth) / 2, (height + sheight) / 2},
          {width, (height + sheight) / 2},
          // Top
          {(width - swidth) / 2, 0},
          {(width - swidth) / 2, (height - kheight) / 2},
          {(width + swidth) / 2, 0},
          {(width + swidth) / 2, (height - kheight) / 2},
          // Bottom
          {(width - swidth) / 2, (height + kheight) / 2},
          {(width - swidth) / 2, height},
          {(width + swidth) / 2, (height + kheight) / 2},
          {(width + swidth) / 2, height},
      };
      OffsetPoints(ptss, Offset.x, Offset.y, 16);
      PolyPolyline(hdc, ptss, ptsslens, 8);
      if (centerfrac < sidefrac) {
        // We must draw 4 extra diagonal lines.
        POINT pts[8] = {
            // Top-Left
            {(width - kwidth) / 2, (height - sheight) / 2},
            {(width - cwidth) / 2, (height - cheight) / 2},
            // Top-Right
            {(width + kwidth) / 2, (height - sheight) / 2},
            {(width + cwidth) / 2, (height - cheight) / 2},
            // Bottom-Left
            {(width - swidth) / 2, (height + kheight) / 2},
            {(width - cwidth) / 2, (height + cheight) / 2},
            // Bottom-Right
            {(width + swidth) / 2, (height + kheight) / 2},
            {(width + cwidth) / 2, (height + cheight) / 2},
        };
        OffsetPoints(pts, Offset.x, Offset.y, 16);
        PolyPolyline(hdc, pts, ptsslens, std::size(pts) / 2);
      }

      if (centermode == 3) {  // Closest side mode
        // Draw diagonal lines
        POINT pts[4] = {
            {(width - cwidth) / 2, (height - cheight) / 2},
            {(width + cwidth) / 2, (height + cheight) / 2},
            {(width - cwidth) / 2, (height + cheight) / 2},
            {(width + cwidth) / 2, (height - cheight) / 2},
        };
        OffsetPoints(pts, Offset.x, Offset.y, 4);
        PolyPolyline(hdc, pts, ptsslens, std::size(pts) / 2);

        HPEN dotpen = CreatePen(PS_DOT, 1, txtcolor);
        HPEN prevpen = static_cast<HPEN>(SelectObject(hdc, dotpen));
        int oldbkmode = SetBkMode(hdc, TRANSPARENT);
        Rectangle(hdc  // Draw dashed central rectagle
                  ,
                  Offset.x + (width - cwidth) / 2,
                  Offset.y + (height - cheight) / 2,
                  (width + cwidth) / 2 + Offset.x + 1,
                  (height + cheight) / 2 + Offset.y + 1);
        // restore oldpen and delete dotpen
        SetBkMode(hdc, oldbkmode);
        DeleteObject(SelectObject(hdc, prevpen));
      }

      // Draw textual info...
      LOGFONT lfont;
      GetObject(GetStockObject(DEFAULT_GUI_FONT), sizeof(lfont), &lfont);
      lfont.lfHeight = -MulDiv(11, ReallyGetDpiForWindow(hwnd), 72);
      long lineheight = -(lfont.lfHeight + lfont.lfHeight / 8);
      auto oldfont =
          static_cast<HFONT>(SelectObject(hdc, CreateFontIndirect(&lfont)));
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, txtcolor);

      RECT crc;
      GetClientRect(hwnd, &crc);
      long splitheight = crc.bottom - lineheight * MAXLINES;

      // Draw Key Historic
      const struct lastkeyss* lks = reinterpret_cast<struct lastkeyss*>(
          GetWindowLongPtr(hwnd, GWLP_USERDATA));
      if (lks) {
        int idx = lks->idx;
        const wchar_t(*lastkey)[MAXLL] = lks->lastkey;
        for (size_t i = 0; i < MAXLINES; i++) {
          UCHAR didx = (idx + i) % MAXLINES;
          RECT trc = {
              lineheight / 2,
              static_cast<LONG>(crc.bottom - lineheight * (MAXLINES - i)),
              crc.right, crc.bottom};
          DrawText(hdc, lastkey[didx], lstrlen(lastkey[didx]), &trc,
                   DT_NOCLIP | DT_TABSTOP);
        }
      }
      const wchar_t* str = l10n->MiscZoneTestWinHelp;
      if (UseZones & 1) {
        RECT trc2 = {lineheight / 2, lineheight / 2, crc.right, splitheight};
        DrawText(hdc, str, lstrlen(str), &trc2, DT_NOCLIP | DT_TABSTOP);
      }

      SelectObject(hdc, oripen);
      DeleteObject(SelectObject(hdc, oldfont));

      EndPaint(hwnd, &ps);

      DeleteObject(pen);  // delete pen
      if (bgbrush != reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1))
        DeleteObject(bgbrush);  // Delete brush if needed.

      return 0;
    } break;

      //    case WM_ERASEBKGND:
      //        Sleep(200); break;
      //        return 1;

    case WM_UPDCFRACTION: {
      centerfrac =
          GetPrivateProfileInt(L"General", L"CenterFraction", 24, inipath);
      centermode =
          GetPrivateProfileInt(L"General", L"ResizeCenter", 1, inipath);
      sidefrac =
          GetPrivateProfileInt(L"General", L"SidesFraction", 255, inipath);
      if (sidefrac == 255) sidefrac = centerfrac;
      return 0;
    } break;
    case WM_DESTROY: {
      // Free the allocatde memory for the key list.
      const std::unique_ptr<lastkeyss> lastkeys(
          reinterpret_cast<lastkeyss*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)));
      SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);  // In case
    } break;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}
#undef MAXLINES

HWND NewTestWindow() {
  HWND testwnd = nullptr;
  WNDCLASS wnd;
  if (!GetClassInfo(g_hinst, APP_NAME L"-Test", &wnd)) {
    WNDCLASS wndd = {CS_HREDRAW | CS_VREDRAW,
                     TestWindowProc,
                     0,
                     sizeof(LONG_PTR)  // To store old GWL_STYLE
                     ,
                     g_hinst,
                     g_icons[1]  // LoadIcon(g_hinst, iconstr[1])
                     ,
                     LoadCursor(nullptr, IDC_ARROW),
                     nullptr  //(HBRUSH)(COLOR_BACKGROUND+1)
                     ,
                     nullptr,
                     APP_NAME L"-Test"};
    RegisterClass(&wndd);
  }
  wchar_t wintitle[256];
  lstrcpy_noaccel(wintitle, std::size(wintitle), l10n->AdvancedTestWindow);
  testwnd = CreateWindowEx(0, APP_NAME L"-Test", wintitle, WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                           CW_USEDEFAULT, nullptr, nullptr, g_hinst, nullptr);
  PostMessage(testwnd, WM_UPDCFRACTION, 0, 0);
  ShowWindow(testwnd, SW_SHOW);

  return testwnd;
}
HWND NewTestWindowAt(int x, int y, int width, int height) {
  HWND hwnd = NewTestWindow();
  if (hwnd) {
    RECT bd;
    FixDWMRectLL(hwnd, &bd, 0);
    MoveWindow(hwnd, x - bd.left, y - bd.top, width + bd.left + bd.right,
               height + bd.top + bd.bottom, TRUE);
  }
  return hwnd;
}

/////////////////////////////////////////////////////////////////////////////
static INT_PTR CALLBACK AdvancedPageDialogProc(HWND hwnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam) {
#define V (void*)
  static const optlst_t optlst[] = {
      {IDC_AUTOREMAXIMIZE, T_BOL, 0, L"Advanced", "AutoRemaximize", V nullptr},
      {IDC_AEROTOPMAXIMIZES, T_BMK, 0, L"Advanced", "AeroTopMaximizes",
       V 1},  // bit 0
      {IDC_AERODBCLICKSHIFT, T_BMK, 1, L"Advanced", "AeroTopMaximizes",
       V 1},  // bit 1
      {IDC_MULTIPLEINSTANCES, T_BOL, 0, L"Advanced", "MultipleInstances",
       V nullptr},
      {IDC_FULLSCREEN, T_BOL, 0, L"Advanced", "FullScreen", V 1},
      {IDC_BLMAXIMIZED, T_BOL, 0, L"Advanced", "BLMaximized", V nullptr},
      {IDC_FANCYZONE, T_BOL, 0, L"Zones", "FancyZone", V nullptr},
      {IDC_NORESTORE, T_BMK, 2, L"General", "SmartAero", V nullptr},  // bit 2
      {IDC_MAXWITHLCLICK, T_BMK, 0, L"General", "MMMaximize", V 1},   // bit 0
      {IDC_RESTOREONCLICK, T_BMK, 1, L"General", "MMMaximize",
       V nullptr},  // bit 1
      {IDC_TOPMOSTINDICATOR, T_BOL, 0, L"Advanced", "TopmostIndicator",
       V nullptr},

      {IDC_CENTERFRACTION, T_STR, 0, L"General", "CenterFraction", V L"24"},
      {IDC_SIDESFRACTION, T_STR, 0, L"General", "SidesFraction", V L"255"},
      {IDC_AEROHOFFSET, T_STR, 0, L"General", "AeroHoffset", V L"50"},
      {IDC_AEROVOFFSET, T_STR, 0, L"General", "AeroVoffset", V L"50"},
      {IDC_SNAPTHRESHOLD, T_STR, 0, L"Advanced", "SnapThreshold", V L"20"},
      {IDC_AEROTHRESHOLD, T_STR, 0, L"Advanced", "AeroThreshold", V L"5"},
      {IDC_SNAPGAP, T_STR, 0, L"Advanced", "SnapGap", V L"0"},
      {IDC_AEROSPEED, T_STR, 0, L"Advanced", "AeroMaxSpeed", V L""},
      {IDC_AEROSPEEDTAU, T_STR, 0, L"Advanced", "AeroSpeedTau", V L"32"},
      {IDC_MOVETRANS, T_STR, 0, L"General", "MoveTrans", V L""},
  };
#undef V

  //    static HWND testwnd=NULL;
  static int have_to_apply = 0;
  if (msg == WM_INITDIALOG) {
    ReadDialogOptions(hwnd, optlst, std::size(optlst));
#ifndef _WIN64
    EnableDlgItem(hwnd, IDC_FANCYZONE, 0);
#endif

  } else if (msg == WM_COMMAND) {
    int id = LOWORD(wParam);
    int event = HIWORD(wParam);

    if (id != IDC_TESTWINDOW && (event == 0 || event == EN_UPDATE)) {
      PropSheet_Changed(g_cfgwnd, hwnd);
      have_to_apply = 1;
    }
    if (id == IDC_TESTWINDOW) {  // Click on the Test Window button
      NewTestWindow();
    }
  } else if (msg == WM_NOTIFY) {
    auto pnmh = reinterpret_cast<LPNMHDR>(lParam);
    if (pnmh->code == PSN_SETACTIVE) {
      // Update text
      static const dialogstring_t strlst[] = {
          {IDC_METRICS_BOX, L10nId::AdvancedMetricsBox},
          {IDC_CENTERFRACTION_H, L10nId::AdvancedCenterFraction},
          {IDC_AEROHOFFSET_H, L10nId::AdvancedAeroHoffset},
          {IDC_AEROVOFFSET_H, L10nId::AdvancedAeroVoffset},
          {IDC_SNAPTHRESHOLD_H, L10nId::AdvancedSnapThreshold},
          {IDC_AEROTHRESHOLD_H, L10nId::AdvancedAeroThreshold},
          {IDC_SNAPGAP_H, L10nId::AdvancedSnapGap},
          {IDC_AEROSPEED_H, L10nId::AdvancedAeroSpeed},
          {IDC_MOVETRANS_H, L10nId::AdvancedMoveTrans},
          {IDC_TESTWINDOW, L10nId::AdvancedTestWindow},

          {IDC_BEHAVIOR_BOX, L10nId::AdvancedBehaviorBox},
          {IDC_MULTIPLEINSTANCES, L10nId::AdvancedMultipleInstances},
          {IDC_AUTOREMAXIMIZE, L10nId::AdvancedAutoRemaximize},
          {IDC_AEROTOPMAXIMIZES, L10nId::AdvancedAeroTopMaximizes},
          {IDC_AERODBCLICKSHIFT, L10nId::AdvancedAeroDBClickShift},
          {IDC_MAXWITHLCLICK, L10nId::AdvancedMaxWithLClick},
          {IDC_RESTOREONCLICK, L10nId::AdvancedRestoreOnClick},
          {IDC_FULLSCREEN, L10nId::AdvancedFullScreen},
          {IDC_BLMAXIMIZED, L10nId::AdvancedBLMaximized},
          {IDC_FANCYZONE, L10nId::AdvancedFancyZone},
          {IDC_NORESTORE, L10nId::AdvancedNoRestore},
          {IDC_TOPMOSTINDICATOR, L10nId::AdvancedTopmostIndicator},
      };
      UpdateDialogStrings(hwnd, strlst, std::size(strlst));

    } else if (pnmh->code == PSN_APPLY && have_to_apply) {
      // Apply or OK button was pressed.
      // Save settings
      WriteDialogOptions(hwnd, optlst, std::size(optlst));
      UpdateSettings();
      // Update Test windows in if open.
      EnumThreadWindows(GetCurrentThreadId(), RefreshTestWin, 0);
      have_to_apply = 0;
    }
  }
  return FALSE;
}
