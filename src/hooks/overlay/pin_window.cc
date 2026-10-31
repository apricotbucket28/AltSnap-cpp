// pin window (split out of hooks.cc).

#include "hooks/overlay/pin_window.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "common/ini_section.h"
#include "hooks/actions/window_actions.h"
#include "hooks/blacklist.h"
#include "hooks/hooks_internal.h"
#include "hooks/overlay/window_menu.h"
#include "hooks/window/snap.h"
#include "hooks/window/window_enum.h"
#include "hooks/window/window_info.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

// Pin window callback function
// We store the old owner window style in GWLP_USERDATA
// and the rightoffset/topoffset in the first LONG_PTR of cbWndExtra (+0)
static LRESULT CALLBACK PinWindowProc(HWND hwnd, UINT msg, WPARAM wp,
                                      LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      SetTimer(hwnd, PIN_TIMER, conf.PinRate, nullptr);
    } break;
    case WM_DPICHANGED: {
      // Reset the oldstyle, so we have to recalculate size/offset
      // of the pin window on DPI change.
      SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    } break;
    case WM_SETTINGCHANGE: {
      if (wp == SPI_SETNONCLIENTMETRICS) {
        // We have to recalculate size/offset on global
        // non client metrics change.
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
      }
    } break;
    case WM_TIMER: {
      HWND ow = nullptr;
      if (!(ow = GetWindow(hwnd, GW_OWNER))) {
        // Owner no longer exists!
        // We have no reasons to be anymore.
        DestroyWindow(hwnd);
        return 0;
      }
      // Destroy the pin if the owner is no longer topmost
      // or no longer exists
      LONG_PTR xstyle = 0;
      if (!IsWindow(ow) || !IsWindowVisible(ow) ||
          !((xstyle = GetWindowLongPtr(ow, GWL_EXSTYLE)) & WS_EX_TOPMOST)) {
        DestroyWindow(hwnd);
        return 0;
      }

      RECT rc;  // Owner rect.
      GetWindowRect(ow, &rc);
      LONG_PTR style = GetWindowLongPtr(ow, GWL_STYLE);
      LONG_PTR OldOwStyle = GetWindowLongPtr(hwnd, GWLP_USERDATA);
      if (OldOwStyle == style) {
        // the data were saved for the correct style!!!
        LONG_PTR RTOffset =
            GetWindowLongPtr(hwnd, 0);  // get Right and Top offset
        SetWindowPos(hwnd, nullptr, rc.right - LOWORDPTR(RTOffset),
                     rc.top + HIWORDPTR(RTOffset), 0, 0,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                         SWP_NOSIZE | SWP_NOSENDCHANGING);
        return 0;  // DONE!
      }

      // Calculate offsets, sets window position and save data
      // to the GWLP_USERDATA stuff!
      UINT dpi = GetDpiForWindow(hwnd);  // Use pin's hwnd
      int CapButtonWidth = 0, PinW = 0, PinH = 0;
      PinW = GetSystemMetricsForDpi(SM_CXSIZE, dpi);  // Caption button width
      PinH = GetSystemMetricsForDpi(SM_CYSIZE, dpi);  // Caption button height

      int bdx = 0, bdy = 0;
      if (style & WS_THICKFRAME) {
        bdx = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi);
        bdy = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi);
      } else if (style & WS_CAPTION) {  // Caption or border
        bdx = GetSystemMetricsForDpi(SM_CXFIXEDFRAME, dpi);
        bdy = GetSystemMetricsForDpi(SM_CYFIXEDFRAME, dpi);
      }
      // Vista/7/8.x/10 extra padding Not working???
      // bdy += GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);

      RECT btrc;
      if (GetCaptionButtonsRect(ow, &btrc)) {
        CapButtonWidth = btrc.right - btrc.left;
        // bdx = rc.right - btrc.right;
      } else {
        UCHAR btnum = 0;  // Number of caption buttons.
        if ((style & (WS_SYSMENU | WS_CAPTION)) == (WS_SYSMENU | WS_CAPTION)) {
          btnum++;  // WS_SYSMENU => Close button [X]
          btnum +=
              2 * !!(style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX));  // [O] [_]
          btnum += ((xstyle & WS_EX_CONTEXTHELP)                  // [?]
                    && (style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) !=
                           (WS_MINIMIZEBOX | WS_MAXIMIZEBOX));
        }
        CapButtonWidth = btnum * PinW;
      }
      // Adjust PinW and PinH to have nice stuff.
      PinW -= 2;
      PinH -= 2;
      SetWindowLongPtr(hwnd, GWLP_USERDATA, style);  // Save old window style
      int rightoffset = CapButtonWidth + PinW + bdx +
                        GetSystemMetricsForDpi(SM_CXBORDER, dpi) * 2;
      int topoffset = bdy + 1;
      // Cache local hwnd storage for pin win offsets in the first LONG_PTR of
      // cbWndExtra
      SetWindowLongPtr(hwnd, 0, MAKELONGPTR(rightoffset, topoffset));
      // Move and size the window...
      SetWindowPos(hwnd, nullptr, rc.right - rightoffset, rc.top + topoffset,
                   PinW, PinH,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                       SWP_NOSENDCHANGING);
      return 0;
    } break;
    case WM_PAINT: {
      wchar_t Topchar = HIBYTE(HIWORD(conf.PinColor & 0xFF000000));
      if (Topchar) {
        if (!GetUpdateRect(hwnd, nullptr, FALSE)) return 0;
        PAINTSTRUCT ps;
        RECT cr;
        GetClientRect(hwnd, &cr);
        BeginPaint(hwnd, &ps);
        SetBkMode(ps.hdc, TRANSPARENT);
        DrawText(ps.hdc, &Topchar, 1, &cr,
                 DT_VCENTER | DT_CENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
        return 0;
      }
    } break;
    case WM_LBUTTONUP: {
      DestroyWindow(hwnd);
      return 0;
    } break;
    case WM_RBUTTONUP: {
      state.mdiclient = nullptr;  // In case...
      TrackMenuOfWindows(EnumTopMostWindows, 0);
      return 0;
    } break;
      //    case WM_GETPINNEDHWND: {
      //        // Returns the handle to the topmost window
      //        HWND ow = GetWindow(hwnd, GW_OWNER);
      //        if(ow && IsWindow(ow)) return (LRESULT)ow;
      //    } break;
    case WM_DESTROY: {
      KillTimer(hwnd, 1);

      // TODO: Event Hook
      // Remove topmost flag if the pin gets destroyed.
      HWND ow = nullptr;
      if ((ow = GetWindow(hwnd, GW_OWNER)) && IsWindow(ow) &&
          (GetWindowLongPtr(ow, GWL_EXSTYLE) & WS_EX_TOPMOST))
        SetWindowLevel(ow, HWND_NOTOPMOST);
    } break;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}
static HWND CreatePinWindow(const HWND owner) {
  WNDCLASS wnd;
  DWORD ex_flags = 0;
  HWND ret = nullptr;
  if (!GetClassInfo(hinstDLL, APP_NAME L"-Pin", &wnd)) {
    // Register the class if no already created.
    ZeroObject(wnd);
    wnd.style = CS_NOCLOSE | CS_HREDRAW | CS_VREDRAW;
    wnd.lpfnWndProc = PinWindowProc;
    wnd.cbWndExtra = sizeof(LONG_PTR);
    wnd.hInstance = hinstDLL;
    wnd.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wnd.hbrBackground = CreateSolidBrush(conf.PinColor & 0x00FFFFFF);
    wnd.lpszClassName = APP_NAME L"-Pin";
    RegisterClass(&wnd);
  }

  ex_flags = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
  const auto create_pin_window = [&]() {
    return CreateWindowEx(ex_flags, APP_NAME L"-Pin", nullptr,
                          WS_POPUP | WS_BORDER /* Start invisible */, 0, 0, 0,
                          0, owner, nullptr, hinstDLL, nullptr);
  };
  ret = create_pin_window();
  if (!ret && (ex_flags & WS_EX_NOACTIVATE)) {
    // Some Windows versions cannot handle the WS_EX_NOACTIVATE flag...
    ex_flags &= ~WS_EX_NOACTIVATE;
    ret = create_pin_window();
  }
  // Show pin window without activating it to avoid focus loss.
  ShowWindow(ret, SW_SHOWNA);
  return ret;
}
BOOL CALLBACK PostPinWindowsProcMessage(HWND hwnd, LPARAM lp) {
  if (IsClassName(hwnd, APP_NAME L"-Pin")) {
    if (lp == WM_CLOSE)
      DestroyWindow(hwnd);
    else
      PostMessage(hwnd, lp, 0, 0);
  }

  return TRUE;  // Next hwnd
}
static BOOL CALLBACK FindPinWinProc(HWND hwnd, LPARAM lp) {
  HWND* inouth = reinterpret_cast<HWND*>(lp);
  if (GetWindow(hwnd, GW_OWNER) == inouth[1]) {
    inouth[0] = hwnd;
    return FALSE;
  }
  return TRUE;
}
HWND GetPinWindow(HWND owner) {
  HWND inouth[2] = {nullptr, owner};
  EnumThreadWindows(GetCurrentThreadId(), FindPinWinProc,
                    reinterpret_cast<LPARAM>(inouth));
  return inouth[0];
}

void TogglesAlwaysOnTop(HWND hwnd) {
  // Use the Root owner
  hwnd = GetRootOwner(hwnd);
  LONG_PTR topmost = GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST;
  SetWindowLevel(hwnd, topmost ? HWND_NOTOPMOST : HWND_TOPMOST);

  if (!topmost) {
    if (conf.TopmostIndicator) CreatePinWindow(hwnd);
  } else {
    // Destroy Pin Window?
    if (conf.TopmostIndicator) DestroyWindow(GetPinWindow(hwnd));
  }
  // Always set foreground #442
  ReallySetForegroundWindow(hwnd);
}

}  // namespace altsnap::hooks
