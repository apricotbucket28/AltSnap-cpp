// window menu (split out of hooks.cc).

#include "hooks/overlay/window_menu.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "common/ini_section.h"
#include "hooks/actions/click_actions.h"
#include "hooks/actions/window_actions.h"
#include "hooks/blacklist.h"
#include "hooks/hooks_internal.h"
#include "hooks/overlay/pin_window.h"
#include "hooks/overlay/trans_window.h"
#include "hooks/send_input.h"
#include "hooks/window/snap.h"
#include "hooks/window/window_enum.h"
#include "hooks/window/window_move.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

// Destroy AltSnap's menu
void KillAltSnapMenu() {
  //    if (state.unikeymenu) {
  //        EnableWindow(g_mchwnd, FALSE);
  //        DestroyMenu(state.unikeymenu);
  //        state.unikeymenu = NULL;
  //    }
  if (g_mchwnd) {
    SendMessage(g_mchwnd, WM_CLOSE, 0, 0);
    g_mchwnd = nullptr;
  }
  state.unikeymenu = nullptr;
}
// Make a menu filled with the windows that are enumed through EnumProc
// And Track it!!!!
static wchar_t Int2Accel(int i) {
  return conf.MenuAccelMap[i];
}
static WORD Accel2Int(UCHAR c) {
  for (size_t i = 0; i < std::size(conf.MenuAccelMap) - 1; i++)
    if (conf.MenuAccelMap[i] == c) return static_cast<WORD>(i);
  return static_cast<WORD>(0xFFFF);
}
// #include <oleacc.h>
struct menuitemdata {
  MSAAMENUINFO msaa;
  std::wstring text;  // "&X - window title"
  HICON icon;
};
void TrackMenuOfWindows(WNDENUMPROC EnumProc, LPARAM flags) {
  state.sclickhwnd = nullptr;
  KillAltSnapMenu();
  g_mchwnd = KreateMsgWin(MenuWindowProc, APP_NAME L"-SClick", 3);
  if (!g_mchwnd) return;  // Failed to create g_mchwnd...
  // Fill up hwnds[] with the stacked windows.
  hwnds.clear();
  HWND mdiclient = state.mdiclient;
  if (mdiclient) {
    EnumChildWindows(mdiclient, EnumProc, flags & TRK_LASERMODE);
  } else {
    EnumDesktopWindows(nullptr, EnumProc, flags & TRK_LASERMODE);
  }
  LOG("Number of stacked windows = %u", static_cast<unsigned>(hwnds.size()));
  if (hwnds.empty()) return;

  state.sclickhwnd = state.hwnd;
  if (hwnds.size() > 36) hwnds.resize(36);  // Max 36 stacked windows

  HMENU menu = CreatePopupMenu();
  state.unikeymenu = menu;
  size_t i = 0;

  struct menuitemdata data[36];  // Always fits into the stack
  for (i = 0; i < hwnds.size(); i++) {
    int textlen = GetWindowTextLength(hwnds[i]);
    // Limit the size to the asked width
    if (conf.MaxMenuWidth) textlen = std::min<int>(textlen, conf.MaxMenuWidth);

    std::wstring title(static_cast<size_t>(textlen) + 1, L'\0');
    GetWindowText(hwnds[i], title.data(), textlen + 1);
    title.resize(lstrlen(title.c_str()));
    data[i].text = std::wstring{L'&', Int2Accel(i), L' ', L'-', L' '} + title;
    // Fill up MSAA structure for screen readers.
    data[i].msaa.dwMSAASignature = MSAA_MENU_SIG;
    data[i].msaa.cchWText = static_cast<DWORD>(data[i].text.size());
    data[i].msaa.pszWText = data[i].text.data();

    // GetWindowIcon return a shared resource not to be freed
    data[i].icon = GetWindowIcon(hwnds[i]);
    MENUITEMINFO lpmi = {sizeof(lpmi)};
    lpmi.fMask = MIIM_DATA | MIIM_TYPE | MIIM_ID;
    lpmi.fType = MFT_OWNERDRAW; /*MFT_STRING*/
    lpmi.wID = i + 1;           // Id starts at 1 because 0 is for ESCAPE.
    lpmi.dwItemData = reinterpret_cast<ULONG_PTR>(&data[i]);
    lpmi.dwTypeData = reinterpret_cast<LPWSTR>(&data[i].msaa);
    lpmi.cch = sizeof(MSAAMENUINFO);
    InsertMenuItem(menu, i + 1, FALSE, &lpmi);
  }
  POINT pt;
  GetCursorPos(&pt);
  BringWindowToTop(g_mchwnd);
  SetForegroundWindow(g_mchwnd);
  i = static_cast<size_t>(TrackPopupMenu(
      menu,
      TPM_RETURNCMD /*|TPM_NONOTIFY*/ | GetSystemMetrics(SM_MENUDROPALIGNMENT),
      pt.x, pt.y, 0, g_mchwnd, nullptr));
  state.mdiclient = mdiclient;
  LOG("menu=%u", static_cast<unsigned>(i));
  // if the return value is in the range..
  if (0 < i && i <= hwnds.size()) {
    HWND hwnd = hwnds[i - 1];
    //        wchar_t buf[128];
    //        PrintHwndDetails(hwnd, buf);
    //        MessageBox(NULL, buf, L"Info", 0);
    if (IsIconic(hwnd)) RestoreWindow(hwnd);
    SetForegroundWindowL(hwnd);

    if (flags & TRK_MOVETOMONITOR) MoveToCurrentMonitorIfNeeded(hwnd);
  }

  DestroyMenu(menu);
  DestroyWindow(g_mchwnd);
  g_mchwnd = nullptr;

  return;
}
void ActionStackList(int lasermode) {
  PostMessage(g_mainhwnd, WM_STACKLIST, lasermode,
              reinterpret_cast<LPARAM>(EnumStackedWindowsProc));
}
void ActionMenu(HWND hwnd) {
  state.sclickhwnd = nullptr;
  KillAltSnapMenu();
  g_mchwnd = KreateMsgWin(MenuWindowProc, APP_NAME L"-SClick", 1);
  state.sclickhwnd = hwnd;
  // Send message to Open Action Menu
  BringWindowToTop(g_mainhwnd);
  SetForegroundWindow(g_mainhwnd);
  PostMessage(g_mainhwnd, WM_SCLICK, reinterpret_cast<WPARAM>(g_mchwnd),
              (!state.ignorept)  // LP_CURSORPOS
                  | !!(GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST)
                        << 1                                    // LP_TOPMOST
                  | !!GetBorderlessFlag(hwnd) << 2              // LP_BORDERLESS
                  | IsZoomed(hwnd) << 3                         // LP_MAXIMIZED
                  | !!(GetRestoreFlag(hwnd) & 2) << 4           // LP_ROLLED
                  | !!GetProp(state.hwnd, kPropMoveOnOff) << 5  // LP_MOVEONOFF
                  | (state.alt <= BT_MB5) << 6  // LP_NOALTACTION
  );
}
///////////////////////////////////////////////////////////////////////////
// Function to calculate the necessary dimentions for the menuitem.
// In response to the WM_MEASUREITEM message
static LPARAM MeasureMenuItem(HWND hwnd, WPARAM wParam, LPARAM lParam, UINT dpi,
                              HFONT mfont) {
  auto lpmi = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
  if (!lpmi) return FALSE;
  auto* data = reinterpret_cast<struct menuitemdata*>(lpmi->itemData);
  if (!data) return FALSE;
  const wchar_t* text = data->text.c_str();
  // LOGA("WM_MEASUREITEM: id=%u, txt=%ls", lpmi->itemID, data->txtptr);

  HDC dc = GetDC(hwnd);

  // Select proper font.
  auto oldfont = static_cast<HFONT>(SelectObject(dc, mfont));

  int xmargin = GetSystemMetricsForDpi(SM_CXFIXEDFRAME, dpi);
  int ymargin = GetSystemMetricsForDpi(SM_CYFIXEDFRAME, dpi);
  int xicosz = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
  int yicosz = GetSystemMetricsForDpi(SM_CYSMICON, dpi);

  if (dpi == 0) {
    xmargin = xmargin * conf.MenuZoom / 100;
    ymargin = ymargin * conf.MenuZoom / 100;
    xicosz = xicosz * conf.MenuZoom / 100;
    yicosz = yicosz * conf.MenuZoom / 100;
  }

  SIZE sz;  // Get text size in both dimentions
  GetTextExtentPoint32(dc, text, lstrlen(text), &sz);

  // Text width + icon width + 4 margins
  lpmi->itemWidth = sz.cx + xicosz + 4 * xmargin;

  // Text height/Icon height + margin
  lpmi->itemHeight = std::max<UINT>(sz.cy, yicosz) + ymargin;

  SelectObject(dc, oldfont);  // restore old font
  ReleaseDC(hwnd, dc);
  return TRUE;
}
///////////////////////////////////////////////////////////////////////////
// Function to custom draw the menu item, in response to WM_DRAWITEM.
// We must both draw he small icon and the menu text.
// We must also draw the selected menu with the highligh color.
static LRESULT DrawMenuItem(HWND hwnd, WPARAM wParam, LPARAM lParam, UINT dpi,
                            HFONT mfont) {
  auto di = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
  if (!di) return FALSE;
  auto* data = reinterpret_cast<struct menuitemdata*>(di->itemData);
  if (!data) return FALSE;

  // Try to be dpi-aware as good as we can...
  int xmargin = GetSystemMetricsForDpi(SM_CXFIXEDFRAME, dpi);
  int xicosz = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
  int yicosz = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
  if (dpi == 0) {
    xmargin = xmargin * conf.MenuZoom / 100;
    xicosz = xicosz * conf.MenuZoom / 100;
    yicosz = yicosz * conf.MenuZoom / 100;
  }

  // LOGA("WM_DRAWITEM: id=%u, txt=%ls", di->itemID, data->txtptr);

  int bgcol = 0, txcol = 0;
  if (di->itemState & ODS_SELECTED) {
    // Item is highlited
    bgcol = COLOR_HIGHLIGHT;
    txcol = COLOR_HIGHLIGHTTEXT;
  } else {
    // normal
    bgcol = COLOR_MENU;
    txcol = COLOR_MENUTEXT;
  }
  if (di->itemState & ODS_GRAYED) {
    txcol = COLOR_GRAYTEXT;
  }

  HBRUSH bgbrush = GetSysColorBrush(bgcol);
  // Set
  SetBkColor(di->hDC, GetSysColor(bgcol));
  SetTextColor(di->hDC, GetSysColor(txcol));

  // Highlight menu entry
  // RECT bgrc; CopyRect(&bgrc, &di->rcItem); bgrc.bottom+=1; bgrc.left+=1;
  FillRect(di->hDC, &di->rcItem, bgbrush);

  auto oldfont = static_cast<HFONT>(SelectObject(di->hDC, mfont));

  SIZE sz;
  GetTextExtentPoint32(di->hDC, data->text.c_str(),
                       static_cast<int>(data->text.size()), &sz);
  // LOGA("WM_DRAWITEM: txtXY=%u, %u, txt=%ls", (UINT)sz.cx, (UINT)sz.cy,
  // data->txtptr);

  int totheight = di->rcItem.bottom - di->rcItem.top;  // total menuitem height
  int yicooffset = (totheight - yicosz) / 2;           // Center icon vertically
  int ytxtoffset = (totheight - sz.cy) / 2;            // Center text vertically

  DrawIconEx(
      di->hDC, di->rcItem.left + xmargin, di->rcItem.top + yicooffset,
      (di->itemState & ODS_GRAYED) ? LoadIcon(nullptr, IDI_HAND) : data->icon,
      xicosz, yicosz, 0, nullptr, DI_NORMAL);
  if (IsIconic(hwnds[di->itemID - 1])) {
    HPEN npen = CreatePen(PS_SOLID, yicosz / 5, GetSysColor(txcol));
    HPEN prevpen = static_cast<HPEN>(SelectObject(di->hDC, npen));
    MoveToEx(di->hDC, di->rcItem.left + xmargin,
             di->rcItem.top + yicooffset + yicosz - 1, nullptr);
    LineTo(di->hDC, di->rcItem.left + xmargin + xicosz,
           di->rcItem.top + yicooffset + yicosz - 1);
    DeleteObject(SelectObject(di->hDC, prevpen));
  }
  // TODO: Draw a cross...
  //    if (state.ctrl) {
  //        HPEN npen = CreatePen(PS_SOLID, yicosz/5, GetSysColor(txcol));
  //        HPEN prevpen = SelectObject(di->hDC, npen);
  //        MoveToEx(di->hDC, di->rcItem.left+xmargin,        di->rcItem.top +
  //        yicooffset, NULL); LineTo(di->hDC,   di->rcItem.left+xmargin+xicosz,
  //        di->rcItem.top + yicooffset+yicosz); MoveToEx(di->hDC,
  //        di->rcItem.left+xmargin, di->rcItem.top + yicooffset+yicosz, NULL);
  //        LineTo(di->hDC,   di->rcItem.left+xmargin+xicosz, di->rcItem.top +
  //        yicooffset);
  //
  //        DeleteObject(SelectObject(di->hDC, prevpen));
  //    }

  // Adjust x offset for Text drawing...
  di->rcItem.left += xicosz + xmargin * 3;
  di->rcItem.top += ytxtoffset;
  // LOGA("menuitemheight = %ld", di->rcItem.bottom-di->rcItem.top);
  DrawText(di->hDC, data->text.c_str(), -1, &di->rcItem, 0);  // Menuitem Text

  // Restore dc context
  SelectObject(di->hDC, oldfont);  // restore old font

  return TRUE;
}
static void SendSYSCOMMANDToMenuItem(HWND hwnd, int id, HMENU hmenu,
                                     WPARAM sc_command) {
  if (conf.RCCloseMItem && 0 <= id && static_cast<UINT>(id) < hwnds.size() &&
      GetWindowLongPtr(hwnd, GWLP_USERDATA) == 3 && IsWindow(hwnds[id])) {
    if (sc_command == SC_CLOSE  // remove topmost flag for close command
        && GetWindowLongPtr(hwnds[id], GWL_EXSTYLE) & WS_EX_TOPMOST) {
      HWND pinhwnd = GetPinWindow(hwnds[id]);
      if (pinhwnd) DestroyWindow(pinhwnd);
    }
    PostMessage(hwnds[id], WM_SYSCOMMAND, sc_command, 0);

    if (sc_command == SC_CLOSE)
      EnableMenuItem(hmenu, id, MF_BYPOSITION | MF_GRAYED);
  }
}
/////////////////////////////////////////////////////////////////////////////
// Window for single click commands for menu
LRESULT CALLBACK MenuWindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                LPARAM lParam) {
  static UINT dpi = 0;
  static HFONT mfont = nullptr;
  static HWND fhwndori = nullptr;
  switch (msg) {
    case WM_CREATE: {
      // per-monitor dpi awareness since Windows 8.1
      POINT pt;
      UINT dpiX = 0, dpiY = 0;
      GetCursorPos(&pt);
      HMONITOR hmon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
      dpi = 0;
      if (hmon && 0 == GetDpiForMonitor(hmon, MDT_DEFAULT, &dpiX, &dpiY))
        dpi = dpiY;  // OK

      dpi = dpi * conf.MenuZoom / 100;
      // dpi = GetDpiForWindow10L(hwnd) * conf.MenuZoom / 100;

      // Save the original foreground window.
      fhwndori = GetForegroundWindow();
      mfont = nullptr;
    } break;
    case WM_DPICHANGED: {
      dpi = LOWORD(wParam) * conf.MenuZoom /
            100;  // Update dpi value if changed...
      if (mfont) {
        DeleteObject(mfont);  // Delete menufont if needed.
        mfont = nullptr;
      }
    } break;
    case WM_INITMENU:
      state.unikeymenu = reinterpret_cast<HMENU>(wParam);
      break;
    case WM_GETCLICKHWND:
      return reinterpret_cast<LRESULT>(state.sclickhwnd);
    case WM_COMMAND: {
      if (LOWORD(wParam)) {
        // UNIKEY MENU (LOWORD of wParam munst be non NULL.
        // LOG("Unikey menu WM_COMMAND, wp=%X, lp=%X", (UINT)wParam,
        // (UINT)lParam);
        SendKey(VK_BACK);  // Errase old char...

        // Send UCS-2 or Lower+Upper UTF-16 surrogates of the UNICODE char.
        SendUnicodeKey(LOWORD(wParam));  // USC-2 or Lower surrogate
        if (HIWORD(wParam)) SendUnicodeKey(HIWORD(wParam));  // Upper surrogate

        state.sclickhwnd = nullptr;
      } else if (HIWORD(wParam) && IsWindow(state.sclickhwnd)) {
        // ACTION MENU LOWORD(wParam) has to be zero to differenctiae with
        // unikey menu
        LOG("Action Menu WM_COMMAND, wp=%X, lp=%X", static_cast<UINT>(wParam),
            static_cast<UINT>(lParam));
        action_t action = {static_cast<BYTE>(HIWORD(wParam)), 0,
                           0};  // TOTO: FIXME!!!!!!!
        if (action.ac) {
          state.prevpt = state.clickpt;
          if (action.ac == AC_ORICLICK) {
            ShowWindow(hwnd, SW_HIDE);
            SetCursorPos(state.clickpt.x, state.clickpt.y);
            SendClick(state.clickbutton);
          } else {
            SClickActions(state.sclickhwnd, action);
          }

          // We should not refocus windows if those
          // actions were performed...
          if (action.ac == AC_LOWER || action.ac == AC_MINIMIZE ||
              action.ac == AC_KILL || action.ac == AC_CLOSE)
            fhwndori = nullptr;
        }
        // Menu closes now.
        state.unikeymenu = nullptr;
        PostMessage(hwnd, WM_CLOSE, 0, 0);  // Done!
        return 0;
      }
    } break;
    case WM_MENURBUTTONUP:
    case WM_MBUTTONUP: {
      // The user released the right button.
      // We must close the corresponding Window in the windows list
      int id = wParam;  // Zero-based menu id.
      WPARAM sc_command = SC_MINIMIZE;
      auto hmenu = reinterpret_cast<HMENU>(lParam);
      if (msg == WM_MBUTTONUP) {
        // Simulate teh same thing with Middle mouse UP.
        POINT pt;
        DWORD msgpos = GetMessagePos();
        pt.x = GET_X_LPARAM(msgpos);
        pt.y = GET_Y_LPARAM(msgpos);
        hmenu = state.unikeymenu;
        id = MenuItemFromPoint(hwnd, hmenu, pt);
        sc_command = SC_CLOSE;
      }
      SendSYSCOMMANDToMenuItem(hwnd, id, hmenu, sc_command);
    } break;
    // OWNER DRAWN MENU !!!!!
    case WM_MEASUREITEM:
      // Create Menu font if not already created.
      if (!mfont) {
        // mfont = CreateNCMenuFont(dpi);
        struct NEWNONCLIENTMETRICSAW ncm;
        GetNonClientMetricsDpi(&ncm, dpi);
        if (dpi == 0)
          ncm.lfMenuFont.lfHeight =
              ncm.lfMenuFont.lfHeight * conf.MenuZoom / 100;
        mfont = CreateFontIndirect(&ncm.lfMenuFont);
      }
      return MeasureMenuItem(hwnd, wParam, lParam, dpi, mfont);

      //    case msg == WM_MENUSELECT; {
      //        USHORT id = (UINT) LOWORD(wParam); // identifier of the menu
      //        item USHORT fuFlags = (UINT) HIWORD(wParam); if (fuFlags ==
      //        0xFFFF && !lParam) {
      //            ShowTransWin(SW_HIDE);
      //        } else if (fuFlags&MF_HILITE && fuFlags&MF_OWNERDRAW
      //        && 0 < id && id <= numhwnds) {
      //            HWND selhwnd = hwnds[id-1]; // currently selected hwnd.
      //            RECT rc;
      //            GetWindowRectL(selhwnd, &rc);
      //            ShowTransWin(SW_SHOWNA);
      //            MoveTransWin(rc.left, rc.top, rc.right-rc.left,
      //            rc.bottom-rc.top);
      //        }
      //        } break;

    case WM_DRAWITEM:
      return DrawMenuItem(hwnd, wParam, lParam, dpi, mfont);

    case WM_MENUCHAR: {
      // Turn the input character into a menu identifier.
      WORD cc = LOWORD(wParam);
      auto vK = static_cast<UCHAR>(VkKeyScan(cc));  // Get the virtual key code.
      // LOGA("WM_MENUCHAR: %X => ch: %X, vK: %X", wParam, (DWORD)cc,
      // (DWORD)vK); wchar_t c = (wchar_t)( cc  |  (('A' <= cc && cc <= 'Z')<<5)
      // );
      if (cc == VK_ESCAPE) return MNC_CLOSE << 16;
      if (GetWindowLongPtr(hwnd, GWLP_USERDATA) == 3) {
        UCHAR closewindow = !conf.NumberMenuItems && conf.RCCloseMItem &&
                            GetKeyState(VK_SHIFT) & 0x8000;
        ;
        WORD item = Accel2Int(vK);
        // Execute item if the key is valid.
        if (item != 0xFFFF && item <= hwnds.size()) {
          if (closewindow)
            // PostMessage(hwnd, WM_MENURBUTTONUP, item, lParam);
            SendSYSCOMMANDToMenuItem(hwnd, item,
                                     reinterpret_cast<HMENU>(lParam), SC_CLOSE);
          else
            return item | MNC_EXECUTE << 16;
        }
      }
    } break;
      //    case WM_SYSCHAR:
      //        MessageBox(NULL, NULL, NULL, 0);
      //        break;
    case WM_KILLFOCUS:
      // Menu gets hiden, be sure to zero-out the clickhwnd
      state.sclickhwnd = nullptr;
      break;
    case WM_DESTROY: {
      LOG("Destroying Menu window!");
      if (mfont) DeleteObject(mfont);  // Delete menufont if needed.
      if (fhwndori && state.sclickhwnd == fhwndori &&
          GetWindowLongPtr(hwnd, GWLP_USERDATA) == 1 && IsWindow(fhwndori)) {
        // Restore the old foreground window
        SetForegroundWindow(fhwndori);
      }
      fhwndori = nullptr;
      state.sclickhwnd = nullptr;
      state.unikeymenu = nullptr;
    } break;
  }
  // LOGA("msg=%X, wParam=%X, lParam=%lX", msg, wParam, lParam);
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

}  // namespace altsnap::hooks
