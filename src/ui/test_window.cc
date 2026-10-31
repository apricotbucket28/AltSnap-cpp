// test window (split out of config.cc).

#include "ui/test_window.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "app/altsnap.h"
#include "app/languages.h"
#include "app/tray.h"
#include "common/ini_section.h"
#include "common/text_format.h"
#include "hooks/hooks.h"
#include "resource.h"
#include "ui/dialog_util.h"

namespace altsnap::app {

// to be used with EnumDesktopWindows()
BOOL CALLBACK RefreshTestWin(HWND hwnd, LPARAM lp) {
  wchar_t classn[256];
  if (GetClassName(hwnd, classn, std::size(classn)) &&
      !lstrcmp(APP_NAME L"-Test", classn)) {
    PostMessage(hwnd, WM_UPDCFRACTION, 0, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOSIZE);
  }
  return TRUE;
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
    std::array<std::wstring, MAXLINES> lastkey;  // Ring buffer of log lines
  };
  // The log lines are limited to MAXLL - 1 characters.
  const auto truncated = [](std::wstring text) {
    text.resize(std::min<size_t>(text.size(), MAXLL - 1));
    return text;
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
      auto* lks = reinterpret_cast<struct lastkeyss*>(
          GetWindowLongPtr(hwnd, GWLP_USERDATA));
      if (!lks) break;
      int idx = lks->idx;
      auto& lastkey = lks->lastkey;
      char zone_to_invalidate = invalidate_all;

      if (!buttonstr) {
        // Key
        if (lParam & (1u << 30) && wParam == lks->pWP && lParam == lks->pLP) {
          // Same key repeated, Append dost to the previous idx.
          idx = (idx - 1 + MAXLINES) % MAXLINES;
          if (lastkey[idx].size() == MAXLL - 1) {
            zone_to_invalidate = invalidate_nothing;
          } else {
            lastkey[idx] += L'.';
            zone_to_invalidate = invalidate_lastline;
          }

        } else {
          wchar_t key_name[64];
          if (!GetKeyNameText(lParam, key_name, std::size(key_name))) {
            key_name[0] = L'\0';
          }
          lastkey[idx] = truncated(FormatKeyEvent(wParam, lParam, key_name));
        }
      } else {
        // Mouse Button
        lastkey[idx] = truncated(FormatMouseEvent(
            buttonstr, msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL, wParam,
            lParam));
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
      RECT rc;
      GetWindowRectL(hwnd, &rc);
      const std::wstring title = std::format(
          L"{}: {} ({}x{})", RemoveAccelerator(l10n->AdvancedTestWindow),
          FormatRect(rc), rc.right - rc.left, rc.bottom - rc.top);
      SetWindowText(hwnd, title.c_str());

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
        const auto& lastkey = lks->lastkey;
        for (size_t i = 0; i < MAXLINES; i++) {
          UCHAR didx = (idx + i) % MAXLINES;
          RECT trc = {
              lineheight / 2,
              static_cast<LONG>(crc.bottom - lineheight * (MAXLINES - i)),
              crc.right, crc.bottom};
          DrawText(hdc, lastkey[didx].c_str(),
                   static_cast<int>(lastkey[didx].size()), &trc,
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
      centerfrac = GetPrivateProfileInt(L"General", L"CenterFraction", 24,
                                        inipath.c_str());
      centermode =
          GetPrivateProfileInt(L"General", L"ResizeCenter", 1, inipath.c_str());
      sidefrac = GetPrivateProfileInt(L"General", L"SidesFraction", 255,
                                      inipath.c_str());
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
  CopyWithoutAccelerator(wintitle, std::size(wintitle),
                         l10n->AdvancedTestWindow);
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
}  // namespace altsnap::app
