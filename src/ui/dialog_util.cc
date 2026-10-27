// dialog util (split out of config.cc).

#include "ui/dialog_util.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "app/altsnap.h"
#include "app/languages.h"
#include "app/tray.h"
#include "common/ini_section.h"
#include "hooks/hooks.h"
#include "resource.h"

namespace altsnap::app {

void MoveButtonUporDown(WORD id, WINDOWPLACEMENT* wndpl, int diffrows) {
  HWND button = GetDlgItem(g_cfgwnd, id);
  GetWindowPlacement(button, wndpl);
  int height = wndpl->rcNormalPosition.bottom - wndpl->rcNormalPosition.top;
  wndpl->rcNormalPosition.top += 18 * diffrows;
  wndpl->rcNormalPosition.bottom = wndpl->rcNormalPosition.top + height;
  SetWindowPlacement(button, wndpl);
}
void MoveToCorner(HWND hwnd) {
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

int CB_GetCurSelDlgItem(HWND hwnd, UINT id) {
  return static_cast<int>(static_cast<DWORD>(
      SendMessage(GetDlgItem(hwnd, id), CB_GETCURSEL, 0, 0)));
}
LRESULT CB_GetTextAndIdx(HWND hwnd, wchar_t* txt, size_t txtlen) {
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

LRESULT CB_AddStringIfNotPresentAndGetIdx(HWND hwnd, const wchar_t* txt) {
  LRESULT new_index =
      SendMessage(hwnd, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                  reinterpret_cast<LPARAM>(txt));
  if (new_index < 0) new_index = ComboBox_AddString(hwnd, txt);
  return new_index;
}
void WriteOptionBool(HWND hwnd, WORD id, const wchar_t* section,
                     const char* name_s) {
  wchar_t name[64];
  str2tchar(name, name_s);
  WritePrivateProfileString(
      section, name, IsDlgButtonChecked(hwnd, id) ? L"1" : L"0", inipath);
}

int WriteOptionBoolBit(HWND hwnd, WORD id, const wchar_t* section,
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
void WriteOptionStr(HWND hwnd, WORD id, const wchar_t* section,
                    const char* name_s) {
  wchar_t name[64];
  str2tchar(name, name_s);
  HWND item = GetDlgItem(hwnd, id);
  const int len = GetWindowTextLength(item);
  std::wstring text(static_cast<size_t>(len) + 1, L'\0');
  GetWindowText(item, text.data(), len + 1);
  WritePrivateProfileString(section, name, text.c_str(), inipath);
}
void ReadOptionStr(HWND hwnd, WORD id, const wchar_t* section,
                   const char* name_s, const wchar_t* def) {
  wchar_t name[64];
  str2tchar(name, name_s);
  SetDlgItemText(hwnd, id, ReadIniString(section, name, def, inipath).c_str());
}
int ReadOptionInt(HWND hwnd, WORD id, const wchar_t* section,
                  const char* name_s, int def, int mask) {
  wchar_t name[64];
  str2tchar(name, name_s);
  int ret = GetPrivateProfileInt(section, name, def, inipath);
  CheckDlgButton(hwnd, id, (ret & mask) ? BST_CHECKED : BST_UNCHECKED);
  return ret;
}
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
HWND CreateInfoTip(HWND hDlg, int toolID, const wchar_t* const pszText) {
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
void UpdateDialogStrings(HWND hwnd, const dialogstring_t* const strlst,
                         size_t size) {
  for (size_t i = 0; i < size; i++) {
    SetDlgItemText(hwnd, strlst[i].idc, l10n->Get(strlst[i].l10nidx));
    CreateInfoTip(hwnd, strlst[i].idc, l10n->Get(strlst[i].l10nidx + 1));
  }
}
void ReadDialogOptions(HWND hwnd, const optlst_t* ol, size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (ol[i].type == T_BOL)
      ReadOptionInt(hwnd, ol[i].idc, ol[i].section, ol[i].name,
                    static_cast<int>(reinterpret_cast<DorQWORD>(ol[i].def)),
                    -1);
    else if (ol[i].type == T_BMK)
      ReadOptionInt(hwnd, ol[i].idc, ol[i].section, ol[i].name,
                    static_cast<int>(reinterpret_cast<DorQWORD>(ol[i].def)),
                    1 << ol[i].bitN);
    else
      ReadOptionStr(hwnd, ol[i].idc, ol[i].section, ol[i].name,
                    static_cast<const wchar_t*>(ol[i].def));
  }
}
void WriteDialogOptions(HWND hwnd, const optlst_t* ol, size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (ol[i].type == T_BOL)
      WriteOptionBool(hwnd, ol[i].idc, ol[i].section, ol[i].name);
    else if (ol[i].type == T_BMK)
      WriteOptionBoolBit(hwnd, ol[i].idc, ol[i].section, ol[i].name,
                         ol[i].bitN);
    else
      WriteOptionStr(hwnd, ol[i].idc, ol[i].section, ol[i].name);
  }
}

static int FindIDCStrIDX(const dialogstring_t sl[], size_t len, int idc) {
  for (size_t i = 0; i < len; i++) {
    if (idc == static_cast<int>(sl[i].idc)) return sl[i].l10nidx;
  }
  return -1;
}
void ShowContextHelp(const dialogstring_t sl[], size_t len, HWND hwnd,
                     LPHELPINFO hi) {
  if (hi->iContextType == HELPINFO_WINDOW) {
    int id = FindIDCStrIDX(sl, len, hi->iCtrlId);
    if (id >= 0) {
      SetDlgItemText(hwnd, IDC_HELPPANNEL, l10n->Get(id));
      // MessageBox(hwnd, l10n->Get(id), NULL, 0);
    }
  }
}

BOOL CALLBACK EnableAllControlsProc(HWND hwnd, LPARAM lp) {
  if (IsWindowVisible(hwnd)) {
    EnableWindow(hwnd, lp);
  }
  return TRUE;
}
}  // namespace altsnap::app
