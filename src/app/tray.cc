/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright (C) 2015    Stefan Sundin                                   *
 * This program is free software: you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation, either version 3 or later.              *
 * Modified By Raymond Gillibert in 2020                                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "app/tray.h"

#include "app/altsnap.h"
#include "app/languages.h"
#include "hooks/hooks.h"
#include "ui/config.h"

namespace altsnap::app {

#ifndef NIF_INFO
#define NIF_INFO 0x00000010
#endif

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
  wchar_t theme[MAX_PATH];  // Get theme name
  int ret = GetPrivateProfileString(L"General", L"Theme", L"", theme,
                                    std::size(theme), inipath);
  if (ret && theme[1]) {
    wchar_t path[MAX_PATH];
    DWORD mod = GetModuleFileName(nullptr, path, std::size(path));
    if (mod) {
      PathRemoveFileSpecL(path);
      lstrcat_s(path, std::size(path), L"\\Themes\\");  // Themes subfolder
      lstrcat_s(path, std::size(path), theme);          // Theme name
      int len = lstrlen(path);
      wchar_t* p = path + len;
      *p++ = L'\\';
      if (len < MAX_PATH - 13) {  // strlen("TRAY_OFF.ICO")==12
        UCHAR i = 0;
        for (i = 0; i < 3; i++) {
          lstrcpy_s(p, std::size(path) - len, tray_iconstr[i]);
          lstrcat_s(path, std::size(path), L".ico");
          auto tmp = static_cast<HICON>(
              LoadImage(g_hinst, path, IMAGE_ICON, 0, 0,
                        LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_LOADTRANSPARENT));
          g_icons[i] =
              tmp ? tmp : LoadIcon(g_hinst, MAKEINTRESOURCE(TRAY_OFF + i));
        }
        return;
      }
    }
  }
  // Fallback to internal icons.
  for (size_t i = 0; i < ICONS_COUNT; i++)
    g_icons[i] = LoadIcon(g_hinst, MAKEINTRESOURCE(TRAY_OFF + i));
}

/////////////////////////////////////////////////////////////////////////////
int InitTray() {
  ScrollLockState =
      GetPrivateProfileInt(L"Input", L"ScrollLockState", 0, inipath);
  LayoutNumber = GetPrivateProfileInt(L"Zones", L"LayoutNumber", 0, inipath);
  MaxLayouts = GetPrivateProfileInt(L"Zones", L"MaxLayouts", 0, inipath);
  MaxLayouts = CLAMP(0, MaxLayouts, 10);
  LayoutNumber = CLAMP(0, LayoutNumber, std::max(0, MaxLayouts - 1));

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
/////////////////////////////////////////////////////////////////////////////
int UpdateTray() {
  int Index = !!IsEnabled();
  if (Index) {
    Index += (ScrollLockState & 1) &&
             !(!(GetKeyState(VK_SCROLL) & 1) ^ !(ScrollLockState & 2));
    if (GetProp(g_hwnd, APP_ASONOFF)) Index = 2;
  }
  // Load info tool tip and tray icon
  lstrcpy_s(tray.szTip, std::size(tray.szTip), tray_tooltipstr[Index]);
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
/////////////////////////////////////////////////////////////////////////////
int RemoveTray() {
  LOG("RemoveTray() tray_added=%d", static_cast<int>(tray_added));
  if (!tray_added) return 1;

  if (!Shell_NotifyIcon(NIM_DELETE, reinterpret_cast<PNOTIFYICONDATA>(&tray)))
    return 1;

  // Success
  tray_added = 0;
  return 0;
}

/////////////////////////////////////////////////////////////////////////////
// Zones functions
void WriteCurrentLayoutNumber() {
  if (MaxLayouts) {
    wchar_t txt[kUintDigits + 1];
    WritePrivateProfileString(L"Zones", L"LayoutNumber",
                              Uint2lStr(txt, LayoutNumber), inipath);
  }
}
wchar_t* RectToStr(const RECT* rc, wchar_t rectstr[kIntDigits * 4 + 4 + 1]) {
  wchar_t txt[kIntDigits + 1];
  const long* RC = reinterpret_cast<const long*>(rc);
  rectstr[0] = '\0';
  for (size_t i = 0; i < 4; i++) {
    lstrcat_s(rectstr, 64, Int2lStr(txt, static_cast<int>(RC[i])));
    lstrcat_s(rectstr, 64, L",");
  }
  return rectstr;
}
// Save a rect as a string in a Zone<num> entry in the inifile
static void SaveZone(const RECT* rc, unsigned num) {
  wchar_t txt[64], name[32];
  WritePrivateProfileString(L"Zones", ZidxToZonestr(LayoutNumber, num, name),
                            RectToStr(rc, txt), inipath);
}
static void ClearAllZones() {
  int i = 0;
  wchar_t txt[128], name[32];
  for (i = 0; i < 2048; i++) {
    ZidxToZonestr(LayoutNumber, i, name);
    if (GetPrivateProfileString(L"Zones", name, L"", txt, std::size(txt),
                                inipath)) {
      WritePrivateProfileString(L"Zones", name, L"", inipath);
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

void catFullLayoutName(wchar_t* txt, size_t len, int laynum) {
  wchar_t n1[kUintDigits + 1];
  lstrcat_s(txt, len, l10n->MenuSnapLayout);
  lstrcat_s(txt, len, Uint2lStr(n1, laynum + 1));
  if (G_HooksWinProc) {
    DWORD rez = 0;
    if ((rez = G_HooksWinProc(g_hwnd, WM_GETLAYOUTREZ, laynum, 0))) {
      // Add (width:height) to label the layout.
      lstrcat_s(txt, len, L"  (");
      lstrcat_s(txt, len, Uint2lStr(n1, LOWORD(rez)));
      lstrcat_s(txt, len, L":");
      lstrcat_s(txt, len, Uint2lStr(n1, HIWORD(rez)));
      lstrcat_s(txt, len, L")");
    } else {
      lstrcat_s(txt, len, L"  ");
      lstrcat_s(txt, len, l10n->MenuEmptyZone);  // (empty)
    }
  } else {
    lstrcat_s(txt, len, L"  (...)");
  }
}

/////////////////////////////////////////////////////////////////////////////
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
    int i = 0;
    wchar_t txt[128];
    for (i = 0; i < MaxLayouts; i++) {
      txt[0] = '\0';
      catFullLayoutName(txt, std::size(txt), i);
      // Check the current layout We use a simple checkmark,
      // because a radio button is more complex to setup.
      UINT mfflags =
          i == LayoutNumber ? MF_STRING | MF_CHECKED : MF_STRING | MF_UNCHECKED;
      AppendMenu(menu, mfflags, CMD_SNAPLAYOUT + i, txt);
    }

    if (!(UseZones & 2)) {
      wchar_t numstr[kIntDigits + 1];
      lstrcpy_s(txt, std::size(txt), l10n->MenuEditLayout);
      lstrcat_s(txt, std::size(txt), L" ");
      lstrcat_s(txt, std::size(txt), Int2lStr(numstr, LayoutNumber + 1));
      AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenu(menu, MF_STRING, CMD_TESTWIN, l10n->AdvancedTestWindow);
      AppendMenu(menu, MF_STRING, CMD_EDITLAYOUT, txt);
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
