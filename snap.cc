/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * (C) Raymond GILLIBERT                                                 *
 * Functions to handle Snap/restore informations AltSnap                 *
 * Snapping informations set with Set/GetProp.                          *
 * Window database is used as fallback.                                  *
 * General Public License Version 3 or later (Free Software Foundation)  *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "snap.h"

#include "hooks_internal.h"

namespace altsnap::hooks {

/////////////////////////////////////////////////////////////////////////////
enum { NUMWNDDB = 16 };
struct wnddata {
  unsigned restore;
  HWND hwnd;
  int width;
  int height;
  //    int rolledh;
  //    UINT odpi
};
static struct wnddata wnddb[NUMWNDDB];

/////////////////////////////////////////////////////////////////////////////
// Database functions: used as fallback if SetProp fails

// Zero-out the database to be called in Load()
void ResetDB() {
  int i = 0;
  for (i = 0; i < NUMWNDDB; i++) {
    wnddb[i].hwnd = nullptr;
    wnddb[i].restore = 0;
  }
}
// Return the entry if it was already in db. Or NULL otherwise
[[nodiscard]] static int GetWindowInDB(HWND hwndd) {
  // Check if window is already in the wnddb database
  // And set it in the current state
  unsigned i = 0;
  for (i = 0; i < NUMWNDDB; i++) {
    if (wnddb[i].hwnd == hwndd) {
      return i;
    }
  }
  return -1;
}
static void AddWindowToDB(HWND hwndd, int width, int height, unsigned flag) {
  int idx = GetWindowInDB(hwndd);

  // Find a nice place in wnddb if not already present
  if (idx < 0) {
    int i = 0;
    for (i = 0; i < NUMWNDDB && wnddb[i].hwnd; i++);
    if (i >= NUMWNDDB) return;
    idx = i;
  }
  wnddb[idx].hwnd = hwndd;
  wnddb[idx].width = width;
  wnddb[idx].height = height;
  wnddb[idx].restore = flag;
}
static void DelWindowFromDB(HWND hwnd) {
  unsigned i = 0;
  for (i = 0; i < NUMWNDDB; i++) {
    if (wnddb[i].hwnd == hwnd) {
      wnddb[i].hwnd = nullptr;
      wnddb[i].restore = 0;
    }
  }
}
/////////////////////////////////////////////////////////////////////////////
// Windows restore data are saved in properties
// In 32b mode width and height are shrunk to 16b WORDS
// In 64b mode width and height are stored on 32b DWORDS
// I 64b mode we also check for FancyZone info that are stored the same way!
// There is a lot of cast because of annoying warnings...
// There is also a fallback to a database for some spetial windows.
unsigned GetRestoreData(HWND hwnd, int* width, int* height) {
  auto WH = reinterpret_cast<DorQWORD>(GetProp(hwnd, APP_PROPPT));
  if (WH) {
    *width = static_cast<int>(LOWORDPTR(WH));
    *height = static_cast<int>(HIWORDPTR(WH));
    UINT dpi = GetDpiForWindow(hwnd);
    UINT odpi = 0;
    if (dpi &&
        (odpi = static_cast<UINT>(
             reinterpret_cast<DorQWORD>(GetProp(hwnd, APP_PRODPI)))) &&
        odpi != dpi) {
      // The current dpi is different for the window, We must scale the content.
      *width = MulDiv(*width, dpi, odpi);
      *height = MulDiv(*height, dpi, odpi);
    }
    return static_cast<unsigned>(
        reinterpret_cast<DorQWORD>(GetProp(hwnd, APP_PROPFL)));
    ;

#ifdef _WIN64  // Try fancy zone flag only in 64bit mode!
  } else if (conf.FancyZone &&
             (WH = reinterpret_cast<DorQWORD>(GetProp(hwnd, FZ_PROPPT)))) {
    // It seems FancyZones stores size info in points, not pixels.
    *width = static_cast<int>(LOWORDPTR(WH));
    *height = static_cast<int>(HIWORDPTR(WH));
    if (conf.FancyZone != 2) {
      int dpi = GetDpiForWindow(hwnd);
      if (dpi) {
        // Scale bcak to current dpi from 96
        *width = MulDiv(*width, dpi, 96);
        *height = MulDiv(*height, dpi, 96);
      }
    }
    return SNAPPED | SNZONE;
#endif
  } else {  // fallback to database
    int idx = GetWindowInDB(hwnd);
    if (idx >= 0) {
      *width = wnddb[idx].width;
      *height = wnddb[idx].height;
      return wnddb[idx].restore;
    }
  }
  // Unable to find the window in the db
  *width = *height = 0;
  return 0;
}
void ClearRestoreData(HWND hwnd) {
  RemoveProp(hwnd, APP_PROPPT);
  RemoveProp(hwnd, APP_PROPFL);
  // RemoveProp(hwnd, APP_PROPOFFSET);
#ifdef _WIN64
  if (conf.FancyZone) {
    RemoveProp(hwnd, FZ_PROPPT);
    RemoveProp(hwnd, FZ_PROPZONES);
  }
#endif
  DelWindowFromDB(hwnd);
}
// Sets width, height and restore flag in a hwnd
void SetRestoreData(HWND hwnd, int width, int height, unsigned restore) {
  BOOL ret = 0;
  ret = SetProp(hwnd, APP_PROPFL,
                reinterpret_cast<HANDLE>(static_cast<DorQWORD>(restore)));
  ret &= SetProp(hwnd, APP_PROPPT,
                 reinterpret_cast<HANDLE>(MAKELONGPTR(width, height)));
  UINT dpi = 0;
  if ((dpi = GetDpiForWindow(hwnd)))
    ret &= SetProp(hwnd, APP_PRODPI,
                   reinterpret_cast<HANDLE>(static_cast<DorQWORD>(dpi)));

  if (!ret) AddWindowToDB(hwnd, width, height, restore);
}

unsigned GetRestoreFlag(HWND hwnd) {
  unsigned flag = reinterpret_cast<DorQWORD>(GetProp(hwnd, APP_PROPFL));

#ifdef _WIN64
  if (conf.FancyZone && GetProp(hwnd, FZ_PROPPT)) flag |= SNAPPED | SNZONE;
#endif

  int idx = 0;  // fallback to db.
  if (!flag && ((idx = GetWindowInDB(hwnd)) >= 0)) {
    return wnddb[idx].restore;
  }
  return flag;
}
void SetRestoreFlag(HWND hwnd, unsigned flag) {
  BOOL ret = SetProp(hwnd, APP_PROPFL,
                     reinterpret_cast<HANDLE>(static_cast<DorQWORD>(flag)));
  int idx = 0;
  if (!ret && ((idx = GetWindowInDB(hwnd)) >= 0)) {
    wnddb[idx].restore = flag;
  }
}

int IsAnySnapped(HWND hwnd) {
  // Any kind of snapping, including maximization.
  // In short whenever the window will be restored.
  unsigned flg = 0;
  return IsZoomed(hwnd) ||
         (!(conf.SmartAero & 4) && SNAPPED & (flg = GetRestoreFlag(hwnd)) &&
          !(flg & SNCLEAR)) ||
         IsWindowSnapped(hwnd);
}
/////////////////////////////////////////////////////////////////////////////
// borderless flag (saving old GWL_STYLE)
void SetBorderlessFlag(HWND hwnd, LONG_PTR flag) {
  SetProp(hwnd, APP_PRBDLESS, reinterpret_cast<HANDLE>(flag));
}
LONG_PTR GetBorderlessFlag(HWND hwnd) {
  return reinterpret_cast<LONG_PTR>(GetProp(hwnd, APP_PRBDLESS));
}
LONG_PTR ClearBorderlessFlag(HWND hwnd) {
  return reinterpret_cast<LONG_PTR>(RemoveProp(hwnd, APP_PRBDLESS));
}

///////////////////////////////////////////////////////////////////////////////
//// Roll unroll stuff
// static int GetRolledHeight(HWND hwnd)
//{
//     int ret = (int)GetProp(hwnd, APP_ROLLED);
//     int idx;
//     if (!ret && ((idx = GetWindowInDB(hwnd)) >=0)) {
//         ret = wnddb[idx].rolledh;
//     }
//     return ret;
// }
// static int ClearRolledHeight(HWND hwnd)
//{
//     int ret = (int)RemoveProp(hwnd, APP_ROLLED);
//
//     int idx;
//     if (!ret && ((idx = GetWindowInDB(hwnd)) >=0)) {
//         ret = wnddb[idx].rolledh;
//         wnddb[idx].rolledh = 0;
//     }
//     return ret;
// }
// static void SetRolledHeight(HWND hwnd, int rolledh)
//{
//     BOOL ret = SetProp(hwnd, APP_ROLLED, (HANDLE)(DorQWORD)rolledh);
//     if (ret) return;
//
//     int idx;
//     if (((idx = GetWindowInDB(hwnd)) >=0)) {
//         wnddb[idx].rolledh = rolledh;
//     } else {
//         int i;
//         for (i=0; i < NUMWNDDB && wnddb[i].hwnd; i++);
//         if (i >= NUMWNDDB) return;
//         idx = i;
//         wnddb[idx].hwnd = hwnd;
//         wnddb[idx].rolledh = rolledh;
//         wnddb[idx].restore = 0;
//     }
//
// }
// static void AddToFlag(HWND hwnd, unsigned flag)
//{
//     SetRestoreFlag(hwnd, flag|GetRestoreFlag(hwnd));
// }
//
// static void RemoveToFlag(HWND hwnd, unsigned flag)
//{
//     SetRestoreFlag(hwnd, flag & (~GetRestoreFlag(hwnd)));
// }

}  // namespace altsnap::hooks
