#ifndef ALTDRAG_RPC_H
#define ALTDRAG_RPC_H

// clang-format off
#include <windows.h>
#include <commctrl.h>  // Must come after windows.h.
// clang-format on

#include "common/winutil.h"

// MainWindow commands
enum {
  CMD_TOGGLE = (401),
  CMD_HIDE = (402),
  CMD_ELEVATE = (403),
  CMD_CONFIG = (404),
  CMD_ABOUT = (405),
  CMD_EXIT = (406),
  CMD_FIND = (407),
  CMD_HELP = (408),
  CMD_SAVEZONES = (409),
  CMD_TESTWIN = (410),
  CMD_OPENINIFILE = (411),
  CMD_SNAPLAYOUT = (412),
  CMD_SNAPLAYOUTEND = (422),
  CMD_EDITLAYOUT = (430),
  CMD_CLOSEZONES = (431)
};

#ifndef LOW_LEVEL_KB_PROC
#ifndef DECORATED_HOOKS_DLL_PROCS
#define LOW_LEVEL_KB_PROC "LowLevelKeyboardProc"
#define LOAD_PROC "Load"
#define UNLOAD_PROC "Unload"
#else
#define LOW_LEVEL_KB_PROC "_LowLevelKeyboardProc@12"
#define LOAD_PROC "_Load@8"
#define UNLOAD_PROC "_Unload@0"
#endif
#endif

// Extra messages for Action Menu
enum {
  LP_CURSORPOS = (1 << 0),
  LP_TOPMOST = (1 << 1),
  LP_BORDERLESS = (1 << 2),
  LP_MAXIMIZED = (1 << 3),
  LP_ROLLED = (1 << 4),
  LP_MOVEONOFF = (1 << 5),
  LP_NOALTACTION = (1 << 6)
};

// App
#define APP_NAME L"AltSnap"
#define APP_NAMEA "AltSnap"
#define APP_PROPPT APP_NAME L"-RDim"
#define APP_PROPFL APP_NAME L"-RFlag"
#define APP_PROPOFFSET APP_NAME L"-ROffset"
#define APP_PRBDLESS APP_NAME L"-RStyle"
#define APP_ROLLED APP_NAME L"-Rolled"
#define APP_ASONOFF APP_NAME L"-ASOnOff"
#define APP_MOVEONOFF APP_NAME L"-MoveOnOff"
#define APP_PRODPI APP_NAME L"-ODPI"
#define FZ_PROPPT L"FancyZones_RestoreSize"
#define FZ_PROPZONES L"FancyZones_zones"

// User Messages
#define WM_TRAY (WM_USER + 2)
#define WM_SCLICK (WM_USER + 3)
#define WM_UPDCFRACTION (WM_USER + 4)
#define WM_UPDATETRAY (WM_USER + 5)
#define WM_OPENCONFIG (WM_USER + 6)
#define WM_CLOSECONFIG (WM_USER + 7)
#define WM_UPDATESETTINGS (WM_USER + 8)
#define WM_ADDTRAY (WM_USER + 9)
#define WM_HIDETRAY (WM_USER + 10)
#define WM_UNIKEYMENU (WM_USER + 11)
#define WM_GETCLICKHWND (WM_USER + 12)
#define WM_STACKLIST (WM_USER + 13)
#define WM_FINISHMOVEMENT (WM_USER + 14)
#define WM_CLOSEMODE (WM_USER + 15)
#define WM_SETLAYOUTNUM (WM_USER + 16)
#define WM_GETLAYOUTREZ (WM_USER + 17)
#define WM_GETBESTLAYOUT (WM_USER + 18)
// #define WM_GETZONESLEN    (WM_USER+19)
#define WM_GETZONES (WM_USER + 20)

// List of possible actions
// ACVALUE(AC_ENUM, IniString, Info)
#define ACTION_MAP                                 \
  ACVALUE(AC_NONE = 0, Nothing, 00)                \
  ACVALUE(AC_MOVE, Move, MR)                       \
  ACVALUE(AC_RESIZE, Resize, MR)                   \
  ACVALUE(AC_RESTORE, Restore, MR)                 \
  ACVALUE(AC_MENU, Menu, CL)                       \
  ACVALUE(AC_MINIMIZE, Minimize, MR)               \
  ACVALUE(AC_MAXIMIZE, Maximize, MR)               \
  ACVALUE(AC_CENTER, Center, MR)                   \
  ACVALUE(AC_CENTER2, Center2, MR)                 \
  ACVALUE(AC_ALWAYSONTOP, AlwaysOnTop, ZO)         \
  ACVALUE(AC_CLOSE, Close, CL)                     \
  ACVALUE(AC_LOWER, Lower, ZO)                     \
  ACVALUE(AC_FOCUS, Focus, ZO)                     \
  ACVALUE(AC_BORDERLESS, Borderless, 00)           \
  ACVALUE(AC_KILL, Kill, CL)                       \
  ACVALUE(AC_PAUSE, Pause, CL)                     \
  ACVALUE(AC_RESUME, Resume, CL)                   \
  ACVALUE(AC_MAXHV, MaximizeHV, MR)                \
  ACVALUE(AC_MINALL, MinAllOther, 00)              \
  ACVALUE(AC_MUTE, Mute, 00)                       \
  ACVALUE(AC_SIDESNAP, SideSnap, MR)               \
  ACVALUE(AC_EXTENDSNAP, ExtendSnap, MR)           \
  ACVALUE(AC_EXTENDTNEDGE, ExtendTNEdge, MR)       \
  ACVALUE(AC_MOVETNEDGE, MoveTNEdge, MV)           \
  ACVALUE(AC_NSTACKED, NStacked, ZO)               \
  ACVALUE(AC_NSTACKED2, NStacked2, ZO)             \
  ACVALUE(AC_PSTACKED, PStacked, ZO)               \
  ACVALUE(AC_PSTACKED2, PStacked2, ZO)             \
  ACVALUE(AC_STACKLIST, StackList, CL)             \
  ACVALUE(AC_STACKLIST2, StackList2, CL)           \
  ACVALUE(AC_ALTTABLIST, AltTabList, CL)           \
  ACVALUE(AC_ALTTABFULLLIST, AltTabFullList, CL)   \
  ACVALUE(AC_ASONOFF, ASOnOff, CL)                 \
  ACVALUE(AC_MOVEONOFF, MoveOnOff, CL)             \
                                                   \
  ACVALUE(AC_MZONE, MZone, MR)                     \
  ACVALUE(AC_XZONE, XZone, MR)                     \
                                                   \
  ACVALUE(AC_STEP, Step, MR)                       \
                                                   \
  ACVALUE(AC_NLAYOUT, NLayout, 00)                 \
  ACVALUE(AC_PLAYOUT, PLayout, 00)                 \
  ACVALUE(AC_SHRT, Shrt, 00)                       \
                                                   \
  ACVALUE(AC_NPLAYOUT, NPLayout, 00)               \
  ACVALUE(AC_ROLL, Roll, MR)                       \
  ACVALUE(AC_ALTTAB, AltTab, ZO)                   \
  ACVALUE(AC_VOLUME, Volume, 00)                   \
  /* ACVALUE(AC_BRIGHTNESS,   Brightness,   00) */ \
  ACVALUE(AC_TRANSPARENCY, Transparency, 00)       \
  ACVALUE(AC_HSCROLL, HScroll, 00)                 \
  ACVALUE(AC_ZOOM, Zoom, MR)                       \
  ACVALUE(AC_ZOOM2, Zoom2, MR)                     \
  ACVALUE(AC_NPSTACKED, NPStacked, ZO)             \
  ACVALUE(AC_NPSTACKED2, NPStacked2, ZO)

#define ACVALUE(a, b, c) a,
enum action { ACTION_MAP AC_MAXVALUE, AC_ORICLICK };
#undef ACVALUE

struct rgTagAction {
  BYTE ac;
  BYTE fl;
  short wp;
};
using action_t = rgTagAction;

inline constexpr action_t k_action_none = {AC_NONE, 0, 0};
inline constexpr action_t k_action_move = {AC_MOVE, 0, 0};

#define PACK_ACTION(x) \
  ((LPARAM)(x.ac << 24) | (LPARAM)(x.fl << 16) | (LPARAM)(x.wp << 0))
#define UNPACK_ACTION(dst, lp)  \
  do {                          \
    dst.ac = (lp >> 24) & 0xFF; \
    dst.fl = (lp >> 16) & 0xFF; \
    dst.wp = lp & 0xFFFF;       \
  } while (0)

// List of extra info options
enum {
  ACINFO_MOVE = 1,
  ACINFO_RESIZE = 2,
  ACINFO_ZORDER = 4,
  ACINFO_CLOSE = 8,
};

// Helper function to get extra action info
[[nodiscard]] inline UCHAR ActionInfo(action_t action) {
  enum {
    MV = ACINFO_MOVE,
    RZ = ACINFO_RESIZE,
    ZO = ACINFO_ZORDER,
    CL = ACINFO_CLOSE,
    MR = ACINFO_MOVE | ACINFO_RESIZE,
  };
#define ACVALUE(a, b, c) (c),
  static const UCHAR action_info[] = {ACTION_MAP};

#undef ACVALUE
  return action_info[action.ac];
}

enum {
  ACFL_ZERO = 0,
  ACFL_LEFT = 1,
  ACFL_UP = 2,
  ACFL_RIGHT = 3,
  ACFL_DOWN = 4,
  ACFL_SET = 5,
  ACFL_TOGGLE = 6,
};

#define MOUVEMENT(action) (action.ac <= AC_RESIZE)

///////////////////////////////////////////////////////////////////////////
// Check if key is assigned in the HKlist
[[nodiscard]] inline int IsHotkeyy(unsigned char key,
                                   const unsigned char* HKlist) {
  const UCHAR* pos = &HKlist[0];
  while (*pos) {
    if (key == *pos) {
      return 1;
    }
    pos++;
  }
  return 0;
}
[[nodiscard]] inline int IsActionInList(const enum action ac,
                                        const enum action* aclst) {
  do {
    if (ac == *aclst) return 1;
  } while (*aclst++ != AC_NONE);

  return 0;
}
// Convert zone number to ini name entry
inline wchar_t* ZidxToZonestr(int laynum, int idx, wchar_t zname[32]) {
  if (laynum > 9) return nullptr;
  wchar_t txt[kUintDigits + 1];
  zname[0] = !laynum ? L'\0' : L'A' + laynum - 1;
  zname[1] = '\0';
  lstrcat_s(zname, 32, L"Zone");
  lstrcat_s(zname, 32, Uint2lStr(txt, idx));  // Zone Name from zone number

  return zname;
}
inline char* ZidxToZonestrA(int laynum, int idx, char zname[32]) {
  if (laynum > 9) return nullptr;
  char txt[16];
  zname[0] = !laynum ? '\0' : 'A' + laynum - 1;
  zname[1] = '\0';
  lstrcat_s(zname, 32, "Zone");
  lstrcat_s(zname, 32, Uint2lStr(txt, idx));  // Zone Name from zone number

  return zname;
}

[[nodiscard]] inline wchar_t* is_base_action(const wchar_t* a, const char* b) {
  while (*a && *a != '=' && *a != '_' && *a == *b) {
    b++;
    a++;
  }
  return (*a == '_' || *a == '=' || *a == *b) ? const_cast<wchar_t*>(a)
                                              : nullptr;
}

/* Map action string to actual action_t struct
 * Possible formats:
 * MyActionName[_XXX][_XXX_YYY]\0 ; ending with a NULL
 * MyActionName[_XXX][_XXX_YYY]=  ; Or with an equal
 */
[[nodiscard]] inline action_t MapActionW(const wchar_t* txt) {
  action_t action = {AC_NONE};
  if (!txt || !*txt) return action;
#define ACVALUE(a, b, c) (#b),
  static const char* action_map[] = {ACTION_MAP};
#undef ACVALUE
  UCHAR ac = 0;

  // ShrtX X = 0 to Z (for backward compat...)
  // It should be done with Shrt_XX.
  if (txt[0] == 'S' && txt[1] == 'h' && txt[2] == 'r' && txt[3] == 't' &&
      '0' <= txt[4] && txt[4] <= 'Z' && (txt[5] == '\0' || txt[5] == '=')) {
    wchar_t c = txt[4];
    UCHAR num = c <= '9' ? c - '0' : c - 'A' + 10;
    action.ac = AC_SHRT;
    action.fl = std::min(num, static_cast<UCHAR>(35));  // Safety clamp
    return action;
  }

  for (ac = 0; ac < std::size(action_map); ac++) {
    wchar_t* params = nullptr;
    if ((params = is_base_action(txt, action_map[ac]))) {
      BYTE flagparam = 0;
      action.ac = ac;
      if (*params == L'_' && *++params) {
        // We got 1 parameter.
        wchar_t cc = *params++;
        switch (cc) {
          // Direction flags
          case 'L':
            flagparam = ACFL_LEFT;
            break;
          case 'U':
            flagparam = ACFL_UP;
            break;
          case 'R':
            flagparam = ACFL_RIGHT;
            break;
          case 'D':
            flagparam = ACFL_DOWN;
            break;
          case 'S':
            flagparam = ACFL_SET;
            break;
          case 'T':
            flagparam = ACFL_TOGGLE;
            break;
          default:  // Parse an int
            --params;
            for (; cc != '=' && cc != '_' && cc; cc = *params) {
              flagparam = flagparam * 10 + (cc - L'0');
              ++params;
            }
        }
        action.fl = flagparam;

        if (*params == ('_') && *++params) {
          // We got 2 parameters.
          action.wp = strtoi(params);  // strtoi stops on any non digit
        }
        // LOGA("Read action %s_%d_%d",  action_map[ac], (int)action.fl,
        // (int)action.wp);
      }
      // We got the action!
      return action;
    }
  }
  return action;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * */
static_assert(AC_ORICLICK <= 127,
              "Maximal action number should be 127 or less!");

#endif /* ALTDRAG_RPC_H */
