// Internal interface shared by the translation units of hooks.dll
// (hooks.cc, snap.cc, zones.cc). Not part of the public DLL API (hooks.h).
#ifndef ALTSNAP_HOOKS_INTERNAL_H_
#define ALTSNAP_HOOKS_INTERNAL_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

#include "common/ini_section.h"
#include "hooks/blacklist.h"
#include "hooks/hooks.h"
#include "hooks/input_options.h"
#include "hooks/worker.h"
#include "hooks/worker_command.h"

namespace altsnap::hooks {

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

// Enumerators
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
  // Owner-thread injection guards; USER calls may reenter the hooks.
  LONG ignorekey;
  LONG ignoreclick;

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
  std::wstring exename;
  wchar_t title[256];
  wchar_t classname[256];
};
// Mutable engine data belongs exclusively to the Load caller's thread.
// Session() checks affinity in all builds. The worker sees only command values;
// no mutex is held across USER calls and no drag state is shared with it.
struct HookSession {
  HookState input{};
  windowRR last_window{};
  std::vector<RECT> monitors;
  std::vector<snwdata> windows;
  std::vector<HWND> enumerated_windows;
  std::vector<snwdata> snapped_windows;
  std::vector<HWND> minimized_windows;
  uint64_t gesture = 0;
  RECT movement_rect{};
  RECT invisible_borders{};
  POINT speed_point{};
  POINT zone_point{};
  DWORD mouse_event_time = 0;
  UCHAR mouse_update_rate = 0;
  bool moved_to_fixed_point = false;
  std::optional<uint64_t> finish_pending;
  std::optional<UCHAR> deferred_alt;
  bool move_running = false;
  std::array<bool, BT_MB20 + 1> passed_buttons{};
};
// Non-aborting callback boundary: null after cleanup or on a foreign thread.
HookSession* TrySession();
HookSession& Session();

// Prevent teardown until the outermost owner-thread callback has returned.
// Idle/foreign callbacks do not acquire a session.
class SessionExecutionScope {
 public:
  SessionExecutionScope();
  ~SessionExecutionScope();
  SessionExecutionScope(const SessionExecutionScope&) = delete;
  SessionExecutionScope& operator=(const SessionExecutionScope&) = delete;

 private:
  bool bound_;
};

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

  InputActions actions;
};
extern const struct config& conf;  // Read-only; written by hook_config.cc

// The only two exports of hooks.dll (resolved by name with GetProcAddress).
// Both calls belong to mainhwnd's thread. Load returns nullptr on failure,
// after rolling back; Unload must complete before releasing the DLL.
extern "C" {
// TRUE confirms the caller owns an idle session (including rollback).
__declspec(dllexport) BOOL WINAPI Unload();
__declspec(dllexport) WNDPROC WINAPI Load(HWND mainhwnd,
                                          const wchar_t* inipath);
}

// Owner-thread-only Win32 handles and configuration stores. Configuration is
// frozen before worker startup and released only after its join.
extern HWND g_mainhwnd;
extern HINSTANCE hinstDLL;

// Low-level callbacks pass input through during startup and teardown.
[[nodiscard]] bool HooksActive();

inline void FixDWMRect(HWND hwnd, RECT* rect) {
  FixDWMRectLL(hwnd, rect, conf.SnapGap);
}
inline BOOL GetWindowRectL(HWND hwnd, RECT* rect) {
  return GetWindowRectLL(hwnd, rect, conf.SnapGap);
}

// To clamp width and height of windows
[[nodiscard]] inline int ClampWidth(int width) {
  return std::clamp<int>(width, Session().input.mmi.Min.x,
                         Session().input.mmi.Max.x);
}
[[nodiscard]] inline int ClampHeight(int height) {
  return std::clamp<int>(height, Session().input.mmi.Min.y,
                         Session().input.mmi.Max.y);
}
[[nodiscard]] inline int CLAMPWL(int width, const rgMMI_t* mmi) {
  return std::clamp<int>(width, mmi->Min.x, mmi->Max.x);
}
[[nodiscard]] inline int CLAMPHL(int height, const rgMMI_t* mmi) {
  return std::clamp<int>(height, mmi->Min.y, mmi->Max.y);
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
// Types and constants shared by the hooks.dll modules.
enum { SW_FULLSCREEN = 28 };

enum { SNTO_CNSNAP = 0, SNTO_EXTEND = 1, SNTO_NEXTBD = 2, SNTO_MOVETO = 4 };

enum { CW_RESTORE = (1 << 0), CW_TRIM = (1 << 1) };

enum { TRK_LASERMODE = (1 << 0), TRK_MOVETOMONITOR = (1 << 1) };

struct MinimizeWindowProcParams {
  HMONITOR hMon;
  HWND clickedhwnd;
};

// Remaining owner-thread module stores (defined in hooks.cc).
extern HWND g_transhwnd[4];
extern HWND g_mchwnd;

extern Blacklists BlkLst;
extern std::array<std::vector<UCHAR>, 36> g_inputSequences;
extern std::atomic<HHOOK> mousehook;

// Defined in hooks.cc.
void LogState(const char* Title);
HWND KreateMsgWin(WNDPROC proc, const wchar_t* name, LONG_PTR userdata);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_HOOKS_INTERNAL_H_
