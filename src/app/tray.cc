/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright (C) 2015    Stefan Sundin                                   *
 * This program is free software: you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation, either version 3 or later.              *
 * Modified By Raymond Gillibert in 2020                                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "app/tray.h"

#include <algorithm>
#include <format>

#include "app/altsnap.h"
#include "app/languages.h"
#include "common/ini_section.h"
#include "common/text_format.h"
#include "common/winutil.h"
#include "hooks/hooks.h"

namespace altsnap::app {

static struct {  // NOTIFYICONDATA for NT4
  DWORD cbSize;
  HWND hWnd;
  UINT uID;
  UINT uFlags;
  UINT uCallbackMessage;
  HICON hIcon;
  wchar_t szTip[64];
} tray;

unsigned char tray_added = 0;
unsigned char tray_hidden = 0;
unsigned char UseZones = 0;
int LayoutNumber = 0;
int MaxLayouts = 0;

static const wchar_t* tray_iconstr[] = {L"TRAY_OFF", L"TRAY_ON", L"TRAY_SUS"};
static const wchar_t* tray_tooltipstr[] = {
    APP_NAME L" (Off)",
    APP_NAME L" (On)",
    APP_NAME L"...",
};
HICON g_icons[ICONS_COUNT];

static void LoadAllIcons() {
  // A theme is a subfolder of Themes\ holding TRAY_OFF.ico, ...
  const std::wstring theme =
      ReadIniString(L"General", L"Theme", L"", inipath.c_str());
  const std::wstring executable_dir = GetExecutableDirectory();
  if (theme.size() > 1 && !executable_dir.empty()) {
    const std::wstring theme_dir =
        executable_dir + L"\\Themes\\" + theme + L"\\";
    for (size_t i = 0; i < ICONS_COUNT; ++i) {
      const std::wstring path = theme_dir + tray_iconstr[i] + L".ico";
      auto icon = static_cast<HICON>(
          LoadImage(g_hinst, path.c_str(), IMAGE_ICON, 0, 0,
                    LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_LOADTRANSPARENT));
      g_icons[i] =
          icon ? icon : LoadIcon(g_hinst, MAKEINTRESOURCE(TRAY_OFF + i));
    }
    return;
  }
  // Fallback to internal icons.
  for (size_t i = 0; i < ICONS_COUNT; i++)
    g_icons[i] = LoadIcon(g_hinst, MAKEINTRESOURCE(TRAY_OFF + i));
}

int InitTray() {
  ScrollLockState =
      GetPrivateProfileInt(L"Input", L"ScrollLockState", 0, inipath.c_str());
  LayoutNumber =
      GetPrivateProfileInt(L"Zones", L"LayoutNumber", 0, inipath.c_str());
  MaxLayouts =
      GetPrivateProfileInt(L"Zones", L"MaxLayouts", 0, inipath.c_str());
  MaxLayouts = std::clamp(MaxLayouts, 0, 10);
  LayoutNumber = std::clamp(LayoutNumber, 0, std::max(0, MaxLayouts - 1));

  LoadAllIcons();

  // Create icondata
  tray.cbSize = sizeof(tray);
  tray.hWnd = g_hwnd;
  tray.uID = 0;
  tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  tray.uCallbackMessage = WM_TRAY;

  // Register TaskbarCreated so we can re-add the tray icon if (when)
  // explorer.exe crashes
  WM_TASKBARCREATED = RegisterWindowMessage(L"TaskbarCreated");
  LOG("Register TaskbarCreated message: %X", WM_TASKBARCREATED);

  return 0;
}

int UpdateTray() {
  int Index = !!IsEnabled();
  if (Index) {
    Index += (ScrollLockState & 1) &&
             !(!(GetKeyState(VK_SCROLL) & 1) ^ !(ScrollLockState & 2));
    if (GetProp(g_hwnd, kPropAutoSnapOnOff)) Index = 2;
  }
  // Load info tool tip and tray icon
  CopyString(tray.szTip, std::size(tray.szTip), tray_tooltipstr[Index]);
  tray.hIcon = g_icons[Index];

  // Only add or modify if not hidden or if balloon will be displayed
  if (!tray_hidden || tray.uFlags & NIF_INFO) {
    // Try a few times, sleep 100 ms between each attempt
    int i = 1;
    LOG("Updating tray icon");
    while (!Shell_NotifyIcon(tray_added ? NIM_MODIFY : NIM_ADD,
                             reinterpret_cast<PNOTIFYICONDATA>(&tray))) {
      LOG("Failed in try No. %d", i);

      // Maybe we just tried to add an already existing tray.
      // Happens after DPI change under Win 10 (TaskbarCreated) msg.
      if (!tray_added &&
          Shell_NotifyIcon(NIM_MODIFY,
                           reinterpret_cast<PNOTIFYICONDATA>(&tray))) {
        LOG("Updated tray icon");
        tray_added = 1;
        return 0;
      }

      if (i > 2) {
        LOG("Failed all atempts!!");
        return 1;
      }
      Sleep(100);
      i++;
    }
    LOG("Sucess at try %d", i);
    // Success
    tray_added = 1;
  }
  return 0;
}

int RemoveTray() {
  LOG("RemoveTray() tray_added=%d", static_cast<int>(tray_added));
  if (!tray_added) return 1;

  if (!Shell_NotifyIcon(NIM_DELETE, reinterpret_cast<PNOTIFYICONDATA>(&tray)))
    return 1;

  // Success
  tray_added = 0;
  return 0;
}

// Zones functions
void WriteCurrentLayoutNumber() {
  if (MaxLayouts) {
    WritePrivateProfileString(L"Zones", L"LayoutNumber",
                              std::to_wstring(LayoutNumber).c_str(),
                              inipath.c_str());
  }
}
// Save a rect as a string in a Zone<num> entry in the inifile
static void SaveZone(const RECT* rc, unsigned num) {
  WritePrivateProfileString(L"Zones", ZoneKeyWide(LayoutNumber, num).c_str(),
                            FormatRect(*rc).c_str(), inipath.c_str());
}
static void ClearAllZones() {
  for (int i = 0; i < 2048; ++i) {
    const std::wstring key = ZoneKeyWide(LayoutNumber, i);
    if (!ReadIniString(L"Zones", key.c_str(), L"", inipath.c_str()).empty()) {
      WritePrivateProfileString(L"Zones", key.c_str(), L"", inipath.c_str());
    }
  }
}
// Call with lParam = 1 to reset NZones
static BOOL CALLBACK SaveTestWindow(HWND hwnd, LPARAM lParam) {
  static unsigned NZones;
  if (lParam) {  // Reset number of Zones
    NZones = 0;
    return FALSE;
  }

  wchar_t classn[256];
  RECT rc;
  if (IsWindowVisible(hwnd) && GetClassName(hwnd, classn, std::size(classn)) &&
      !lstrcmp(classn, APP_NAME L"-Test") && GetWindowRectL(hwnd, &rc)) {
    SaveZone(&rc, NZones++);
    PostMessage(hwnd, WM_CLOSE, 0, 0);
  }
  return TRUE;
}

void SaveCurrentLayout() {
  ClearAllZones();
  SaveTestWindow(nullptr, 1);
  EnumThreadWindows(GetCurrentThreadId(), SaveTestWindow, 0);
}

static BOOL CALLBACK CloseTestWindowCB(HWND hwnd, LPARAM lParam) {
  wchar_t classn[256];
  if (GetClassName(hwnd, classn, std::size(classn)) &&
      !lstrcmp(classn, APP_NAME L"-Test")) {
    PostMessage(hwnd, WM_CLOSE, 0, 0);
  }
  return TRUE;
}

void CloseAllTestWindows() {
  EnumThreadWindows(GetCurrentThreadId(), CloseTestWindowCB, 0);
}

// Upstream: catFullLayoutName
std::wstring FullLayoutName(int laynum) {
  std::wstring name = l10n->MenuSnapLayout;
  name += std::to_wstring(laynum + 1);
  if (!G_HooksWinProc) return name + L"  (...)";
  if (const DWORD rez = G_HooksWinProc(g_hwnd, WM_GETLAYOUTREZ, laynum, 0)) {
    // Add (width:height) to label the layout.
    return name + std::format(L"  ({}:{})", LOWORD(rez), HIWORD(rez));
  }
  return name + L"  " + l10n->MenuEmptyZone;  // (empty)
}

void ShowContextMenu(HWND hwnd) {
  POINT pt;
  GetCursorPos(&pt);
  HMENU menu = CreatePopupMenu();

  AppendMenu(menu, MF_STRING, CMD_TOGGLE,
             (IsEnabled() ? l10n->MenuDisable : l10n->MenuEnable));
  AppendMenu(menu, MF_STRING, CMD_HIDE, l10n->MenuHideTray);
  InsertMenu(menu, -1, elevated ? MF_BYPOSITION | MF_GRAYED : MF_BYPOSITION,
             CMD_ELEVATE,
             (elevated ? l10n->GeneralElevated : l10n->GeneralElevate));

  AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenu(menu, MF_STRING, CMD_CONFIG, l10n->MenuConfigure);
  AppendMenu(menu, MF_STRING, CMD_ABOUT, l10n->MenuAbout);
  AppendMenu(menu, MF_STRING, CMD_OPENINIFILE, l10n->MenuOpenIniFile);

  if (UseZones & 1) {  // Zones section
    if (MaxLayouts) AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < MaxLayouts; ++i) {
      // Check the current layout We use a simple checkmark,
      // because a radio button is more complex to setup.
      UINT mfflags =
          i == LayoutNumber ? MF_STRING | MF_CHECKED : MF_STRING | MF_UNCHECKED;
      AppendMenu(menu, mfflags, CMD_SNAPLAYOUT + i, FullLayoutName(i).c_str());
    }

    if (!(UseZones & 2)) {
      const std::wstring edit_layout =
          std::format(L"{} {}", l10n->MenuEditLayout, LayoutNumber + 1);
      AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenu(menu, MF_STRING, CMD_TESTWIN, l10n->AdvancedTestWindow);
      AppendMenu(menu, MF_STRING, CMD_EDITLAYOUT, edit_layout.c_str());
      AppendMenu(menu,
                 FindWindow(APP_NAME L"-test", nullptr) ? MF_STRING
                                                        : MF_STRING | MF_GRAYED,
                 CMD_SAVEZONES, l10n->MenuSaveZones);

      AppendMenu(menu,
                 FindWindow(APP_NAME L"-test", nullptr) ? MF_STRING
                                                        : MF_STRING | MF_GRAYED,
                 CMD_CLOSEZONES, l10n->MenuCloseAllZones);
    }
  }

  AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenu(menu, MF_STRING, CMD_EXIT, l10n->MenuExit);

  // Track menu
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu, TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
  DestroyMenu(menu);
}

}  // namespace altsnap::app
