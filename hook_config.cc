// hook config (split out of hooks.cc).

#include "hook_config.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "blacklist.h"
#include "hooks_internal.h"
#include "ini_section.h"
#include "snap.h"
#include "trans_window.h"
#include "zones.h"

namespace altsnap::hooks {

void freeblacklists() {
  BlkLst = {};
}
/////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
// Used to read Hotkeys and Hotclicks
// Reads a list of hexadecimal key codes ("A4 A5") into `keys` and
// zero-terminates it, so `keys` needs room for `max_keys + 1` entries.
// Returns the number of keys read.
// Upstream: readhotkeys
unsigned ReadHotkeys(const IniSection& section, const char* name,
                     const wchar_t* def, UCHAR* keys, unsigned MaxKeys) {
  const wchar_t* txt = section.GetString(name, def);
  size_t i = 0;
  if (!txt || !*txt) return i;
  const wchar_t* pos = txt;
  while (*pos) {
    // Store key
    if (i == MaxKeys) break;
    keys[i++] = lstrhex2u(pos);

    while (*pos && *pos >= '0') pos++;  // go to the end of the word
    while (*pos && *pos < '0') pos++;   // go to next char after spaces.
  }
  keys[i] = 0;
  return i;
}
// Reads up to `count` (at most 4) hexadecimal bytes ("RR GG BB [AA]") packed
// little-endian into one value, e.g. a COLORREF.
static uint32_t ReadPackedBytes(const IniSection& section, const char* key,
                                const wchar_t* def, unsigned count) {
  UCHAR bytes[MAXKEYS + 1];
  ReadHotkeys(section, key, def, bytes, count);
  uint32_t value = 0;
  for (unsigned i = 0; i < count; i++) value |= uint32_t{bytes[i]} << (8 * i);
  return value;
}
uint32_t ReadColor(const IniSection& section, const char* key,
                   const wchar_t* def, unsigned count) {
  return ReadPackedBytes(section, key, def, count);
}

/* TOTO: IMPLEMENT action parameters */
static action_t readaction(const IniSection& section, const char* key) {
  const wchar_t* txt = section.GetString(key, L"Nothing");
  if (!txt || !*txt) return k_action_none;

  return MapActionW(txt);
}
// Read all buttons actions from inipath
static void readbuttonactions(const IniSection& inputsection) {
  static const char* buttons[] = {
      "LMB",         "RMB",    "MMB",      "MB4",      "MB5",     "MB6",
      "MB7",         "MB8",    "MB9",      "MB10",     "MB11",    "MB12",
      "MB13",        "MB14",   "MB15",     "MB16",     "MB17",    "MB18",
      "MB19",        "MB20",   "Scroll",   "ScrollUp", "HScroll", "HScrollUp",

      "GrabWithAlt", "MoveUp", "ResizeUp",
  };

  for (size_t i = 0; i < std::size(buttons); i++) {
    action_t* const actionptr = &conf.Mouse[0];  // first action in list

    char key[32];
    lstrcpy_s(key, std::size(key) - 8, buttons[i]);
    int len = lstrlenA(key);
    // Read primary action (no sufix)
    actionptr[NACPB * i + 0] = readaction(inputsection, key);
    key[len] = 'B';
    key[len + 1] = '\0';  // Secondary B sufixe
    actionptr[NACPB * i + 1] = readaction(inputsection, key);

    // Titlbar actions
    key[len] = 'T';
    key[len + 1] = '\0';  // Titlebar T sufixes
    actionptr[NACPB * i + 2] = readaction(inputsection, key);
    key[len + 1] = 'B';
    key[len + 2] = '\0';  // TB
    actionptr[NACPB * i + 3] = readaction(inputsection, key);

    // Action while moving
    key[len] = 'M';
    key[len + 1] = '\0';  // Action while Moving M sufixe
    actionptr[NACPB * i + 4] = readaction(inputsection, key);
    key[len + 1] = 'B';
    key[len + 2] = '\0';  // MB
    actionptr[NACPB * i + 5] = readaction(inputsection, key);

    // Action while resizing
    key[len] = 'R';
    key[len + 1] = '\0';  // Action while Moving M sufixe
    actionptr[NACPB * i + 6] = readaction(inputsection, key);
    key[len + 1] = 'B';
    key[len + 2] = '\0';  // MB
    actionptr[NACPB * i + 7] = readaction(inputsection, key);
  }

  for (size_t j = 0; j < NACPB; j++) {
    // ScrollUp
    if (conf.Mouse[21 * NACPB + j].ac == AC_NONE)
      conf.Mouse[21 * NACPB + j] = conf.Mouse[20 * NACPB + j];

    // HScrollUp
    if (conf.Mouse[23 * NACPB + j].ac == AC_NONE)
      conf.Mouse[23 * NACPB + j] = conf.Mouse[22 * NACPB + j];
  }
}

void registerAllHotkeys(const wchar_t* inipath) {
  LOG("registerAllHotkeys START");
  ChangeWindowMessageFilterEx(g_mainhwnd, WM_HOTKEY, /*MSGFLT_ALLOW*/ 1,
                              nullptr);
  // MOD_ALT=1, MOD_CONTROL=2, MOD_SHIFT=4, MOD_WIN=8
  // RegisterHotKey(g_mainhwnd, 0xC000 + AC_KILL,   MOD_ALT|MOD_CONTROL, VK_F4);
  // // F4=73h Read All shortcuts in the [KBShortcuts] section.
  const IniSection section = IniSection::Read(inipath, L"KBShortcuts");

  conf.UsePtWindow = section.GetInt("UsePtWindow", 0);

#define ACVALUE(a, b, c) (#b),
  static const char* action_names[] = {ACTION_MAP};
#undef ACVALUE

  size_t idx = 0;
  for (const wchar_t* p : section.lines()) {
    if (idx >= std::size(conf.KBShortcutsList)) break;
    if (*p == ';') continue;

    action_t action = MapActionW(p);
    if (action.ac == AC_NONE) continue;

    const wchar_t* pp = lstrchr(p, L'=');
    if (!pp) continue;

    WORD HK = strtoi(p = ++pp);  // Read HOTKEY
    if (LOBYTE(HK) && HIBYTE(HK)) {
      // Lobyte is the virtual key code and hibyte is the mod_key
      if (RegisterHotKey(g_mainhwnd, 0xC001 + idx, HIBYTE(HK), LOBYTE(HK))) {
        conf.KBShortcutsList[idx] = action;  // Add action to the list
        LOG("OK registering hotkey %s_%d_%d=%x, [idx=%d]",
            action_names[action.ac], static_cast<int>(action.fl),
            static_cast<int>(action.wp), static_cast<unsigned>(HK),
            static_cast<int>(idx));
        idx++;
      } else {
        LOG("Error registering hotkey %s=%x", action_names[action.ac],
            static_cast<unsigned>(HK));
#ifdef LOG_STUFF
        wchar_t title[76], acN[32];
        lstrcpy_s(title, std::size(title),
                  APP_NAME L": unable to register hotkey for action ");
        str2tchar_s(acN, std::size(acN) - 1, action_names[action.ac]);
        lstrcat_s(title, std::size(title), acN);
        ErrorBox(title);
#endif  // LOG_STUFF
      }
    }
  }
  LOG("registerAllHotkeys END");
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

static void readalluchars(const IniSection& section,
                          const struct OptionListItem* optlist,
                          size_t listlen) {
  // Read all one-byte options straight into their `conf` fields.
  auto* const bytes = reinterpret_cast<UCHAR*>(&conf);
  for (size_t i = 0; i < listlen; i++) {
    bytes[optlist[i].offset] = section.GetInt(optlist[i].name, optlist[i].def);
  }
}
static void readallinputSequences(const IniSection& section) {
  UCHAR buf[512];
  char shrtN[6] = "Shrt0";

  freeallinputSequences();

  for (size_t i = 0; i < g_inputSequences.size(); i++) {
    shrtN[4] = i < 10 ? '0' + i : 'A' - 10 + i;
    unsigned len =
        ReadHotkeys(section, shrtN, L"\0", buf + 1, std::size(buf) - 4) / 2;
    buf[0] = len;  // We store the amount of input, not characters; the input
                   // includes up or down.
    if (len) g_inputSequences[i].assign(buf, buf + len * 2 + 1);
  }
}
void freeallinputSequences() {
  for (auto& sequence : g_inputSequences) sequence = {};
}

// ---------------------------------------------------------------------------
// ReadConfig(): one function per ini section.

namespace {

void ReadGeneralOptions(const wchar_t* inipath) {
  readalluchars(IniSection::Read(inipath, L"General"), General_uchars,
                std::size(General_uchars));

  // Consistency checks
  conf.CenterFraction = std::min<UCHAR>(conf.CenterFraction, 100);
  if (conf.SidesFraction == 255) conf.SidesFraction = conf.CenterFraction;
  conf.AHoff = std::min<UCHAR>(conf.AHoff, 100);
  conf.AVoff = std::min<UCHAR>(conf.AVoff, 100);
  conf.AeroSpeedTau = std::max<UCHAR>(1, conf.AeroSpeedTau);
  conf.MinAlpha = std::max<UCHAR>(1, conf.MinAlpha);
  state.snap = conf.AutoSnap;
}

void ReadAdvancedOptions(const wchar_t* inipath) {
  const IniSection advanced = IniSection::Read(inipath, L"Advanced");
  readalluchars(advanced, Advanced_uchars, std::size(Advanced_uchars));

  conf.ZoomFrac = std::max<UCHAR>(2, conf.ZoomFrac);
  conf.ZoomFracShift = std::max<UCHAR>(2, conf.ZoomFracShift);
  conf.BLCapButtons = advanced.GetInt("BLCapButtons", 3);
  conf.BLUpperBorder = advanced.GetInt("BLUpperBorder", 3);
  conf.AeroMaxSpeed = advanced.GetInt("AeroMaxSpeed", MAX_SNAP_SPEED);
  conf.LongClickMoveDelay = advanced.GetInt("LongClickMoveDelay", 0);
  if (conf.LongClickMoveDelay == 0) {
    conf.LongClickMoveDelay = GetDoubleClickTime();
  }
}

void ReadPerformanceOptions(const wchar_t* inipath) {
  readalluchars(IniSection::Read(inipath, L"Performance"), Performance_uchars,
                std::size(Performance_uchars));
  conf.MoveRate = std::max<UCHAR>(1, conf.MoveRate);
  conf.ResizeRate = std::max<UCHAR>(1, conf.ResizeRate);

  if (conf.RezTimer == 2 || conf.RezTimer == 4) {
    // 2 => Auto 1 (if 60Hz monitor) or 0.
    // 4 => Auto 1 (if 60Hz monitor) or 3.
    conf.RezTimer = conf.RezTimer == 2 ? 0 : 3;
    DEVMODE dvm;
    ZeroObject(dvm);
    dvm.dmSize = sizeof(dvm);
    if (EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &dvm)) {
      LOG("Display Frequency = %luHz", dvm.dmDisplayFrequency);
      if (dvm.dmDisplayFrequency == 60) conf.RezTimer = 1;
    }
  }
  if (conf.RezTimer) {
    conf.RefreshRate = 0;  // Ignore the refresh rate in RezTimer mode.
  }
  if (conf.FullWin == 2) {      // Use the system setting to decide.
    BOOL drag_full_win = TRUE;  // Default to ON if unable to detect
    SystemParametersInfo(SPI_GETDRAGFULLWINDOWS, 0, &drag_full_win, 0);
    conf.FullWin = drag_full_win;
  }
}

// Hotkey lists, in the same order as in struct config.
void ReadHotkeyLists(const IniSection& input) {
  static const struct {
    const char* name;
    const wchar_t* def;
    UCHAR* dst;
  } kHotkeyLists[] = {
      {"Hotkeys", L"A4 A5", conf.Hotkeys},      // VK_LMENU VK_RMENU
      {"Shiftkeys", L"A0 A1", conf.Shiftkeys},  // VK_LSHIFT VK_RSHIFT
      {"Hotclicks", nullptr, conf.Hotclick},
      {"Killkeys", L"09 2E", conf.Killkey},  // VK_TAB VK_DELETE
      {"XXButtons", nullptr, conf.XXButtons},
      {"ModKey", nullptr, conf.ModKey},
      {"HScrollKey", L"10", conf.HScrollKey},  // VK_SHIFT
      {"ESCKeys", L"1B", conf.ESCkeys},        // VK_ESCAPE
  };
  for (const auto& list : kHotkeyLists) {
    ReadHotkeys(input, list.name, list.def, list.dst, MAXKEYS);
  }
  UCHAR end_keys[MAXKEYS + 1];  // Key to be sent at the end of a movement.
  ReadHotkeys(input, "EndSendKey", L"11", end_keys, MAXKEYS);
  conf.EndSendKey = end_keys[0];
}

// Accelerators of the window list menu.
void ReadMenuAccelerators(const IniSection& input) {
  const size_t read =
      ReadHotkeys(input, "MenuAccelMap", nullptr, conf.MenuAccelMap,
                  std::size(conf.MenuAccelMap) - 1);
  // Fill the rest with the usual accelerators.
  for (size_t j = read; j < std::size(conf.MenuAccelMap); j++) {
    if (conf.NumberMenuItems) {
      conf.MenuAccelMap[j] = j < 10 ? L'0' + j : L'A' + j - 10;
    } else {
      conf.MenuAccelMap[j] = j < 26 ? L'A' + j : L'0' + j - 26;
    }
  }
  conf.MenuAccelMap[std::size(conf.MenuAccelMap) - 1] = 0;
}

void ReadInputOptions(const wchar_t* inipath) {
  const IniSection input = IniSection::Read(inipath, L"Input");
  readalluchars(input, Input_uchars, std::size(Input_uchars));
  readbuttonactions(input);

  if (conf.TopmostIndicator) {
    conf.PinColor = ReadColor(input, "PinColor", L"FF FF 00 54", 4);
  }
  if (!conf.FullWin) {
    CreateTransWin(input);  // The hollow window used while moving.
  }

  ReadHotkeyLists(input);
  ReadMenuAccelerators(input);
  readallinputSequences(input);  // User shortcuts / input sequences
}

void ReadZoneOptions(const wchar_t* inipath) {
  const IniSection zones = IniSection::Read(inipath, L"Zones");
  readalluchars(zones, Zones_uchars, std::size(Zones_uchars));

  if (conf.UseZones & 1) {  // We are using Zones
    conf.MaxLayouts = CLAMP(0, conf.MaxLayouts, 10);
    if (conf.UseZones & 2) {  // Grid Mode
      ReadGrids(zones);
    } else {
      ReadZones(zones);
    }
    SnapLayoutPreviewCreateDestroy(&zones /*CREATE*/);
  }
}

}  // namespace

void ReadConfig(const wchar_t* inipath) {
  conf.dbclickX = GetSystemMetrics(SM_CXDOUBLECLK);
  conf.dbclickY = GetSystemMetrics(SM_CYDOUBLECLK);

  ReadGeneralOptions(inipath);
  ReadAdvancedOptions(inipath);
  ReadPerformanceOptions(inipath);
  ReadInputOptions(inipath);

  BlkLst = Blacklists::Read(IniSection::Read(inipath, L"Blacklist"));

  ResetDB();  // Zero the database of restore info (snap.cc)

  ReadZoneOptions(inipath);

  conf.keepMousehook = ((conf.TTBActions & 1)    // titlebar action w/o Alt
                        || conf.InactiveScroll   // Inactive scrolling
                        || conf.Hotclick[0]      // Hotclick
                        || conf.LongClickMove);  // Move with long click
}

}  // namespace altsnap::hooks
