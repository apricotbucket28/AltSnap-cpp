// trans window (split out of hooks.cc).

#include "hooks/overlay/trans_window.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "common/ini_section.h"
#include "hooks/blacklist.h"
#include "hooks/hook_config.h"
#include "hooks/hooks_internal.h"
#include "hooks/input_state.h"
#include "hooks/window/snap.h"
#include "hooks/window/window_move.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

void HideCursor() {
  // Reduce the size to 0 to avoid redrawing.
  if (!IsWindowVisible(g_mainhwnd)) return;
  SetWindowPos(g_mainhwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_DEFERERASE);
  ShowWindow(g_mainhwnd, SW_HIDE);
}

// Do not reclip the cursor if it is already clipped
// Do not unclip the cursor if it was not clipped by AltDrag.
void ClipCursorOnce(const RECT* clip) {
  static char trapped = 0;
  if (trapped && !clip) {
    ClipCursor(nullptr);
    trapped = 0;
  } else if (!trapped && clip) {
    ClipCursor(clip);
    trapped = 1;
  }
}

void RestrictCursorToMon() {
  // Restrict pt within origin monitor if Ctrl is being pressed
  if (IsCtrlDown()) {
    static HMONITOR origMonitor;
    static RECT fmon;
    if (origMonitor != state.origin.monitor || !state.origin.monitor) {
      origMonitor = state.origin.monitor;
      MONITORINFO mi;
      mi.cbSize = sizeof(mi);
      GetMonitorInfo(state.origin.monitor, &mi);
      CopyRect(&fmon, &mi.rcMonitor);
      fmon.left++;
      fmon.top++;
      fmon.right--;
      fmon.bottom--;
    }
    RECT clip;
    if (state.mdiclient) {
      CopyRect(&clip, &state.origin.mon);
      OffsetRect(&clip, state.mdipt.x, state.mdipt.y);
    } else {
      CopyRect(&clip, &fmon);
    }
    ClipCursorOnce(&clip);
  }
}

// Transparent window
// We use 4 thin windows to simulate a hollow window because the
// SetWindowRgn() function is very slow and would have to be called at every
// Mouse frame when resizing.
void ShowTransWin(int nCmdShow) {
  if (conf.TransWinOpacity) {
    if (g_transhwnd[0]) ShowWindow(g_transhwnd[0], nCmdShow);
  } else {
    for (auto& i : g_transhwnd)
      if (i) ShowWindow(i, nCmdShow);
  }
}
void HideTransWin() {
  ShowTransWin(SW_HIDE);
}
BOOL IsTransWinVisible() {
  return IsVisible(g_transhwnd[0]);
}

static void MoveTransWinRaw(int x, int y, int w, int h) {
  constexpr UINT f =
      SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING;
  if (conf.TransWinOpacity) {
    SetWindowPos(g_transhwnd[0], nullptr, x, y, w, h, f);
  } else {
    SetWindowPos(g_transhwnd[0], nullptr, x, y, w, 4, f);
    SetWindowPos(g_transhwnd[1], nullptr, x, y, 4, h, f);
    SetWindowPos(g_transhwnd[2], nullptr, x, y + h - 4, w, 4, f);
    SetWindowPos(g_transhwnd[3], nullptr, x + w - 4, y, 4, h, f);
  }
  //      if(hwndSS) EndDeferWindowPos(hwndSS);
}

void MoveTransWin(struct windowRR* lw) {
  LetWindowKickBack(lw->hwnd, lw);

  RECT bd;
  FixDWMRectLL(lw->hwnd, &bd, 0);
  int x = lw->x + state.mdipt.x + bd.left;
  int y = lw->y + state.mdipt.y + bd.top;
  int w = lw->width - bd.left - bd.right;
  int h = lw->height - bd.top - bd.bottom;

  if (state.origin.dpi) {
    POINT pt = {x + w / 2, y + h / 2};
    HMONITOR hmon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (hmon != state.origin.monitor && !GetRestoreFlag(lw->hwnd)) {
      UINT ptdpi = 0, dpiy_ignore = 0;
      if (S_OK == GetDpiForMonitor(hmon, MDT_DEFAULT, &ptdpi, &dpiy_ignore) &&
          ptdpi) {
        w = MulDiv(w, ptdpi, state.origin.dpi);
        h = MulDiv(h, ptdpi, state.origin.dpi);
      }
    }
  }
  MoveTransWinRaw(x, y, w, h);
}

void RestrictToCurentMonitor() {
  if (state.action.ac || state.alt) {
    POINT pt;
    GetCursorPos(&pt);
    state.origin.maximized = 0;  // To prevent auto-remax on Ctrl
    state.origin.monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
  }
}

static HCURSOR CursorToDraw() {
  HCURSOR cursor = nullptr;

  if (conf.UseCursor == 3) {
    return LoadCursor(nullptr, IDC_ARROW);
  }
  if (state.action.ac == AC_MOVE) {
    if (conf.UseCursor == 4) return LoadCursor(nullptr, IDC_SIZEALL);
    cursor = LoadCursor(nullptr, conf.UseCursor > 1 ? IDC_ARROW : IDC_HAND);
    if (!cursor) cursor = LoadCursor(nullptr, IDC_ARROW);  // Fallback;
    return cursor;
  }

  if ((state.resize.y == RZ_TOP && state.resize.x == RZ_LEFT) ||
      (state.resize.y == RZ_BOTTOM && state.resize.x == RZ_RIGHT)) {
    return LoadCursor(nullptr, IDC_SIZENWSE);
  }
  if ((state.resize.y == RZ_TOP && state.resize.x == RZ_RIGHT) ||
      (state.resize.y == RZ_BOTTOM && state.resize.x == RZ_LEFT)) {
    return LoadCursor(nullptr, IDC_SIZENESW);
  } else if ((state.resize.y == RZ_TOP && state.resize.x == RZ_XCENTER) ||
             (state.resize.y == RZ_BOTTOM && state.resize.x == RZ_XCENTER)) {
    return LoadCursor(nullptr, IDC_SIZENS);
  } else if ((state.resize.y == RZ_YCENTER && state.resize.x == RZ_LEFT) ||
             (state.resize.y == RZ_YCENTER && state.resize.x == RZ_RIGHT)) {
    return LoadCursor(nullptr, IDC_SIZEWE);
  } else {
    return LoadCursor(nullptr, IDC_SIZEALL);
  }
}
void UpdateCursor(POINT pt) {
  // Update cursor
  if (conf.UseCursor && g_mainhwnd) {
    SetClassLongPtr(g_mainhwnd, GCLP_HCURSOR,
                    reinterpret_cast<LONG_PTR>(CursorToDraw()));
    ShowWindow(g_mainhwnd, SW_SHOWNA);
    SetWindowPos(
        g_mainhwnd, HWND_TOPMOST, pt.x - 128, pt.y - 128, 256, 256,
        SWP_NOACTIVATE | SWP_NOREDRAW | SWP_DEFERERASE | SWP_NOSENDCHANGING);
  }
}

bool CreateTransWin(const IniSection& section) {
  // Read the color for the TransWin from ini file
  const COLORREF color =
      ReadColor(section, "FrameColor", L"80 00 80", ColorChannels::kRgb);
  WNDCLASS wnd;
  ZeroObject(wnd);
  //    wnd.style = CS_SAVEBITS;
  wnd.lpfnWndProc = DefWindowProc;
  wnd.hInstance = hinstDLL;
  wnd.hbrBackground = CreateSolidBrush(color);
  wnd.lpszClassName = APP_NAME L"-Trans";
  if (!wnd.hbrBackground || !RegisterClass(&wnd)) {
    if (wnd.hbrBackground) DeleteObject(wnd.hbrBackground);
    return false;
  }
  g_transhwnd[0] = nullptr;
  if (conf.TransWinOpacity) {
    int xflags = conf.TransWinOpacity == 255
                     ? WS_EX_TOPMOST | WS_EX_TOOLWINDOW
                     : WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED;
    g_transhwnd[0] =
        CreateWindowEx(xflags, wnd.lpszClassName, nullptr, WS_POPUP, 0, 0, 0, 0,
                       nullptr, nullptr, hinstDLL, nullptr);
    if (g_transhwnd[0] && conf.TransWinOpacity != 255 &&
        !SetLayeredWindowAttributes(g_transhwnd[0], 0, conf.TransWinOpacity,
                                    LWA_ALPHA))
      return false;
    if (g_transhwnd[0]) return true;
  }
  if (!g_transhwnd[0]) {
    for (auto& i :
         g_transhwnd) {  // the transparent window is made with 4 thin windows
      i = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wnd.lpszClassName,
                         nullptr, WS_POPUP, 0, 0, 0, 0, g_mainhwnd, nullptr,
                         hinstDLL, nullptr);
      LOG("CreateWindowEx[i] = %lX",
          static_cast<DWORD>(reinterpret_cast<DorQWORD>(i)));
    }
  }
  return std::ranges::all_of(g_transhwnd,
                             [](HWND window) { return window != nullptr; });
}

}  // namespace altsnap::hooks
