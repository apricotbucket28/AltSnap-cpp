// general page (split out of config.cc).

#include "ui/general_page.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "app/altsnap.h"
#include "app/languages.h"
#include "app/settings_service.h"
#include "app/tray.h"
#include "common/ini_section.h"
#include "hooks/hooks.h"
#include "resource.h"
#include "ui/dialog_util.h"
#include "ui/test_window.h"

namespace altsnap::app {

// No error reporting since we don't want the user to be interrupted
static void CheckAutostart(int* _on, int* _hidden, int* _elevated) {
  *_on = *_hidden = *_elevated = 0;
  // Read registry
  HKEY key = nullptr;
  wchar_t value[MAX_PATH + 20];
  value[0] = L'\0';
  DWORD len = sizeof(value);
  RegOpenKeyEx(HKEY_CURRENT_USER,
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
               KEY_QUERY_VALUE, &key);
  RegQueryValueEx(key, APP_NAME, nullptr, nullptr,
                  reinterpret_cast<LPBYTE>(value), &len);
  RegCloseKey(key);

  // Compare to what it should be
  wchar_t compare[MAX_PATH + 20];
  GetModuleFileName(nullptr, &compare[1], MAX_PATH);
  compare[0] = '\"';
  unsigned ll = lstrlen(compare);
  compare[ll] = '\"';
  compare[++ll] = '\0';

  if (FindSubstring(value, compare) != value) {
    return;
  }
  // Autostart is on, check arguments
  *_on = 1;
  if (FindSubstring(value, L" -hide") != nullptr) {
    *_hidden = 1;
  }
  if (FindSubstring(value, L" -elevate") != nullptr) {
    *_elevated = 1;
  }
}

static void SetAutostart(int on, int hhide, int eelevate) {
  // Open key
  HKEY key = nullptr;
  int error = RegCreateKeyEx(
      HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
      0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
  if (error != ERROR_SUCCESS) return;
  if (on) {
    // Get path
    wchar_t value[MAX_PATH + 20];
    GetModuleFileName(nullptr, &value[1], MAX_PATH);
    value[0] = '\"';
    unsigned ll = lstrlen(value);
    value[ll] = '\"';
    value[++ll] = '\0';
    // Add -hide or -elevate flags
    if (hhide) AppendString(value, std::size(value), L" -hide");
    if (eelevate) AppendString(value, std::size(value), L" -elevate");
    // Set autostart
    RegSetValueEx(key, APP_NAME, 0, REG_SZ, reinterpret_cast<LPBYTE>(value),
                  (lstrlen(value) + 1) * sizeof(value[0]));
  } else {
    // Remove
    RegDeleteValue(key, APP_NAME);
  }
  // Close key
  RegCloseKey(key);
}

BOOL ElevateNow(int showconfig) {
  wchar_t path[MAX_PATH];
  GetModuleFileName(nullptr, path, std::size(path));
  INT_PTR ret = 0;
  if (showconfig)
    ret = reinterpret_cast<INT_PTR>(ShellExecute(
        nullptr, L"runas", path, L"-config -multi", nullptr, SW_SHOWNORMAL));
  else
    ret = reinterpret_cast<INT_PTR>(ShellExecute(
        nullptr, L"runas", path, L"-multi", nullptr, SW_SHOWNORMAL));

  if (ret > 32) {
    PostMessage(g_hwnd, WM_CLOSE, 0, 0);
  } else {
    MessageBox(nullptr, l10n->GeneralElevationAborted, APP_NAME,
               MB_ICONINFORMATION | MB_OK);
  }
  return FALSE;
}

static DWORD IsUACEnabled() {
  DWORD uac_enabled = 0;
  if (elevated) {
    DWORD len = sizeof(uac_enabled);
    HKEY key = nullptr;
    RegOpenKeyEx(
        HKEY_LOCAL_MACHINE,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0,
        KEY_QUERY_VALUE, &key);

    RegQueryValueEx(key, L"EnableLUA", nullptr, nullptr,
                    reinterpret_cast<LPBYTE>(&uac_enabled), &len);
    RegCloseKey(key);
  }
  return uac_enabled;
}

INT_PTR CALLBACK GeneralPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam) {
  // Options to bead or written...
  static const DialogOption optlst[] = {
      // dialog id, type, bit number, section name, option name, def val.
      {IDC_AERO, T_BOL, 0, L"General", "Aero", 1},
      {IDC_SMARTAERO, T_BMK, 0, L"General", "SmartAero", 1},
      {IDC_SMARTERAERO, T_BMK, 1, L"General", "SmartAero", 0},
      {IDC_STICKYRESIZE, T_BOL, 0, L"General", "StickyResize", 1},
      {IDC_INACTIVESCROLL, T_BOL, 0, L"General", "InactiveScroll", 0},
      {IDC_MDI, T_BOL, 0, L"General", "MDI", 1},
      {IDC_RESIZEALL, T_BOL, 0, L"Advanced", "ResizeAll", 1},
      {IDC_USEZONES, T_BMK, 0, L"Zones", "UseZones", 0},
      {IDC_PIERCINGCLICK, T_BOL, 0, L"Advanced", "PiercingClick", 0},
  };

  static const dialogstring_t strlst[] = {
      {IDC_GENERAL_BOX, L10nId::GeneralBox},
      {IDC_AUTOFOCUS, L10nId::GeneralAutoFocus},
      {IDC_AERO, L10nId::GeneralAero},
      {IDC_SMARTAERO, L10nId::GeneralSmartAero},
      {IDC_SMARTERAERO, L10nId::GeneralSmarterAero},
      {IDC_STICKYRESIZE, L10nId::GeneralStickyResize},
      {IDC_INACTIVESCROLL, L10nId::GeneralInactiveScroll},
      {IDC_MDI, L10nId::GeneralMDI},
      {IDC_AUTOSNAP_HEADER, L10nId::GeneralAutoSnap},
      {IDC_LANGUAGE_HEADER, L10nId::GeneralLanguage},
      {IDC_USEZONES, L10nId::GeneralUseZones},
      {IDC_PIERCINGCLICK, L10nId::GeneralPiercingClick},
      {IDC_RESIZEALL, L10nId::GeneralResizeAll},
      {IDC_RESIZECENTER, L10nId::GeneralResizeCenter},
      {IDC_RZCENTER_NORM, L10nId::GeneralResizeCenterNorm},
      {IDC_RZCENTER_BR, L10nId::GeneralResizeCenterBr},
      {IDC_RZCENTER_MOVE, L10nId::GeneralResizeCenterMove},
      {IDC_RZCENTER_CLOSE, L10nId::GeneralResizeCenterClose},
      {IDC_AUTOSTART_BOX, L10nId::GeneralAutostartBox},
      {IDC_AUTOSTART, L10nId::GeneralAutostart},
      {IDC_AUTOSTART_HIDE, L10nId::GeneralAutostartHide},
      {IDC_AUTOSTART_ELEVATE, L10nId::GeneralAutostartElevate}};

  int updatestrings = 0;
  static int have_to_apply = 0;
  static altsnap::settings::Snapshot auto_focus_snapshot;
  if (msg == WM_INITDIALOG) {
    MoveToCorner(g_cfgwnd);
    int ret = 0;
    ReadDialogOptions(hwnd, optlst, std::size(optlst));
    auto_focus_snapshot = settings_service
                              ? settings_service->ReadLegacyAutoFocus()
                              : altsnap::settings::Snapshot{};
    CheckDlgButton(hwnd, IDC_AUTOFOCUS,
                   auto_focus_snapshot.auto_focus ? BST_CHECKED : BST_UNCHECKED);
    have_to_apply = 0;

    ret = GetPrivateProfileInt(L"General", L"ResizeCenter", 1, inipath.c_str());
    ret = ret == 1   ? IDC_RZCENTER_NORM
          : ret == 2 ? IDC_RZCENTER_MOVE
          : ret == 3 ? IDC_RZCENTER_CLOSE
                     : IDC_RZCENTER_BR;
    CheckRadioButton(hwnd, IDC_RZCENTER_NORM, IDC_RZCENTER_CLOSE, ret);

    HWND control = GetDlgItem(hwnd, IDC_LANGUAGE);
    ComboBox_ResetContent(control);
    EnableWindow(control, TRUE);

    for (size_t i = 0; i < langinfo.size(); i++) {
      ComboBox_AddString(control, langinfo[i].lang.c_str());
      if (!lstrcmpi(l10n->Code, langinfo[i].code.c_str())) {
        ComboBox_SetCurSel(control, i);
      }
    }
    EnableDlgItem(hwnd, IDC_ELEVATE, !elevated);
    //    } else if (msg == WM_HELP) {
    //        ShowContextHelp(strlst, std::size(strlst), hwnd,
    //        (LPHELPINFO)lParam);
  } else if (msg == WM_COMMAND) {
    int id = LOWORD(wParam);
    int event = HIWORD(wParam);
    int val = IsDlgButtonChecked(hwnd, id);
    if (id == IDC_SMARTAERO) {
      EnableDlgItem(hwnd, IDC_SMARTERAERO, IsChecked(hwnd, IDC_SMARTAERO));
    }

    if (id != IDC_ELEVATE && (event == 0 || event == CBN_SELCHANGE)) {
      PropSheet_Changed(g_cfgwnd, hwnd);
      have_to_apply = 1;
    }

    if (id == IDC_AUTOSTART) {
      EnableDlgItem(hwnd, IDC_AUTOSTART_HIDE, val);
      EnableDlgItem(hwnd, IDC_AUTOSTART_ELEVATE, val);
      if (!val) {
        CheckDlgButton(hwnd, IDC_AUTOSTART_HIDE, BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_AUTOSTART_ELEVATE, BST_UNCHECKED);
      }
    } else if (id == IDC_AUTOSTART_ELEVATE) {
      if (val && IsUACEnabled()) {
        MessageBox(nullptr, l10n->GeneralAutostartElevateTip, APP_NAME,
                   MB_ICONINFORMATION | MB_OK);
      }
    } else if (id == IDC_ELEVATE) {
      return ElevateNow(1);
    }
  } else if (msg == WM_NOTIFY) {
    auto pnmh = reinterpret_cast<LPNMHDR>(lParam);

    if (pnmh->code == PSN_SETACTIVE) {
      updatestrings = 1;

      // Autostart
      int autostart = 0, hidden = 0, eelevated = 0;
      CheckAutostart(&autostart, &hidden, &eelevated);
      CheckDlgButton(hwnd, IDC_AUTOSTART,
                     autostart ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_AUTOSTART_HIDE,
                     hidden ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_AUTOSTART_ELEVATE,
                     eelevated ? BST_CHECKED : BST_UNCHECKED);
      EnableDlgItem(hwnd, IDC_AUTOSTART_HIDE, autostart);
      EnableDlgItem(hwnd, IDC_AUTOSTART_ELEVATE, autostart);
      EnableDlgItem(hwnd, IDC_INACTIVESCROLL,
                    IsChecked(hwnd, IDC_INACTIVESCROLL));
    } else if (pnmh->code == PSN_APPLY && have_to_apply) {
      if (static_cast<bool>(IsChecked(hwnd, IDC_AUTOFOCUS)) != auto_focus_snapshot.auto_focus &&
          (!settings_service || !settings_service->WriteLegacyAutoFocus(
              auto_focus_snapshot, IsChecked(hwnd, IDC_AUTOFOCUS)))) {
        MessageBoxW(hwnd,
                    L"Could not save AutoFocus. The configuration may have changed. "
                    L"Reopen settings, close other AltSnap instances, and check configuration access.",
                    L"AltSnap", MB_OK | MB_ICONERROR);
        SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
        return TRUE;
      }
      // all bool options (checkboxes).
      WriteDialogOptions(hwnd, optlst, std::size(optlst));

      int val = ComboBoxCurSel(hwnd, IDC_AUTOSNAP);
      WritePrivateProfileString(L"General", L"AutoSnap",
                                std::to_wstring(val).c_str(), inipath.c_str());

      val = IsChecked(hwnd, IDC_RZCENTER_NORM)    ? 1
            : IsChecked(hwnd, IDC_RZCENTER_MOVE)  ? 2
            : IsChecked(hwnd, IDC_RZCENTER_CLOSE) ? 3
                                                  : 0;
      WritePrivateProfileString(L"General", L"ResizeCenter",
                                std::to_wstring(val).c_str(), inipath.c_str());

      // Load selected Language
      int i = ComboBoxCurSel(hwnd, IDC_LANGUAGE);
      if (static_cast<size_t>(i) < langinfo.size() &&
          lstrcmpi(l10n->Code, langinfo[i].code.c_str())) {
        LoadTranslation(langinfo[i].fn.empty() ? nullptr
                                               : langinfo[i].fn.c_str());
#ifdef UNICODE
        wchar_t curlang[16];
        GetUserLanguageCode(curlang);
        if (!lstrcmpi(l10n->Code, curlang))  // Use Auto if selected language is
                                             // the current user's one.
          WritePrivateProfileString(L"General", L"Language", L"Auto",
                                    inipath.c_str());
        else
          WritePrivateProfileString(L"General", L"Language", l10n->Code,
                                    inipath.c_str());
#else
        WritePrivateProfileString(L"General", L"Language", l10n->Code,
                                  inipath.c_str());
#endif
        updatestrings = 1;
        UpdateStrings();
      }

      // Autostart
      SetAutostart(IsChecked(hwnd, IDC_AUTOSTART),
                   IsChecked(hwnd, IDC_AUTOSTART_HIDE),
                   IsChecked(hwnd, IDC_AUTOSTART_ELEVATE));

      UpdateSettings();
      // Update Test windows in if open.
      EnumThreadWindows(GetCurrentThreadId(), RefreshTestWin, 0);

      have_to_apply = 0;
      if (settings_service) {
        auto_focus_snapshot = settings_service->ReadLegacyAutoFocus();
      }
    }
  }
  if (updatestrings) {
    // Update text
    UpdateDialogStrings(hwnd, strlst, std::size(strlst));
    // spetial case...
    // CreateToolTip(IDC_AUTOFOCUS, hwnd, L"String\nExample");
    SetDlgItemText(hwnd, IDC_ELEVATE,
                   elevated ? l10n->GeneralElevated : l10n->GeneralElevate);

    // AutoSnap
    HWND control = GetDlgItem(hwnd, IDC_AUTOSNAP);
    ComboBox_ResetContent(control);
    ComboBox_AddString(control, l10n->GeneralAutoSnap0);
    ComboBox_AddString(control, l10n->GeneralAutoSnap1);
    ComboBox_AddString(control, l10n->GeneralAutoSnap2);
    ComboBox_AddString(control, l10n->GeneralAutoSnap3);
    wchar_t txt[8];
    GetPrivateProfileString(L"General", L"AutoSnap", L"0", txt, std::size(txt),
                            inipath.c_str());
    ComboBox_SetCurSel(control, ParseInt(txt));

    // Language
    control = GetDlgItem(hwnd, IDC_LANGUAGE);
    ComboBox_DeleteString(control, langinfo.size());
  }
  return FALSE;
}
}  // namespace altsnap::app
