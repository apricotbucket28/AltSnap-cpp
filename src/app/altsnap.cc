/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright (C) 2015    Stefan Sundin                                   *
 * This program is free software: you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation, either version 3 or later.              *
 * Modified By Raymond Gillibert in 2021                                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "app/altsnap.h"

#include "app/languages.h"
#include "app/tray.h"
#include "hooks/hooks.h"
#include "ui/config.h"

namespace altsnap::app {

// Boring stuff
HINSTANCE g_hinst = nullptr;
HWND g_hwnd = nullptr;
UINT WM_TASKBARCREATED = 0;
wchar_t inipath[MAX_PATH];

WNDPROC G_HooksWinProc = nullptr;

// Cool stuff
HINSTANCE hinstDLL = nullptr;
DWORD ACMenuItems = -1;
unsigned char elevated = 0;
unsigned char ScrollLockState = 0;
char SnapGap = 0;

static HINSTANCE LoadHooksDLL() {
  // Load library
  wchar_t path[MAX_PATH];
  DWORD ret = GetModuleFileName(nullptr, path, std::size(path));
  if (!ret || ret == std::size(path)) return nullptr;
  PathRemoveFileSpecL(path);
  AppendString(path, std::size(path), L"\\hooks.dll");
  return LoadLibrary(path);
}
static void FreeHooksDLL() {
  if (hinstDLL) {
    BOOL ret = FreeLibrary(hinstDLL);
    if (!ret) LOG("FreeLibrary() FAILED!!!");
    hinstDLL = nullptr;
  }
}
/////////////////////////////////////////////////////////////////////////////
int HookSystem() {
  if (G_HooksWinProc) return 1;  // System already hooked
  LOG("Going to Hook the system...");

  if (!hinstDLL) {
    hinstDLL = LoadHooksDLL();
    if (!hinstDLL) {
      LOG("Could not load HOOKS.DLL!!!");
      return 1;
    }
  }
  WNDPROC(WINAPI * Load)
  (HWND, const wchar_t*) =
      reinterpret_cast<WNDPROC(WINAPI*)(HWND, const wchar_t*)>(
          GetProcAddress(hinstDLL, kLoadProcName));
  if (Load) {
    G_HooksWinProc = Load(g_hwnd, inipath);
  }

  LOG("HOOKS.DLL Loaded");

  // Reading some config options...
  UseZones = GetPrivateProfileInt(L"Zones", L"UseZones", 0, inipath);
  SnapGap = CLAMP(
      -128, GetPrivateProfileInt(L"Advanced", L"SnapGap", 0, inipath), 127);
  ACMenuItems = GetPrivateProfileInt(L"Advanced", L"ACMenuItems", -1, inipath);
  UpdateTray();
  return 0;
}
/////////////////////////////////////////////////////////////////////////////
int UnhookSystem() {
  if (G_HooksWinProc == nullptr) return 0;
  LOG("Going to UnHook the system...");

  // Tell dll file that we are unloading
  void(WINAPI * Unload)() = reinterpret_cast<void(WINAPI*)()>(
      GetProcAddress(hinstDLL, kUnloadProcName));
  if (Unload) {
    Unload();
  }
  // FreeHooksDLL();
  // Zero out the message hwnd from DLL.
  G_HooksWinProc = nullptr;

  // Success
  UpdateTray();
  return 0;
}
/////////////////////////////////////////////////////////////////////////////
static void ToggleState() {
  if (IsEnabled()) {
    UnhookSystem();
  } else {
    // SendMessage(g_hwnd, WM_UPDATESETTINGS, 0, 0);
    HookSystem();
  }
}
/////////////////////////////////////////////////////////////////////////////
void UpdateSettings() {
  // PostMessage(g_hwnd, WM_UPDATESETTINGS, 0, 0);
  if (IsEnabled()) {
    UnhookSystem();
    HookSystem();
  }
}
/////////////////////////////////////////////////////////////////////////////
//
static void ShowSClickMenu(HWND hwnd, LPARAM param) {
  POINT pt;
  if (param & LP_CURSORPOS) {
    // Use cursor position to place menu.
    GetCursorPos(&pt);
  } else {
    // Use the pointed window
    RECT rc;
    HWND clickhwnd =
        reinterpret_cast<HWND>(SendMessage(hwnd, WM_GETCLICKHWND, 0, 0));
    GetWindowRect(clickhwnd, &rc);
    pt.x = rc.left + GetSystemMetricsForWin(SM_CXSIZEFRAME, clickhwnd);
    pt.y = rc.top + GetSystemMetricsForWin(SM_CYSIZEFRAME, clickhwnd) +
           GetSystemMetricsForWin(SM_CYCAPTION, clickhwnd);
  }
  HMENU menu = CreatePopupMenu();
  UCHAR show_oriclick = (param & LP_NOALTACTION) ? AC_ORICLICK : 0xFF;

  // MF_STRING, plus MF_CHECKED if `flag` is set in the action parameter.
  const auto menu_flags = [&](unsigned flag) {
    return static_cast<WORD>(MF_STRING |
                             ((param & flag) ? MF_CHECKED : MF_UNCHECKED));
  };

  const struct {
    UCHAR action;
    WORD mf;
    short l10nidx;
  } mnlst[] = {
      /* hide, action,      MF_FLAG/CHECKED,    menu string */
      {AC_ALWAYSONTOP, menu_flags(LP_TOPMOST), L10nId::InputActionAlwaysOnTop},
      {AC_BORDERLESS, menu_flags(LP_BORDERLESS), L10nId::InputActionBorderless},
      {AC_CENTER, MF_STRING, L10nId::InputActionCenter},
      {AC_CENTER2, MF_STRING, L10nId::InputActionCenter2},
      {AC_ROLL, menu_flags(LP_ROLLED), L10nId::InputActionRoll},
      {AC_LOWER, MF_STRING, L10nId::InputActionLower},
      {AC_MAXHV, MF_STRING, L10nId::InputActionMaximizeHV},
      {AC_MINALL, MF_STRING, L10nId::InputActionMinAllOther},
      {AC_SIDESNAP, MF_STRING, L10nId::InputActionSideSnap},
      {0, MF_SEPARATOR, -1}, /* ------------------------ */
      {AC_MAXIMIZE, menu_flags(LP_MAXIMIZED), L10nId::InputActionMaximize},
      {AC_MINIMIZE, MF_STRING, L10nId::InputActionMinimize},
      {AC_CLOSE, MF_STRING, L10nId::InputActionClose},
      {0, MF_SEPARATOR, -1}, /* ------------------------ */
      {AC_KILL, MF_STRING, L10nId::InputActionKill},
      {0, MF_SEPARATOR, -1}, /* ------------------------ */
      {AC_MOVEONOFF, menu_flags(LP_MOVEONOFF), L10nId::InputActionMoveOnOff},
      {0, MF_SEPARATOR, -1}, /* ------------------------ */
      {show_oriclick, MF_STRING, L10nId::InputActionOriClick},
      {AC_NONE, MF_STRING, L10nId::InputActionNothing},
  };
#undef CHK
#undef K
  size_t i = 0;
  for (i = 0; i < std::size(mnlst); i++) {
    if ((ACMenuItems >> i) & 1 &&
        mnlst[i].action != 0xFF) {  // Put the action in the HIWORD of wParam
      const wchar_t* str =
          mnlst[i].l10nidx >= 0 ? l10n->Get(mnlst[i].l10nidx) : nullptr;
      AppendMenu(menu, mnlst[i].mf, mnlst[i].action << 16, str);
    }
  }
  TrackPopupMenu(menu, GetSystemMetrics(SM_MENUDROPALIGNMENT), pt.x, pt.y, 0,
                 hwnd, nullptr);
  DestroyMenu(menu);
  PostMessage(hwnd, WM_CLOSE, 0, 0);
}
// To get the caret position in screen coordinate.
// We first try to get the carret rect
// #include <oleacc.h>
// static const GUID  my_IID_IAccessible = { 0x618736e0, 0x3c3d, 0x11cf, {0x81,
// 0x0c, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71} };
static void GetKaretPos(POINT* pt) {
  GUITHREADINFO gui;
  gui.cbSize = sizeof(gui);
  gui.hwndCaret = nullptr;
  if (GetGUIThreadInfo(0, &gui)) {
    pt->x = (gui.rcCaret.right + gui.rcCaret.left) >> 1;
    pt->y = (gui.rcCaret.top + gui.rcCaret.bottom) >> 1;
    if (gui.hwndCaret) {
      ClientToScreen(gui.hwndCaret, pt);
      return;
      //        } else if (gui.hwndFocus) {
      //            IAccessible *pacc = NULL;
      //            if ( S_OK==AccessibleObjectFromWindow(gui.hwndFocus,
      //            OBJID_CARET, &my_IID_IAccessible, (void**)&pacc) ) {
      //        //        MessageBox(NULL, NULL, NULL, 0);
      //                LONG x=0, y=0, w=0, h=0;
      //                VARIANT varCaret;
      //                varCaret.vt = VT_I4;
      //                varCaret.lVal = CHILDID_SELF;
      //                if (S_OK == pacc->lpVtbl->accLocation(pacc, &x, &y, &w,
      //                &h, varCaret)) {
      //                    pt->x = x+w/2;
      //                    pt->y = y+h/2;
      //                    pacc->lpVtbl->Release(pacc);
      //                    return;
      //                }
      //            }
    }
  }

  GetCursorPos(pt);
}
static void ShowUnikeyMenu(HWND hwnd, LPARAM param) {
  UCHAR vkey = LOBYTE(LOWORD(param));
  UCHAR capital = HIBYTE(LOWORD(param));
  wchar_t const* const* const ukmap = &l10n->a;  // EXTRAKEYS_MAP;
  HMENU menu = CreatePopupMenu();
  if (!menu) return;

  const wchar_t *kl = nullptr, *keylist = ukmap[(vkey - 0x41) * 2];
  UCHAR i = 0;
  for (kl = keylist, i = 'A'; *kl; kl++) {
    if (*kl == L'%') {
      AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
      continue;
    }
    wchar_t unichar = *kl;
    if (kl[1] == L'|') {
      kl += 2;
      if (capital) unichar = *kl;
    } else if (capital) {
      unichar = static_cast<wchar_t>(reinterpret_cast<LONG_PTR>(
          CharUpper(reinterpret_cast<wchar_t*>(static_cast<LONG_PTR>(*kl)))));
    }
    if (i > 'Z') i = '1';
    wchar_t mwstr[6];
    mwstr[0] = L'&';
    mwstr[1] = i++;
    mwstr[2] = L'\t';
    DWORD utf16c = 0;
    if (IS_SURROGATE_PAIR(unichar, kl[1])) {
      std::memcpy(&utf16c, kl, sizeof(utf16c));  // Two UTF-16 units.
      mwstr[3] = LOWORD(utf16c);
      mwstr[4] = HIWORD(utf16c);
      mwstr[5] = L'\0';
      kl++;  // skip high surrogate
    } else {
      mwstr[3] = utf16c = unichar;
      mwstr[4] = L'\0';
    }
    if (!AppendMenu(menu, MF_STRING, utf16c, mwstr)) break;
  }
  if (kl > keylist) {
    POINT pt;
    GetKaretPos(&pt);
    TrackPopupMenu(menu, GetSystemMetrics(SM_MENUDROPALIGNMENT), pt.x, pt.y, 0,
                   hwnd, nullptr);
  }
  DestroyMenu(menu);
}
/////////////////////////////////////////////////////////////////////////////
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam) {
  if (!msg) {
    // In case some messages are not registered.
  } else if (wParam && msg == WM_ERASEBKGND) {
    return 1;
  } else if (wParam && (msg == WM_PAINT || msg == WM_NCPAINT)) {
    return 0;
  } else if (msg == WM_TRAY) {
    if (lParam == WM_LBUTTONDOWN || lParam == WM_LBUTTONDBLCLK) {
      ToggleState();
      if (lParam == WM_LBUTTONDBLCLK) {
        SendMessage(hwnd, WM_OPENCONFIG, 0, 0);
      }
    } else if (lParam == WM_MBUTTONDOWN) {
      SendMessage(hwnd, WM_COMMAND, CMD_OPENINIFILE, 0);
    } else if (lParam == WM_RBUTTONUP) {
      ShowContextMenu(hwnd);
    }
  } else if (msg == WM_SCLICK && wParam) {
    ShowSClickMenu(reinterpret_cast<HWND>(wParam), lParam);
  } else if (msg == WM_UNIKEYMENU) {
    ShowUnikeyMenu(reinterpret_cast<HWND>(wParam), lParam);
  } else if (msg == WM_UPDATESETTINGS) {
    // Reload hooks
    UpdateSettings();
  } else if (msg == WM_ADDTRAY) {
    tray_hidden = 0;
    UpdateTray();
  } else if (msg == WM_UPDATETRAY) {
    UpdateTray();
  } else if (msg == WM_HIDETRAY) {
    tray_hidden = 1;
    RemoveTray();
  } else if (msg == WM_OPENCONFIG && (lParam || !tray_hidden)) {
    OpenConfig(wParam);
  } else if (msg == WM_CLOSECONFIG) {
    CloseConfig();
  } else if (msg == WM_TASKBARCREATED) {
    // Try to add the tray icon because explorer started.
    tray_added = 0;
    UpdateTray();
  } else if (msg == WM_COMMAND) {
    int wmId = LOWORD(wParam);  // int wmEvent = HIWORD(wParam);
    if (wmId == CMD_TOGGLE) {
      ToggleState();
    } else if (wmId == CMD_HIDE) {
      tray_hidden = 1;
      RemoveTray();
    } else if (wmId == CMD_ELEVATE) {
      ElevateNow(0);
    } else if (wmId == CMD_CONFIG) {
      SendMessage(hwnd, WM_OPENCONFIG, 0, 0);
    } else if (wmId == CMD_ABOUT) {
      SendMessage(hwnd, WM_OPENCONFIG, 5, 0);
    } else if (wmId == CMD_OPENINIFILE) {
      ShellExecute(nullptr, L"open", inipath, nullptr, nullptr, SW_SHOWNORMAL);
    } else if (wmId == CMD_EXIT) {
      DestroyWindow(hwnd);
    } else if (wmId == CMD_SAVEZONES) {
      wchar_t txt[256];
      CopyString(txt, std::size(txt), l10n->MiscZoneConfirmation);
      AppendString(txt, std::size(txt), L"\n\n");
      AppendFullLayoutName(txt, std::size(txt), LayoutNumber);
      int ret = MessageBox(nullptr, txt, APP_NAME, MB_OKCANCEL);
      if (ret == IDOK) {
        UnhookSystem();
        SaveCurrentLayout();
        WriteCurrentLayoutNumber();
        HookSystem();
      }
    } else if (wmId == CMD_CLOSEZONES) {
      CloseAllTestWindows();
    } else if (wmId == CMD_TESTWIN) {
      NewTestWindow();
    } else if (CMD_SNAPLAYOUT <= wmId && wmId <= CMD_SNAPLAYOUTEND) {
      // Inform hooks.dll that the snap layout changed
      LayoutNumber = wmId - CMD_SNAPLAYOUT;
      if (G_HooksWinProc)
        G_HooksWinProc(hwnd, WM_SETLAYOUTNUM, LayoutNumber, 0);
      // Save new value in the .ini file
      WriteCurrentLayoutNumber();
    } else if (wmId == CMD_EDITLAYOUT) {
      if (G_HooksWinProc) {
        RECT* zones = nullptr;
        unsigned len = G_HooksWinProc(hwnd, WM_GETZONES, LayoutNumber,
                                      reinterpret_cast<LPARAM>(&zones));
        if (!len || !zones) {
          // Empty layout, Let's open a new Test Window
          return !NewTestWindow();
        }
        // Open them from bottom to top to ensure
        // the windows are in the correct order.
        while (len--) {
          const RECT* rc = &zones[len];
          NewTestWindowAt(rc->left, rc->top, rc->right - rc->left,
                          rc->bottom - rc->top);
        }
      }
    }
  } else if (msg == WM_QUERYENDSESSION) {
    UnhookSystem();
  } else if (msg == WM_DESTROY) {
    UnhookSystem();
    RemoveTray();
    PostQuitMessage(0);
  } else if (msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN ||
             msg == WM_RBUTTONDOWN) {
    // Hide cursorwnd if clicked on, this might happen if
    // it wasn't hidden by hooks.c for some reason
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  } else if (msg == WM_DISPLAYCHANGE ||
             (msg == WM_SETTINGCHANGE && wParam == SPI_SETWORKAREA)) {
    LOG("WM_DISPLAYCHANGE %d:%d, %dbpp in WindowProc", LOWORD(lParam),
        HIWORD(lParam), static_cast<int>(wParam));
    if (G_HooksWinProc) {
      int bestlayout = G_HooksWinProc(hwnd, WM_GETBESTLAYOUT, 0, 0);
      if (bestlayout != LayoutNumber && 0 <= bestlayout &&
          bestlayout < MaxLayouts) {
        LayoutNumber = bestlayout;
        G_HooksWinProc(hwnd, WM_SETLAYOUTNUM, LayoutNumber, 0);
      }
    }
  } else if (msg == WM_HOTKEY || msg == WM_STACKLIST) {
    if (G_HooksWinProc) return G_HooksWinProc(hwnd, msg, wParam, lParam);
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}
/////////////////////////////////////////////////////////////////////////////

}  // namespace altsnap::app
using namespace altsnap::app;

static bool IsDigit(wchar_t c) {
  return L'0' <= c && c <= L'9';
}

// Returns the part of the process command line that follows the program name.
// WinMain() only receives the command line as ANSI text, which would lose any
// non-ASCII characters of a Unicode build (e.g. in the "-l" or "-a" values).
static const wchar_t* GetCommandLineParams() {
  const wchar_t* p = GetCommandLine();
  if (*p == L'"') {  // Quoted program name: ends at the next quote.
    for (++p; *p && *p != L'"';) ++p;
    if (*p) ++p;
  } else {  // Unquoted: ends at the first whitespace.
    while (*p && *p != L' ' && *p != L'\t') ++p;
  }
  while (*p == L' ' || *p == L'\t') ++p;
  return p;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*ansi_params*/, int iCmdShow) {
  g_hinst = hInst;
  const wchar_t* const params = GetCommandLineParams();

  // Get ini path
  LOG("\n\nALTSNAP STARTED");
  GetModuleFileName(nullptr, inipath, std::size(inipath));
  inipath[MAX_PATH - 1] = '\0';
  size_t inipath_len = lstrlen(inipath) - 3;
  CopyString(&inipath[inipath_len], 4, L"ini");
  if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(inipath) &&
      GetEnvironmentVariable(L"APPDATA", nullptr, 0)) {
    // .ini file is not in current directorry, and APPDATA exists
    // we should look for %APPDATA%\AltSnap\AltSnap.ini
    wchar_t userini[MAX_PATH];
    GetEnvironmentVariable(L"APPDATA", userini, std::size(userini));
    AppendString(userini, std::size(userini), L"\\AltSnap");
    if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(userini)) {
      CreateDirectory(userini, nullptr);
      LOG("CreateDirectory(%ls)", userini);
    }
    // Full user ini name.
    // AppendString(userini, std::size(userini), L"\\AltSnap.ini");
    const wchar_t* inifilename = inipath + inipath_len - 3;
    while (inifilename >= inipath && *inifilename != '\\') inifilename--;
    AppendString(userini, std::size(userini), inifilename);
    if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(userini)) {
      // Copy AltSnap.dni (Default ini file) if no ini present
      CopyString(&inipath[lstrlen(inipath) - 3], 4, L"dni");
      CopyFile(inipath, userini, FALSE);  // AltSnap.dni -> AltSnap.ini
      LOG("CopyFile(%ls -> %ls)", inipath, userini);
    }

    CopyString(inipath, std::size(inipath), userini);
  }
  LOG("ini file: %ls", inipath);

  // Read parameters on command line
  int help = !!FindSubstring(params, L"-help") ||
             !!FindSubstring(params, L"/?") || !!FindSubstring(params, L"-?");
  if (help) {
    static const wchar_t* txthelp =
        L"AltSnap command line options:\n\n"
        L"--help\tShow this help!\n"
        L"-h\tHide the tray icon\n"
        L"-q\tQuiet mode\n"
        L"-m\tMultiple instances allowed\n"
        L"-c\tOpen Config dialog\n"
        L"-e\tElevate AltSnap\n"
        L"-r\tRelaod AltSnap settings\n"
        L"-lX\tSelect Snap Layout number X\n"
        L"-afX\tExecute action X for the foreground window\n"
        L"-apX\tExecute action X for the pointed window\n";

    MessageBox(nullptr, txthelp, APP_NAME L" Usage",
               MB_OK | MB_ICONINFORMATION);
    return 0;
  }

  tray_hidden = !!FindSubstring(params, L"-h");
  int quiet = !!FindSubstring(params, L"-q");
  int elevate = !!FindSubstring(params, L"-e");
  int multi = !!FindSubstring(params, L"-m");
  int config = !!FindSubstring(params, L"-c");
  int rlini = !!FindSubstring(params, L"-r");

  // Check whether the process is already elevated.
  {
    HANDLE token = nullptr;
    TOKEN_ELEVATION elevation = {0};
    DWORD len = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &token) &&
        GetTokenInformation(token, TokenElevation, &elevation,
                            sizeof(elevation), &len)) {
      elevated = elevation.TokenIsElevated;
    }
    if (token) CloseHandle(token);
    LOG("Process started %s elevated", elevated ? "already" : "non");
  }
  LOG("Command line parameters read, hide=%d, quiet=%d, elevate=%d, multi=%d, "
      "config=%d",
      tray_hidden, quiet, elevate, multi, config);

  // Look for previous instance
  if (!multi &&
      !GetPrivateProfileInt(L"Advanced", L"MultipleInstances", 0, inipath)) {
    if (quiet) return 0;

    HWND previnst = FindWindow(APP_NAME, L"");
    if (previnst) {
      // Ask old HotKey window to perform an action.
      const wchar_t* actionstr = FindSubstring(params, L"-a");
      if (actionstr && actionstr[2] && actionstr[3] && actionstr[4]) {
        action_t action = MapActionW(&actionstr[3]);
        PostMessage(previnst, WM_HOTKEY,
                    (actionstr[2] == 'p') * 0x1000 + action.ac,
                    PackAction(action));
        return 0;
      }
      // Change layout if asked...
      const wchar_t* layout = FindSubstring(params, L"-l");
      if (layout && IsDigit(layout[2])) {
        wchar_t numstr[3] = {layout[2], layout[3], L'\0'};
        if (!IsDigit(numstr[1])) numstr[1] = L'\0';
        // MessageBox(NULL, NULL, NULL, 0);
        int layoutnumber = CLAMP(0, ParseInt(numstr) - 1, 9);
        PostMessage(previnst, WM_COMMAND, CMD_SNAPLAYOUT + layoutnumber, 0);
        return 0;
      }
      // Update old instance if no action to be made.
      LOG("Previous instance found and no -multi mode");
      if (tray_hidden) PostMessage(previnst, WM_CLOSECONFIG, 0, 0);
      if (config) PostMessage(previnst, WM_OPENCONFIG, 0, 0);
      if (rlini) PostMessage(previnst, WM_UPDATESETTINGS, 0, 0);
      PostMessage(previnst, tray_hidden ? WM_HIDETRAY : WM_ADDTRAY, 0, 0);
      LOG("Updated old instance and NORMAL EXIT");
      return 0;
    }
    LOG("No previous instance found");
  }

  // Check AlwaysElevate
  if (!elevated) {
    if (!elevate)
      elevate = GetPrivateProfileInt(L"Advanced", L"AlwaysElevate", 0, inipath);

    // Handle request to elevate to administrator privileges
    if (elevate) {
      LOG("Elevation requested");
      wchar_t path[MAX_PATH];
      GetModuleFileName(nullptr, path, std::size(path));
      HINSTANCE ret =
          ShellExecute(nullptr, L"runas", path, (tray_hidden ? L"-h" : nullptr),
                       nullptr, SW_SHOWNORMAL);
      if (reinterpret_cast<DorQWORD>(ret) > 32) {
        LOG("Elevation Faild => Not cool NORMAL EXIT");
        return 0;
      }
      LOG("Elevation sucess");
    } else {
      LOG("No Elevation requested");
    }
  }
  // Language
  UpdateLanguage();
  LOG("Language updated");

  // Create window
  WNDCLASS wnd = {0,       WindowProc, 0,       0,       hInst,
                  nullptr, nullptr,    nullptr, nullptr, APP_NAME};
  RegisterClass(&wnd);
  g_hwnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
                          wnd.lpszClassName, nullptr, WS_POPUP, 0, 0, 0, 0,
                          nullptr, nullptr, hInst, nullptr);
  LOG("Create main APP Window: %s", g_hwnd ? "Sucess" : "Failed");
  if (elevated) {
    // AltSnap was started elevated!
    // Allow some messages to be sent from non-elevated instance
    // so that user can do AltSnap.exe -c/r/h/l
    UINT i = 0;
    for (i = WM_UPDATETRAY; i <= WM_HIDETRAY; i++)
      ChangeWindowMessageFilterEx(g_hwnd, i, /*MSGFLT_ALLOW*/ 1, nullptr);
  }
  // Tray icon

  InitTray();
  UpdateTray();

  // Hook system
  HookSystem();

  // Add tray if hook failed, even though -hide was supplied
  if (tray_hidden && !G_HooksWinProc) {
    tray_hidden = 0;
    UpdateTray();
  }
  // Open config if -config was supplied
  if (config) {
    PostMessage(g_hwnd, WM_OPENCONFIG, 0, 0);
  }
  // Message loop
  LOG("Starting " APP_NAMEA " message loop...");
  BOOL ret = 0;
  MSG msg;
  while ((ret = GetMessage(&msg, nullptr, 0, 0)) != 0) {
    if (ret == -1) {
      break;
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  UnhookSystem();
  FreeHooksDLL();
  DestroyWindow(g_hwnd);
  LOG("GOOD NORMAL EXIT");
  FreeAllLangRelated();

  return static_cast<int>(msg.wParam);
}
