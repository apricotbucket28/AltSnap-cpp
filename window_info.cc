// window info (split out of hooks.cc).

#include "window_info.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "blacklist.h"
#include "hooks_internal.h"
#include "ini_section.h"
#include "snap.h"
#include "zones.h"

namespace altsnap::hooks {

/////////////////////////////////////////////////////////////////////////////
static void blacklist_cache_state() {
  state.cached_hwnd_blacklist = state.hwnd;
  state.title[0] = state.classname[0] = state.exename[0] = L'\0';
  GetWindowProgName(state.hwnd, state.exename, std::size(state.exename));
  GetWindowText(state.hwnd, state.title, std::size(state.title));
  GetClassName(state.hwnd, state.classname, std::size(state.classname));
}
[[nodiscard]] bool blacklisted(HWND hwnd, const Blacklist& list) {
  // Null hwnd or empty list
  if (!hwnd || list.empty()) return false;

  if (hwnd == state.hwnd) {
    // Magage a simple cache only for the state window
    if (state.hwnd != state.cached_hwnd_blacklist) blacklist_cache_state();
    return list.Contains(state.exename, state.title, state.classname);
  }

  wchar_t title[256], classname[256];
  wchar_t exename[MAX_PATH];
  title[0] = classname[0] = exename[0] = '\0';
  GetWindowText(hwnd, title, std::size(title));
  GetClassName(hwnd, classname, std::size(classname));
  GetWindowProgName(hwnd, exename, std::size(exename));

  // LOGA( "hwnd=%X (%ls:%ls|%ls) blacklist = %X, thread=%d", (UINT)hwnd,
  // exename, title, classname, (UINT)list, (int)GetCurrentThreadId());
  return list.Contains(exename, title, classname);
}

int isClassName(HWND hwnd, const wchar_t* str) {
  wchar_t classname[256];
  return GetClassName(hwnd, classname, std::size(classname)) &&
         !lstrcmp(classname, str);
}
/////////////////////////////////////////////////////////////////////////////
// The second bit (&2) will always correspond to the WS_THICKFRAME flag
[[nodiscard]] int IsResizable(HWND hwnd) {
  int thickf = !!(GetWindowLongPtr(hwnd, GWL_STYLE) & WS_THICKFRAME);
  int ret = conf.ResizeAll  // bit two is the real thickframe state.
            | thickf | (thickf << 1);

  if (!ret) ret = !!GetBorderlessFlag(hwnd);
  if (!ret) ret = !!blacklisted(hwnd, BlkLst.AResize);  // Always resize list

  return ret;
}
HMONITOR GetMonitorInfoFromWin(HWND hwnd, MONITORINFO* mi) {
  mi->cbSize = sizeof(*mi);
  HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  GetMonitorInfo(hmon, mi);
  return hmon;
}
/////////////////////////////////////////////////////////////////////////////
static BOOL IsFullscreen(HWND hwnd) {
  LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);

  if ((style & WS_CAPTION) != WS_CAPTION) {
    RECT rc;
    if (GetWindowRect(hwnd, &rc)) {
      MONITORINFO mi;
      GetMonitorInfoFromWin(hwnd, &mi);
      return EqualRect(&rc, &mi.rcMonitor);
    }
  }
  return FALSE;
}
int IsFullscreenF(HWND hwnd, const RECT* wnd, const RECT* fmon) {
  LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);

  // No caption and fullscreen window
  return ((style & WS_CAPTION) != WS_CAPTION) && EqualRect(wnd, fmon);
}
int IsFullScreenBL(HWND hwnd) {
  return conf.FullScreen && IsFullscreen(hwnd);
}
/////////////////////////////////////////////////////////////////////////////
// WM_ENTERSIZEMOVE or WM_EXITSIZEMOVE...
void NotifySizeMoveStaEnd(HWND hwnd, UCHAR sta) {
  // Don't send WM_ENTER/EXIT SIZEMOVE if the window is in SSizeMove BL
  if (!blacklisted(hwnd, BlkLst.SSizeMove)) {
    PostMessage(hwnd, sta ? WM_ENTERSIZEMOVE : WM_EXITSIZEMOVE, 0, 0);
  }
  // Always send the NotifyWinEvent for IAccessible interface.
  if (conf.NotifyWinEvent) {
    LOG("NotifySizeMove %s", sta ? "START" : "END");
    NotifyWinEvent(sta ? EVENT_SYSTEM_MOVESIZESTART : EVENT_SYSTEM_MOVESIZEEND,
                   hwnd, 0, 0);
  }
}

/////////////////////////////////////////////////////////////////////////////
// Overloading of the Hittest function to include a whitelist
// x and y are in screen coordinate.
int HitTestTimeoutbl(HWND hwnd, POINT pt) {
  DorQWORD area = 0;

  // Try first with the ancestor window for some buggy AppX?
  HWND ancestor = state.hwnd = GetAncestor(hwnd, GA_ROOT);
  if (blacklisted(ancestor, BlkLst.MMBLower)) return 0;
  if (hwnd != ancestor && blacklisted(ancestor, BlkLst.NCHittest)) {
    SendMessageTimeout(ancestor, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y),
                       SMTO_NORMAL, 200, &area);
  }
  if (area != HTCAPTION) area = HitTestTimeoutL(hwnd, MAKELPARAM(pt.x, pt.y));
  if (area == HTCAPTION) {
    // Double check that we are not inside one of the
    // caption buttons buttons because of buggy Win10..
    RECT buttonRc;
    if (GetCaptionButtonsRect(ancestor, &buttonRc) && PtInRect(&buttonRc, pt)) {
      // let us assume it is the minimize button, it makes no sence
      // But Windows is too buggy
      area = HTMINBUTTON;
    }
  }
  return area;
}
/////////////////////////////////////////////////////////////////////////////
//
void GetMinMaxInfo(HWND hwnd, POINT* Min, POINT* Max) {
  GetMinMaxInfoF(hwnd, Min, Max, conf.IgnoreMinMaxInfo);
}
/////////////////////////////////////////////////////////////////////////////
// Use NULL to restore old transparency.
// Set to -1 to clear old state
void SetWindowTrans(HWND hwnd) {
  static BYTE oldtrans;
  static HWND oldhwnd;
  if (conf.MoveTrans == 0 || conf.MoveTrans == 255) return;
  if (oldhwnd == hwnd) return;  // Nothing to do
  if (hwnd == reinterpret_cast<HWND>(-1)) {
    oldhwnd = nullptr;
    oldtrans = 0;
    return;
  }

  if (hwnd && !oldtrans) {
    oldhwnd = hwnd;
    LONG_PTR exstyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (exstyle & WS_EX_LAYERED) {
      BYTE ctrans = 0;
      if (GetLayeredWindowAttributes(hwnd, nullptr, &ctrans, nullptr))
        if (ctrans) oldtrans = ctrans;
    } else {
      SetWindowLongPtr(hwnd, GWL_EXSTYLE, exstyle | WS_EX_LAYERED);
      oldtrans = 255;
    }
    SetLayeredWindowAttributes(hwnd, 0, conf.MoveTrans, LWA_ALPHA);
  } else if (!hwnd && oldhwnd) {  // restore old trans;
    LONG_PTR exstyle = GetWindowLongPtr(oldhwnd, GWL_EXSTYLE);
    if (!oldtrans || oldtrans == 255) {
      SetWindowLongPtr(oldhwnd, GWL_EXSTYLE, exstyle & ~WS_EX_LAYERED);
    } else {
      SetLayeredWindowAttributes(oldhwnd, 0, oldtrans, LWA_ALPHA);
    }
    oldhwnd = nullptr;
    oldtrans = 0;
  }
}

/////////////////////////////////////////////////////////////////////////////
// Enumerate callback proc
BOOL CALLBACK EnumMonitorsProc(HMONITOR hMonitor, HDC hdcMonitor,
                               LPRECT lprcMonitor, LPARAM dwData) {
  // Add monitor
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  GetMonitorInfo(hMonitor, &mi);
  monitors.push_back(mi.rcWork);
  return TRUE;  //*lprcMonitor;
}
/////////////////////////////////////////////////////////////////////////////
void OffsetRectMDI(RECT* wnd) {
  OffsetRect(wnd, -state.mdipt.x, -state.mdipt.y);
}
int ShouldSnapTo(HWND hwnd) {
  LONG_PTR style = 0;
  return hwnd != state.hwnd && IsVisible(hwnd) && !IsIconic(hwnd) &&
         !(GetWindowLongPtr(hwnd, GWL_EXSTYLE) &
           WS_EX_NOACTIVATE)  // != WS_EX_NOACTIVATE
         && (((style = GetWindowLongPtr(hwnd, GWL_STYLE)) & WS_CAPTION) ==
                 WS_CAPTION ||
             (style & WS_THICKFRAME) ||
             GetBorderlessFlag(hwnd)  //&(WS_THICKFRAME|WS_CAPTION)
             || blacklisted(hwnd, BlkLst.Snaplist));
}
///////////////////////////////////////////////////////////////////////////
void GetMonitorRect(const POINT* pt, int full, RECT* _mon) {
  if (state.mdiclient && GetClientRect(state.mdiclient, _mon)) {
    return;  // MDI!
  }

  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  GetMonitorInfo(MonitorFromPoint(*pt, MONITOR_DEFAULTTONEAREST), &mi);

  CopyRect(_mon, full ? &mi.rcMonitor : &mi.rcWork);
}
///////////////////////////////////////////////////////////////////////////
// Get state.mdipt and mdi monitor
BOOL GetMDInfo(POINT* mdicpt, RECT* wnd) {
  mdicpt->x = mdicpt->y = 0;
  if (!GetClientRect(state.mdiclient, wnd) ||
      !ClientToScreen(state.mdiclient, mdicpt)) {
    return FALSE;
  }
  return TRUE;
}
// Determine if we should select the window through AltTab equivalents
// We do not want the desktop window nor taskbar in this list
// We want usually a window with a taskbar or that has the WS_EX_APPWINDOW
// extended flag. Another case is the windows that were made borderless.
// We include all windows that do not have the WS_EX_TOOLWINDOW exstyle
// De also exclude all windows that are in the Bottommost list.
int IsAltTabAble(HWND window) {
  LONG_PTR xstyle = 0;
  wchar_t txt[2];
  return IsVisible(window) &&
         ((xstyle = GetWindowLongPtr(window, GWL_EXSTYLE)) &
          WS_EX_NOACTIVATE) != WS_EX_NOACTIVATE &&
         (  // Has a caption or borderless or present in taskbar.
             (xstyle & WS_EX_TOOLWINDOW) !=
                 WS_EX_TOOLWINDOW  // Not a tool window
             || (GetWindowLongPtr(window, GWL_STYLE) & WS_CAPTION) ==
                    WS_CAPTION  // or has a caption
             || (xstyle & WS_EX_APPWINDOW) ==
                    WS_EX_APPWINDOW        // Or is forced in taskbar
             || GetBorderlessFlag(window)  // Or we made it borderless
             ) &&
         (conf.MenuShowEmptyLabelWin ||
          (GetWindowText(window, txt, std::size(txt)) && txt[0])) &&
         !blacklisted(window,
                      BlkLst.Bottommost);  // Exclude bottommost windows
}
int IsToolWindow(HWND hwnd) {
  return GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW;
}
////////////////////////////////////////////////////////////////////////////
// Returns the GA_ROOT window if not MDI or MDIblacklist
HWND MDIorNOT(HWND hwnd, HWND* mdiclient_) {
  HWND mdiclient = nullptr;
  HWND root = GetAncestor(hwnd, GA_ROOT);

  if (conf.MDI && !blacklisted(root, BlkLst.MDIs)) {
    while (hwnd != root) {
      HWND parent = GetParent(hwnd);
      if (!parent) return root;
      if (parent == hwnd) return hwnd;

      LONG_PTR exstyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
      if ((exstyle & WS_EX_MDICHILD)) {
        // Found MDI child, parent is now MDIClient window
        mdiclient = parent;
        break;
      }
      hwnd = parent;
    }
  } else {
    hwnd = root;
  }
  *mdiclient_ = mdiclient;
  return hwnd;
}

int IsMXRolled(HWND hwnd, const RECT* rc) {
  MONITORINFO mi;
  GetMonitorInfoFromWin(hwnd, &mi);
  // Consider the window rolled if its height less than a quarter of monitors
  return (rc->bottom - rc->top) < (mi.rcWork.bottom - mi.rcWork.top) / 4;
}
int IsDoubleClick(
    int button) {  // Never validate a double-click if the click has to pierce
  return !conf.PiercingClick && state.clickbutton == button &&
         GetTickCount() - state.clicktime <= GetDoubleClickTime();
}

[[nodiscard]] BOOL IsRectInMonitors(const RECT* rc) {
  for (auto& monitor : monitors) {
    int inx = monitor.left < rc->right - 8 && rc->left + 8 < monitor.right;
    int iny = monitor.top < rc->bottom - 8 && rc->top + 8 < monitor.bottom;
    if (inx && iny)  // Windows is inside one of the monitors.
      return TRUE;
  }
  return FALSE;
}
[[nodiscard]] int IsAeraCapbutton(int area) {
  return area == HTMINBUTTON || area == HTMAXBUTTON || area == HTCLOSE ||
         area == HTHELP;
}
[[nodiscard]] static int IsAreaAnyCap(int area) {
  return area == HTCAPTION          // Caption
         || area == HTSYSMENU       // System menu
         || IsAeraCapbutton(area);  // Caption buttons
}
[[nodiscard]] static int IsAreaTopRZ(int area) {
  return (area >= HTTOP && area <= HTTOPRIGHT);  // Top resizing border
}
int InTitlebar(POINT pt, action_t action, enum button button) {
  int willtest = ((conf.TTBActions & 1) && !state.alt) ||
                 ((conf.TTBActions & 2) && state.alt);
  if (willtest && action.ac) {
    HWND nhwnd = WindowFromPoint(pt);
    if (!nhwnd) return 0;  // Next hook!
    // HWND hwnd = MDIorNOT(nhwnd, &state.mdiclient);
    // if (blacklisted(hwnd, BlkLst.Windows)) return 0; // Next hook

    // Hittest to see if we are in a caption!
    // Only accept caption buttons as a titlebar for the buttons that are
    // Not in the blacklist (default LMB and RMB ttb actions only apply
    // to the real titlebar (hittest=2).
    assert(button - 2 < 32 && button - 2 >= 0);  // Max is 32 buttons.
    int area = HitTestTimeoutbl(nhwnd, pt);
    if (area == HTCAPTION  // Real caption
        || (!(conf.BLCapButtons & (1 << (button - 2))) && IsAreaAnyCap(area)) ||
        (!(conf.BLUpperBorder & (1 << (button - 2))) && IsAreaTopRZ(area))) {
      return area;
    }
  }
  return 0;
}
[[nodiscard]] int IsAreaLongClikcable(int area) {
  return IsAeraCapbutton(area) || area == HTHSCROLL || area == HTVSCROLL ||
         area == HTSYSMENU;
}

}  // namespace altsnap::hooks
