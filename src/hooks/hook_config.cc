// hook config (split out of hooks.cc).

#include "hooks/hook_config.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "common/ini_section.h"
#include "hooks/blacklist.h"
#include "hooks/hooks_internal.h"
#include "hooks/hotkey_message.h"
#include "hooks/mouse_hook.h"
#include "hooks/overlay/trans_window.h"
#include "hooks/window/snap.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

// The only writable copy of the configuration. Everything else sees it
// through the read-only reference `conf` (declared in hooks_internal.h).
static config g_config;
const config& conf = g_config;

namespace {
std::vector<action_t> g_registered_hotkeys;
}  // namespace

std::span<const action_t> RegisteredHotkeys() {
  return g_registered_hotkeys;
}

void UnregisterAllHotkeys() {
  for (size_t i = 0; i < g_registered_hotkeys.size(); ++i) {
    UnregisterHotKey(g_mainhwnd, kFirstRegisteredHotkey + i);
  }
  g_registered_hotkeys.clear();
}

// Upstream: freeblacklists
void FreeBlacklists() {
  BlkLst = {};
}
/////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
// Upstream: registerAllHotkeys
void RegisterAllHotkeys(const wchar_t* inipath) {
  LOG("RegisterAllHotkeys START");
  ChangeWindowMessageFilterEx(g_mainhwnd, WM_HOTKEY, /*MSGFLT_ALLOW*/ 1,
                              nullptr);
  // MOD_ALT=1, MOD_CONTROL=2, MOD_SHIFT=4, MOD_WIN=8
  // RegisterHotKey(g_mainhwnd, 0xC000 + AC_KILL,   MOD_ALT|MOD_CONTROL, VK_F4);
  // // F4=73h Read All shortcuts in the [KBShortcuts] section.
  const IniSection section = IniSection::Read(inipath, L"KBShortcuts");

  g_config.UsePtWindow = section.GetInt("UsePtWindow", 0);

#define ACVALUE(a, b, c) (#b),
  static const char* action_names[] = {ACTION_MAP};
#undef ACVALUE

  UnregisterAllHotkeys();
  for (const wchar_t* p : section.lines()) {
    const size_t idx = g_registered_hotkeys.size();
    if (idx >= kMaxRegisteredHotkeys) break;
    if (*p == ';') continue;

    action_t action = MapActionW(p);
    if (action.ac == AC_NONE) continue;

    const wchar_t* pp = FindChar(p, L'=');
    if (!pp) continue;

    WORD HK = ParseInt(p = ++pp);  // Read HOTKEY
    if (LOBYTE(HK) && HIBYTE(HK)) {
      // Lobyte is the virtual key code and hibyte is the mod_key
      if (RegisterHotKey(g_mainhwnd, kFirstRegisteredHotkey + idx, HIBYTE(HK),
                         LOBYTE(HK))) {
        g_registered_hotkeys.push_back(action);
        LOG("OK registering hotkey %s_%d_%d=%x, [idx=%d]",
            action_names[action.ac], static_cast<int>(action.fl),
            static_cast<int>(action.wp), static_cast<unsigned>(HK),
            static_cast<int>(idx));
      } else {
        LOG("Error registering hotkey %s=%x", action_names[action.ac],
            static_cast<unsigned>(HK));
#ifdef LOG_STUFF
        wchar_t title[76], acN[32];
        CopyString(title, std::size(title),
                   APP_NAME L": unable to register hotkey for action ");
        AsciiToWideBounded(acN, std::size(acN) - 1, action_names[action.ac]);
        AppendString(title, std::size(title), acN);
        ErrorBox(title);
#endif  // LOG_STUFF
      }
    }
  }
  LOG("RegisterAllHotkeys END");
}
struct OptionListItem {
  const char* name;  // Key in the ini file.
  int def;           // Default value.
  size_t offset;     // offsetof(config, field) of the one-byte field to fill.
};

// [General]
static const struct OptionListItem General_uchars[] = {
    {"AutoFocus", 0, offsetof(config, AutoFocus)},
    {"AutoSnap", 0, offsetof(config, AutoSnap)},
    {"Aero", 1, offsetof(config, Aero)},
    {"SmartAero", 1, offsetof(config, SmartAero)},
    {"StickyResize", 0, offsetof(config, StickyResize)},
    {"InactiveScroll", 0, offsetof(config, InactiveScroll)},
    {"MDI", 0, offsetof(config, MDI)},
    {"ResizeCenter", 1, offsetof(config, ResizeCenter)},
    {"CenterFraction", 24, offsetof(config, CenterFraction)},
    {"SidesFraction", 255, offsetof(config, SidesFraction)},
    {"AeroHoffset", 50, offsetof(config, AHoff)},
    {"AeroVoffset", 50, offsetof(config, AVoff)},
    {"MoveTrans", 255, offsetof(config, MoveTrans)},
    {"MMMaximize", 1, offsetof(config, MMMaximize)},
};
// [Advanced]
static const struct OptionListItem Advanced_uchars[] = {
    {"ResizeAll", 1, offsetof(config, ResizeAll)},
    {"FullScreen", 1, offsetof(config, FullScreen)},
    {"BLMaximized", 0, offsetof(config, BLMaximized)},
    {"AutoRemaximize", 0, offsetof(config, AutoRemaximize)},
    {"SnapThreshold", 20, offsetof(config, SnapThreshold)},
    {"AeroThreshold", 5, offsetof(config, AeroThreshold)},
    {"AeroTopMaximizes", 1, offsetof(config, AeroTopMaximizes)},
    {"UseCursor", 1, offsetof(config, UseCursor)},
    {"MinAlpha", 32, offsetof(config, MinAlpha)},
    {"AlphaDelta", 64, offsetof(config, AlphaDelta)},
    {"AlphaDeltaShift", 8, offsetof(config, AlphaDeltaShift)},
    {"ZoomFrac", 16, offsetof(config, ZoomFrac)},
    {"ZoomFracShift", 64, offsetof(config, ZoomFracShift)},
    {"NumberMenuItems", 0, offsetof(config, NumberMenuItems)},
    {"MaxMenuWidth", 80, offsetof(config, MaxMenuWidth)},
    {"AeroSpeedTau", 64, offsetof(config, AeroSpeedTau)},
    {"SnapGap", 0, offsetof(config, SnapGap)},
    {"ShiftSnaps", 1, offsetof(config, ShiftSnaps)},
    {"PiercingClick", 0, offsetof(config, PiercingClick)},
    {"DragSendsAltCtrl", 0, offsetof(config, DragSendsAltCtrl)},
    {"TopmostIndicator", 0, offsetof(config, TopmostIndicator)},
    {"RCCloseMItem", 1, offsetof(config, RCCloseMItem)},
    {"MaxKeysNum", 0, offsetof(config, MaxKeysNum)},
    {"DragThreshold", 1, offsetof(config, DragThreshold)},
    {"DragOutThresholdPx", 20, offsetof(config, DragOutThresholdPx)},
    {"AblockHotclick", 0, offsetof(config, AblockHotclick)},
    {"MenuShowOffscreenWin", 0, offsetof(config, MenuShowOffscreenWin)},
    {"MenuShowEmptyLabelWin", 0, offsetof(config, MenuShowEmptyLabelWin)},
    {"MenuZoom", 100, offsetof(config, MenuZoom)},
    {"IgnoreMinMaxInfo", 0, offsetof(config, IgnoreMinMaxInfo)},
    {"NotifyWinEvent", 1, offsetof(config, NotifyWinEvent)},
};
// [Performance]
static const struct OptionListItem Performance_uchars[] = {
    {"FullWin", 2, offsetof(config, FullWin)},
    {"TransWinOpacity", 0, offsetof(config, TransWinOpacity)},
    {"RefreshRate", 0, offsetof(config, RefreshRate)},
    {"RezTimer", 0, offsetof(config, RezTimer)},
    {"PinRate", 32, offsetof(config, PinRate)},
    {"MoveRate", 2, offsetof(config, MoveRate)},
    {"ResizeRate", 4, offsetof(config, ResizeRate)},
};
// [Input]
static const struct OptionListItem Input_uchars[] = {
    {"TTBActions", 0, offsetof(config, TTBActions)},
    {"KeyCombo", 0, offsetof(config, KeyCombo)},
    {"ScrollLockState", 0, offsetof(config, ScrollLockState)},
    {"LongClickMove", 0, offsetof(config, LongClickMove)},
    {"UniKeyHoldMenu", 0, offsetof(config, UniKeyHoldMenu)},
};
// [Zones]
static const struct OptionListItem Zones_uchars[] = {
    {"UseZones", 0, offsetof(config, UseZones)},
    {"ShowZonesPrevw", 1, offsetof(config, ShowZonesPrevw)},
    {"ShowZonesOnChange", 0, offsetof(config, ShowZonesOnChange)},
    {"ZonesPrevwOpacity", 161, offsetof(config, ZonesPrevwOpacity)},
    {"ZSnapMode", 0, offsetof(config, ZSnapMode)},
    {"MaxLayouts", 4, offsetof(config, MaxLayouts)},
    {"LayoutNumber", 0, offsetof(config, LayoutNumber)},
    {"InterZone", 32, offsetof(config, InterZone)},
#ifdef _WIN64
    {"FancyZone", 0, offsetof(config, FancyZone)},
#endif
};

// Upstream: readalluchars
static void ReadAllUchars(const IniSection& section,
                          const struct OptionListItem* optlist,
                          size_t listlen) {
  // Read all one-byte options straight into their `g_config` fields.
  auto* const bytes = reinterpret_cast<UCHAR*>(&g_config);
  for (size_t i = 0; i < listlen; i++) {
    bytes[optlist[i].offset] = section.GetInt(optlist[i].name, optlist[i].def);
  }
}
// Upstream: readallinputSequences
static void ReadAllInputSequences(const IniSection& section) {
  UCHAR buf[512];
  char shrtN[6] = "Shrt0";

  FreeAllInputSequences();

  for (size_t i = 0; i < g_inputSequences.size(); i++) {
    shrtN[4] = i < 10 ? '0' + i : 'A' - 10 + i;
    unsigned len = ReadHotkeys(section, shrtN, L"\0",
                               std::span(buf).subspan(1, std::size(buf) - 3)) /
                   2;
    buf[0] = len;  // We store the amount of input, not characters; the input
                   // includes up or down.
    if (len) g_inputSequences[i].assign(buf, buf + len * 2 + 1);
  }
}
// Upstream: freeallinputSequences
void FreeAllInputSequences() {
  for (auto& sequence : g_inputSequences) sequence = {};
}

// ---------------------------------------------------------------------------
// ReadConfig(): one function per ini section.

namespace {

void ReadGeneralOptions(const wchar_t* inipath) {
  ReadAllUchars(IniSection::Read(inipath, L"General"), General_uchars,
                std::size(General_uchars));

  // Consistency checks
  g_config.CenterFraction = std::min<UCHAR>(g_config.CenterFraction, 100);
  if (g_config.SidesFraction == 255)
    g_config.SidesFraction = g_config.CenterFraction;
  g_config.AHoff = std::min<UCHAR>(g_config.AHoff, 100);
  g_config.AVoff = std::min<UCHAR>(g_config.AVoff, 100);
  g_config.AeroSpeedTau = std::max<UCHAR>(1, g_config.AeroSpeedTau);
  g_config.MinAlpha = std::max<UCHAR>(1, g_config.MinAlpha);
  state.snap = g_config.AutoSnap;
}

void ReadAdvancedOptions(const wchar_t* inipath) {
  const IniSection advanced = IniSection::Read(inipath, L"Advanced");
  ReadAllUchars(advanced, Advanced_uchars, std::size(Advanced_uchars));

  g_config.ZoomFrac = std::max<UCHAR>(2, g_config.ZoomFrac);
  g_config.ZoomFracShift = std::max<UCHAR>(2, g_config.ZoomFracShift);
  g_config.BLCapButtons = advanced.GetInt("BLCapButtons", 3);
  g_config.BLUpperBorder = advanced.GetInt("BLUpperBorder", 3);
  g_config.AeroMaxSpeed = advanced.GetInt("AeroMaxSpeed", MAX_SNAP_SPEED);
  g_config.LongClickMoveDelay = advanced.GetInt("LongClickMoveDelay", 0);
  if (g_config.LongClickMoveDelay == 0) {
    g_config.LongClickMoveDelay = GetDoubleClickTime();
  }
}

void ReadPerformanceOptions(const wchar_t* inipath) {
  ReadAllUchars(IniSection::Read(inipath, L"Performance"), Performance_uchars,
                std::size(Performance_uchars));
  g_config.MoveRate = std::max<UCHAR>(1, g_config.MoveRate);
  g_config.ResizeRate = std::max<UCHAR>(1, g_config.ResizeRate);

  if (g_config.RezTimer == 2 || g_config.RezTimer == 4) {
    // 2 => Auto 1 (if 60Hz monitor) or 0.
    // 4 => Auto 1 (if 60Hz monitor) or 3.
    g_config.RezTimer = g_config.RezTimer == 2 ? 0 : 3;
    DEVMODE dvm;
    ZeroObject(dvm);
    dvm.dmSize = sizeof(dvm);
    if (EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &dvm)) {
      LOG("Display Frequency = %luHz", dvm.dmDisplayFrequency);
      if (dvm.dmDisplayFrequency == 60) g_config.RezTimer = 1;
    }
  }
  if (g_config.RezTimer) {
    g_config.RefreshRate = 0;  // Ignore the refresh rate in RezTimer mode.
  }
  if (g_config.FullWin == 2) {  // Use the system setting to decide.
    BOOL drag_full_win = TRUE;  // Default to ON if unable to detect
    SystemParametersInfo(SPI_GETDRAGFULLWINDOWS, 0, &drag_full_win, 0);
    g_config.FullWin = drag_full_win;
  }
}

// Hotkey lists, in the same order as in struct config.
void ReadHotkeyLists(const IniSection& input) {
  static const struct {
    const char* name;
    const wchar_t* def;
    std::span<UCHAR> dst;
  } kHotkeyLists[] = {
      {"Hotkeys", L"A4 A5", g_config.Hotkeys},      // VK_LMENU VK_RMENU
      {"Shiftkeys", L"A0 A1", g_config.Shiftkeys},  // VK_LSHIFT VK_RSHIFT
      {"Hotclicks", nullptr, g_config.Hotclick},
      {"Killkeys", L"09 2E", g_config.Killkey},  // VK_TAB VK_DELETE
      {"XXButtons", nullptr, g_config.XXButtons},
      {"ModKey", nullptr, g_config.ModKey},
      {"HScrollKey", L"10", g_config.HScrollKey},  // VK_SHIFT
      {"ESCKeys", L"1B", g_config.ESCkeys},        // VK_ESCAPE
  };
  for (const auto& list : kHotkeyLists) {
    ReadHotkeys(input, list.name, list.def, list.dst);
  }
  UCHAR end_keys[MAXKEYS + 1];  // Key to be sent at the end of a movement.
  ReadHotkeys(input, "EndSendKey", L"11", end_keys);
  g_config.EndSendKey = end_keys[0];
}

// Accelerators of the window list menu.
void ReadMenuAccelerators(const IniSection& input) {
  const size_t read =
      ReadHotkeys(input, "MenuAccelMap", nullptr, g_config.MenuAccelMap);
  // Fill the rest with the usual accelerators.
  for (size_t j = read; j < std::size(g_config.MenuAccelMap); j++) {
    if (g_config.NumberMenuItems) {
      g_config.MenuAccelMap[j] = j < 10 ? L'0' + j : L'A' + j - 10;
    } else {
      g_config.MenuAccelMap[j] = j < 26 ? L'A' + j : L'0' + j - 26;
    }
  }
  g_config.MenuAccelMap[std::size(g_config.MenuAccelMap) - 1] = 0;
}

void ReadInputOptions(const wchar_t* inipath) {
  const IniSection input = IniSection::Read(inipath, L"Input");
  ReadAllUchars(input, Input_uchars, std::size(Input_uchars));
  g_config.actions = ReadButtonActions(input);

  if (g_config.TopmostIndicator) {
    g_config.PinColor =
        ReadColor(input, "PinColor", L"FF FF 00 54", ColorChannels::kRgba);
  }
  if (!g_config.FullWin) {
    CreateTransWin(input);  // The hollow window used while moving.
  }

  ReadHotkeyLists(input);
  ReadMenuAccelerators(input);
  ReadAllInputSequences(input);  // User shortcuts / input sequences
}

void ReadZoneOptions(const wchar_t* inipath) {
  const IniSection zones = IniSection::Read(inipath, L"Zones");
  ReadAllUchars(zones, Zones_uchars, std::size(Zones_uchars));

  if (g_config.UseZones & 1) {  // We are using Zones
    g_config.MaxLayouts = std::clamp<UCHAR>(g_config.MaxLayouts, 0, 10);
    if (g_config.UseZones & 2) {  // Grid Mode
      ReadGrids(zones);
    } else {
      ReadZones(zones);
    }
    SnapLayoutPreviewCreateDestroy(&zones /*CREATE*/);
  }
}

}  // namespace

void ReadConfig(const wchar_t* inipath) {
  g_config.dbclickX = GetSystemMetrics(SM_CXDOUBLECLK);
  g_config.dbclickY = GetSystemMetrics(SM_CYDOUBLECLK);

  ReadGeneralOptions(inipath);
  ReadAdvancedOptions(inipath);
  ReadPerformanceOptions(inipath);
  ReadInputOptions(inipath);

  BlkLst = Blacklists::Read(IniSection::Read(inipath, L"Blacklist"));

  ResetDB();  // Zero the database of restore info (snap.cc)

  ReadZoneOptions(inipath);

  SetKeepMouseHook((g_config.TTBActions & 1)    // titlebar action w/o Alt
                   || g_config.InactiveScroll   // Inactive scrolling
                   || g_config.Hotclick[0]      // Hotclick
                   || g_config.LongClickMove);  // Move with long click
}

}  // namespace altsnap::hooks
