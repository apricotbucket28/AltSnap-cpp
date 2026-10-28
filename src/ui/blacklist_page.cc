// blacklist page (split out of config.cc).

#include "ui/blacklist_page.h"

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
#include "ui/dialog_util.h"

namespace altsnap::app {

static LRESULT CALLBACK FindWindowProc(HWND, UINT, WPARAM, LPARAM);

/////////////////////////////////////////////////////////////////////////////
INT_PTR CALLBACK BlacklistPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam) {
  static const DialogOption optlst[] = {
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
      AppendString(txt, std::size(txt), title);
      AppendString(txt, std::size(txt), L"|");
      AppendString(txt, std::size(txt), classname);
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
      CopyString(txt, std::size(txt),
                 Int2lStr(tt, HitTestTimeout(nwindow, pt.x, pt.y)));
      AppendString(txt, std::size(txt), L"/");
      AppendString(txt, std::size(txt),
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
      CopyString(txt, std::size(txt), L"Hwnd: ");
      AppendString(txt, std::size(txt),
                   LPTR2Hex(tt, reinterpret_cast<ULONG_PTR>(nwindow)));
      AppendString(txt, std::size(txt), L", Root: ");
      AppendString(txt, std::size(txt),
                   LPTR2Hex(tt, reinterpret_cast<ULONG_PTR>(window)));
      AppendString(txt, std::size(txt), L", Owner: ");
      AppendString(txt, std::size(txt),
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
}  // namespace altsnap::app
