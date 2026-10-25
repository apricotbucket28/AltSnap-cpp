// Internal interface shared by the translation units of hooks.dll
// (hooks.cc, snap.cc, zones.cc). Not part of the public DLL API (hooks.h).
#ifndef ALTSNAP_HOOKS_INTERNAL_H_
#define ALTSNAP_HOOKS_INTERNAL_H_

#include <array>
#include <vector>

#include "hooks.h"
#include "ini_section.h"

// Timer messages
enum {
  PIN_TIMER = (1),
  REHOOK_TIMER = (2),
  SPEED_TIMER = (3),
  GRAB_TIMER = (4),
  // #define ALTUP_TIMER     (5)
  HIDELAYOUT_TIMER = (6),
  POOL_TIMER = (7)
};

inline constexpr UINT WM_DOWORK = WM_APP + 6;
inline constexpr UINT WM_DOMOUSEMOVE = WM_APP + 7;
inline constexpr UINT WM_DOACTION = WM_APP + 8;

enum { BD_HIDDEN_L = 1, BD_HIDDEN_T = 2, BD_HIDDEN_R = 4, BD_HIDDEN_B = 8 };

// #define NO_HOOK_LL

enum { MAX_SNAP_SPEED = 65535 };
enum {
  NOT_MOVING = 0,
  NOW_MOVING = 1,
  CURSOR_ONLY = 66,
  RESET_OFFSET = 22,
  DRAG_WAIT = 77
};

// Number of actions per button!
//  2 for Alt+Clikc
// +2 for Titlebar action
// +2 for Action while moving
// +2 for action while resizing
enum { NACPB = 8 };

enum { STACK = 0x1000 };

// Enumerators
enum button {
  BT_NONE = 0,
  BT_LMB = 0x02,
  BT_RMB = 0x03,
  BT_MMB = 0x04,
  BT_MB4 = 0x05,
  BT_MB5 = 0x06,
  BT_MB6 = 0x07,
  BT_MB7 = 0x08,
  BT_MB8 = 0x09,
  BT_MB9 = 0x0A,
  BT_MB10 = 0x0B,
  BT_MB11 = 0x0C,
  BT_MB12 = 0x0D,
  BT_MB13 = 0x0E,
  BT_MB14 = 0x0F,
  BT_MB15 = 0x10,
  BT_MB16 = 0x11,
  BT_MB17 = 0x12,
  BT_MB18 = 0x13,
  BT_MB19 = 0x14,
  BT_MB20 = 0x15,
  BT_WHEELD = 0x16,
  BT_WHEELU = 0x17,
  BT_HWHEELD = 0x18,
  BT_HWHEELU = 0x19
};
enum resizeX { RZ_XNONE = 0, RZ_LEFT = 1, RZ_RIGHT = 2, RZ_XCENTER = 3 };
enum resizeY { RZ_YNONE = 0, RZ_TOP = 1, RZ_BOTTOM = 2, RZ_YCENTER = 3 };
enum buttonstate { STATE_NONE, STATE_DOWN, STATE_UP };

enum { BT_PROBE = (1 << 16) };

struct resizeXY {
  BYTE x;
  BYTE y;
};
using resizexy_t = resizeXY;

struct windowRR {
  HWND hwnd;
  int x;
  int y;
  int width;
  int height;
  UINT odpi;
  resizexy_t resize;
  UCHAR end;
  UCHAR start;
  UCHAR moveonly;
  UCHAR maximize;
  UCHAR snap;
  // UCHAR resizing_now;
};
using windowRR_t = windowRR;
extern windowRR_t LastWin;

struct rgMMI {
  POINT Min;
  POINT Max;
};
using rgMMI_t = rgMMI;
inline constexpr resizexy_t AUTORESIZE = {RZ_XNONE, RZ_YNONE};

struct snwdata {
  RECT rc;
  HWND hwnd;
  unsigned flag;
};

// State
struct HookState {
  rgMMI_t mmi;
  POINT clickpt;
  POINT prevpt;
  POINT ctrlpt;
  POINT shiftpt;
  POINT offset;
  POINT mdipt;

  HWND hwnd;
  HWND sclickhwnd;
  HWND mdiclient;
  DWORD clicktime;
  short hittest;
  short delta;
  unsigned Speed;
  HMENU unikeymenu;
  volatile LONG ignorekey;
  volatile LONG ignoreclick;

  UCHAR alt;
  UCHAR alt1;
  UCHAR blockaltup;
  UCHAR ctrl;

  UCHAR shift;
  UCHAR snap;
  UCHAR altsnaponoff;
  UCHAR moving;

  UCHAR blockmouseup;
  UCHAR fwmouseup;
  UCHAR enumed;
  UCHAR usezones;
  UCHAR forcelayoutdisplay;

  UCHAR clickbutton;
  UCHAR resizable;
  struct {
    UCHAR maximized;
    UCHAR fullscreen;
    RECT mon;
    HMONITOR monitor;
    int width;
    int height;
    int right;
    int bottom;
    UINT dpi;
  } origin;

  action_t sactiondone;
  UCHAR xxbutton;
  UCHAR ignorept;
  action_t action;
  resizexy_t resize;
  HWND cached_hwnd_blacklist;
  wchar_t exename[MAX_PATH];
  wchar_t title[256];
  wchar_t classname[256];
};
extern HookState state;

// Snap
extern std::vector<RECT> monitors;

/* for the Snap to borders option */
extern std::vector<snwdata> wnds;

extern std::vector<HWND> hwnds;

// Settings
enum { MAXKEYS = 15 };
struct config {
  // System settings
  short dbclickX;
  short dbclickY;
  // [General]
  UCHAR AutoFocus;
  UCHAR AutoSnap;
  UCHAR Aero;
  UCHAR SmartAero;
  UCHAR StickyResize;
  UCHAR InactiveScroll;
  UCHAR MDI;
  UCHAR ResizeCenter;
  UCHAR CenterFraction;
  UCHAR SidesFraction;
  UCHAR AVoff;
  UCHAR AHoff;
  UCHAR MoveTrans;
  UCHAR MMMaximize;
  // [Advanced]
  UCHAR ResizeAll;
  UCHAR FullScreen;
  UCHAR BLMaximized;
  UCHAR AutoRemaximize;
  UCHAR SnapThreshold;
  UCHAR AeroThreshold;
  UCHAR AeroTopMaximizes;
  UCHAR UseCursor;
  UCHAR MinAlpha;
  char AlphaDelta;
  char AlphaDeltaShift;
  UCHAR ZoomFrac;
  UCHAR ZoomFracShift;
  UCHAR NumberMenuItems;
  UCHAR MaxMenuWidth;
  UCHAR AeroSpeedTau;
  char SnapGap;
  UCHAR ShiftSnaps;
  UCHAR PiercingClick;
  UCHAR DragSendsAltCtrl;
  UCHAR TopmostIndicator;
  UCHAR RCCloseMItem;
  UCHAR MaxKeysNum;
  UCHAR DragThreshold;
  UCHAR DragOutThresholdPx;  // SPI_GETMOUSEDRAGOUTTHRESHOLD but it is buggy on
                             // Win11...
  UCHAR AblockHotclick;
  UCHAR MenuShowOffscreenWin;
  UCHAR MenuShowEmptyLabelWin;
  UCHAR MenuZoom;
  UCHAR IgnoreMinMaxInfo;
  UCHAR NotifyWinEvent;
  // [Performance]
  UCHAR FullWin;
  UCHAR TransWinOpacity;
  UCHAR RefreshRate;
  UCHAR RezTimer;
  UCHAR PinRate;
  UCHAR MoveRate;
  UCHAR ResizeRate;
  // [Input]
  UCHAR TTBActions;
  UCHAR KeyCombo;
  UCHAR ScrollLockState;
  UCHAR LongClickMove;
  UCHAR UniKeyHoldMenu;
  // [Zones]
  UCHAR UseZones;
  UCHAR ShowZonesPrevw;
  UCHAR ShowZonesOnChange;
  UCHAR ZonesPrevwOpacity;
  UCHAR ZSnapMode;
  UCHAR MaxLayouts;
  UCHAR LayoutNumber;
  char InterZone;
#ifdef _WIN64
  UCHAR FancyZone;
#endif
  // [KBShortcuts]
  UCHAR UsePtWindow;
  // -- -- -- -- -- -- --
  UCHAR keepMousehook;
  UCHAR EndSendKey;  // Used to be VK_CONTROL
  WORD AeroMaxSpeed;
  WORD LongClickMoveDelay;
  DWORD BLCapButtons;
  DWORD BLUpperBorder;
  int PinColor;

  UCHAR Hotkeys[MAXKEYS + 1];
  UCHAR Shiftkeys[MAXKEYS + 1];
  UCHAR Hotclick[MAXKEYS + 1];
  UCHAR Killkey[MAXKEYS + 1];
  UCHAR XXButtons[MAXKEYS + 1];
  UCHAR ModKey[MAXKEYS + 1];
  UCHAR HScrollKey[MAXKEYS + 1];
  UCHAR ESCkeys[MAXKEYS + 1];

  UCHAR MenuAccelMap[36 + 1];
  UCHAR pad0[3];

  action_t Mouse[NACPB * (20 + 4)];  // Up to 20 buttons + vertical and
                                     // horizontal wheels D/U;
  action_t GrabWithAlt[NACPB];       // Actions without click
  action_t MoveUp[NACPB];            // Actions on (long) Move Up w/o drag
  action_t ResizeUp[NACPB];          // Actions on (long) Resize Up w/o drag

  action_t KBShortcutsList[64];
};
extern struct config conf;

// The only two exports of hooks.dll (resolved by name with GetProcAddress).
extern "C" {
__declspec(dllexport) void WINAPI Unload();
__declspec(dllexport) WNDPROC WINAPI Load(HWND mainhwnd,
                                          const wchar_t* inipath);
}

extern HWND g_mainhwnd;
extern HINSTANCE hinstDLL;

inline void FixDWMRect(HWND hwnd, RECT* rect) {
  FixDWMRectLL(hwnd, rect, conf.SnapGap);
}
inline BOOL GetWindowRectL(HWND hwnd, RECT* rect) {
  return GetWindowRectLL(hwnd, rect, conf.SnapGap);
}

// To clamp width and height of windows
[[nodiscard]] inline int CLAMPW(int width) {
  return CLAMP(state.mmi.Min.x, width, state.mmi.Max.x);
}
[[nodiscard]] inline int CLAMPH(int height) {
  return CLAMP(state.mmi.Min.y, height, state.mmi.Max.y);
}
[[nodiscard]] inline int CLAMPWL(int width, const rgMMI_t* mmi) {
  return CLAMP(mmi->Min.x, width, mmi->Max.x);
}
[[nodiscard]] inline int CLAMPHL(int height, const rgMMI_t* mmi) {
  return CLAMP(mmi->Min.y, height, mmi->Max.y);
}

[[nodiscard]] inline int ISCLAMPEDWL(int x, const rgMMI_t* mmi) {
  return mmi->Min.x <= x && x <= mmi->Max.x;
}
[[nodiscard]] inline int ISCLAMPEDHL(int y, const rgMMI_t* mmi) {
  return mmi->Min.y <= y && y <= mmi->Max.y;
}

/* If pt and ptt are it is the same points with 4px tolerence */
[[nodiscard]] inline int IsSamePTT(const POINT* pt, const POINT* ptt) {
  const short Tx = conf.dbclickX;
  const short Ty = conf.dbclickY;
  return !(pt->x > ptt->x + Tx || pt->y > ptt->y + Ty || pt->x < ptt->x - Tx ||
           pt->y < ptt->y - Ty);
}

[[nodiscard]] inline int IsPtDragOut(const POINT* pt, const POINT* ptt) {
  const long Tx = conf.DragOutThresholdPx;
  const long Ty = Tx;
  return !(pt->x > ptt->x + Tx || pt->y > ptt->y + Ty || pt->x < ptt->x - Tx ||
           pt->y < ptt->y - Ty);
}

// Specific includes

// Defined in hooks.cc, used by zones.cc.
BOOL CALLBACK EnumMonitorsProc(HMONITOR hMonitor, HDC hdcMonitor,
                               LPRECT lprcMonitor, LPARAM dwData);
HMONITOR GetMonitorInfoFromWin(HWND hwnd, MONITORINFO* mi);
[[nodiscard]] int IsResizable(HWND hwnd);
void MouseMove(POINT pt);
void MoveWindowAsync(HWND hwnd, int x, int y, int w, int h);
void SetOriginFromRestoreData(HWND hwnd, action_t action);
VOID CALLBACK TimerWindowProc(HWND hwnd, UINT msg, UINT_PTR idEvent,
                              DWORD dwTime);
unsigned ReadHotkeys(const IniSection& section, const char* name,
                     const wchar_t* def, UCHAR* keys, unsigned max_keys);
// Reads `count` hex bytes ("RR GG BB") as a packed little-endian COLORREF.
uint32_t ReadColor(const IniSection& section, const char* key,
                   const wchar_t* def, unsigned count);

#endif  // ALTSNAP_HOOKS_INTERNAL_H_
