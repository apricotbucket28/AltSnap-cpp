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

// Names under which the exe finds the two exports of hooks.dll.
inline constexpr char kLoadProcName[] = "Load";
inline constexpr char kUnloadProcName[] = "Unload";

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
// Window properties used to remember per-window state (SetProp/GetProp).
// Upstream: kPropRestoreSize, kPropRestoreFlag, ... and
// kPropFancyZonesSize/kPropFancyZonesZones.
inline constexpr wchar_t kPropRestoreSize[] = APP_NAME L"-RDim";
inline constexpr wchar_t kPropRestoreFlag[] = APP_NAME L"-RFlag";
inline constexpr wchar_t kPropRestoreOffset[] = APP_NAME L"-ROffset";
inline constexpr wchar_t kPropBorderlessStyle[] = APP_NAME L"-RStyle";
inline constexpr wchar_t kPropRolled[] = APP_NAME L"-Rolled";
inline constexpr wchar_t kPropAutoSnapOnOff[] = APP_NAME L"-ASOnOff";
inline constexpr wchar_t kPropMoveOnOff[] = APP_NAME L"-MoveOnOff";
inline constexpr wchar_t kPropOriginalDpi[] = APP_NAME L"-ODPI";
inline constexpr wchar_t kPropFancyZonesSize[] = L"FancyZones_RestoreSize";
inline constexpr wchar_t kPropFancyZonesZones[] = L"FancyZones_zones";

// User Messages
inline constexpr UINT WM_TRAY = WM_USER + 2;
inline constexpr UINT WM_SCLICK = WM_USER + 3;
inline constexpr UINT WM_UPDCFRACTION = WM_USER + 4;
inline constexpr UINT WM_UPDATETRAY = WM_USER + 5;
inline constexpr UINT WM_OPENCONFIG = WM_USER + 6;
inline constexpr UINT WM_CLOSECONFIG = WM_USER + 7;
inline constexpr UINT WM_UPDATESETTINGS = WM_USER + 8;
inline constexpr UINT WM_ADDTRAY = WM_USER + 9;
inline constexpr UINT WM_HIDETRAY = WM_USER + 10;
inline constexpr UINT WM_UNIKEYMENU = WM_USER + 11;
inline constexpr UINT WM_GETCLICKHWND = WM_USER + 12;
inline constexpr UINT WM_STACKLIST = WM_USER + 13;
inline constexpr UINT WM_FINISHMOVEMENT = WM_USER + 14;
inline constexpr UINT WM_CLOSEMODE = WM_USER + 15;
inline constexpr UINT WM_SETLAYOUTNUM = WM_USER + 16;
inline constexpr UINT WM_GETLAYOUTREZ = WM_USER + 17;
inline constexpr UINT WM_GETBESTLAYOUT = WM_USER + 18;
// #define WM_GETZONESLEN    (WM_USER+19)
inline constexpr UINT WM_GETZONES = WM_USER + 20;

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

// An action travels through a window message as one LPARAM.
[[nodiscard]] inline LPARAM PackAction(action_t action) {
  return (static_cast<LPARAM>(action.ac) << 24) |
         (static_cast<LPARAM>(action.fl) << 16) |
         static_cast<LPARAM>(action.wp);
}
[[nodiscard]] inline action_t UnpackAction(LPARAM lp) {
  action_t action;
  action.ac = (lp >> 24) & 0xFF;
  action.fl = (lp >> 16) & 0xFF;
  action.wp = lp & 0xFFFF;
  return action;
}

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

// True for the actions that move or resize a window. Upstream: MOUVEMENT.
[[nodiscard]] inline bool IsMovementAction(action_t action) {
  return action.ac <= AC_RESIZE;
}

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

// Ini key of zone number `index` of a layout: "Zone5" for the first layout,
// "AZone5" for the second, "BZone5" for the third, ... (empty if the layout
// number is out of range). Upstream: ZidxToZonestr / ZidxToZonestrA.
[[nodiscard]] inline std::string ZoneKey(int layout, int index) {
  if (layout < 0 || layout > 9) return {};
  std::string key;
  if (layout > 0) key += static_cast<char>('A' + layout - 1);
  return key + "Zone" + std::to_string(index);
}
[[nodiscard]] inline std::wstring ZoneKeyWide(int layout, int index) {
  const std::string key = ZoneKey(layout, index);
  return std::wstring(key.begin(), key.end());  // The key is ASCII.
}

// Upstream: is_base_action
[[nodiscard]] inline wchar_t* IsBaseAction(const wchar_t* a, const char* b) {
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
    if ((params = IsBaseAction(txt, action_map[ac]))) {
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
          action.wp = ParseInt(params);  // ParseInt stops on any non digit
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
