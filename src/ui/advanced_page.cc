// advanced page (split out of config.cc).

#include "ui/advanced_page.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "app/altsnap.h"
#include "app/languages.h"
#include "app/tray.h"
#include "common/ini_section.h"
#include "hooks/hooks.h"
#include "resource.h"
#include "ui/dialog_util.h"
#include "ui/test_window.h"

namespace altsnap::app {

INT_PTR CALLBACK AdvancedPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam) {
  static const DialogOption optlst[] = {
      {IDC_AUTOREMAXIMIZE, T_BOL, 0, L"Advanced", "AutoRemaximize", 0},
      {IDC_AEROTOPMAXIMIZES, T_BMK, 0, L"Advanced", "AeroTopMaximizes",
       1},  // bit 0
      {IDC_AERODBCLICKSHIFT, T_BMK, 1, L"Advanced", "AeroTopMaximizes",
       1},  // bit 1
      {IDC_MULTIPLEINSTANCES, T_BOL, 0, L"Advanced", "MultipleInstances", 0},
      {IDC_FULLSCREEN, T_BOL, 0, L"Advanced", "FullScreen", 1},
      {IDC_BLMAXIMIZED, T_BOL, 0, L"Advanced", "BLMaximized", 0},
      {IDC_FANCYZONE, T_BOL, 0, L"Zones", "FancyZone", 0},
      {IDC_NORESTORE, T_BMK, 2, L"General", "SmartAero", 0},        // bit 2
      {IDC_MAXWITHLCLICK, T_BMK, 0, L"General", "MMMaximize", 1},   // bit 0
      {IDC_RESTOREONCLICK, T_BMK, 1, L"General", "MMMaximize", 0},  // bit 1
      {IDC_TOPMOSTINDICATOR, T_BOL, 0, L"Advanced", "TopmostIndicator", 0},

      {IDC_CENTERFRACTION, T_STR, 0, L"General", "CenterFraction", L"24"},
      {IDC_SIDESFRACTION, T_STR, 0, L"General", "SidesFraction", L"255"},
      {IDC_AEROHOFFSET, T_STR, 0, L"General", "AeroHoffset", L"50"},
      {IDC_AEROVOFFSET, T_STR, 0, L"General", "AeroVoffset", L"50"},
      {IDC_SNAPTHRESHOLD, T_STR, 0, L"Advanced", "SnapThreshold", L"20"},
      {IDC_AEROTHRESHOLD, T_STR, 0, L"Advanced", "AeroThreshold", L"5"},
      {IDC_SNAPGAP, T_STR, 0, L"Advanced", "SnapGap", L"0"},
      {IDC_AEROSPEED, T_STR, 0, L"Advanced", "AeroMaxSpeed", L""},
      {IDC_AEROSPEEDTAU, T_STR, 0, L"Advanced", "AeroSpeedTau", L"32"},
      {IDC_MOVETRANS, T_STR, 0, L"General", "MoveTrans", L""},
  };

  //    static HWND testwnd=NULL;
  static int have_to_apply = 0;
  if (msg == WM_INITDIALOG) {
    ReadDialogOptions(hwnd, optlst, std::size(optlst));
#ifndef _WIN64
    EnableDlgItem(hwnd, IDC_FANCYZONE, 0);
#endif

  } else if (msg == WM_COMMAND) {
    int id = LOWORD(wParam);
    int event = HIWORD(wParam);

    if (id != IDC_TESTWINDOW && (event == 0 || event == EN_UPDATE)) {
      PropSheet_Changed(g_cfgwnd, hwnd);
      have_to_apply = 1;
    }
    if (id == IDC_TESTWINDOW) {  // Click on the Test Window button
      NewTestWindow();
    }
  } else if (msg == WM_NOTIFY) {
    auto pnmh = reinterpret_cast<LPNMHDR>(lParam);
    if (pnmh->code == PSN_SETACTIVE) {
      // Update text
      static const dialogstring_t strlst[] = {
          {IDC_METRICS_BOX, L10nId::AdvancedMetricsBox},
          {IDC_CENTERFRACTION_H, L10nId::AdvancedCenterFraction},
          {IDC_AEROHOFFSET_H, L10nId::AdvancedAeroHoffset},
          {IDC_AEROVOFFSET_H, L10nId::AdvancedAeroVoffset},
          {IDC_SNAPTHRESHOLD_H, L10nId::AdvancedSnapThreshold},
          {IDC_AEROTHRESHOLD_H, L10nId::AdvancedAeroThreshold},
          {IDC_SNAPGAP_H, L10nId::AdvancedSnapGap},
          {IDC_AEROSPEED_H, L10nId::AdvancedAeroSpeed},
          {IDC_MOVETRANS_H, L10nId::AdvancedMoveTrans},
          {IDC_TESTWINDOW, L10nId::AdvancedTestWindow},

          {IDC_BEHAVIOR_BOX, L10nId::AdvancedBehaviorBox},
          {IDC_MULTIPLEINSTANCES, L10nId::AdvancedMultipleInstances},
          {IDC_AUTOREMAXIMIZE, L10nId::AdvancedAutoRemaximize},
          {IDC_AEROTOPMAXIMIZES, L10nId::AdvancedAeroTopMaximizes},
          {IDC_AERODBCLICKSHIFT, L10nId::AdvancedAeroDBClickShift},
          {IDC_MAXWITHLCLICK, L10nId::AdvancedMaxWithLClick},
          {IDC_RESTOREONCLICK, L10nId::AdvancedRestoreOnClick},
          {IDC_FULLSCREEN, L10nId::AdvancedFullScreen},
          {IDC_BLMAXIMIZED, L10nId::AdvancedBLMaximized},
          {IDC_FANCYZONE, L10nId::AdvancedFancyZone},
          {IDC_NORESTORE, L10nId::AdvancedNoRestore},
          {IDC_TOPMOSTINDICATOR, L10nId::AdvancedTopmostIndicator},
      };
      UpdateDialogStrings(hwnd, strlst, std::size(strlst));

    } else if (pnmh->code == PSN_APPLY && have_to_apply) {
      // Apply or OK button was pressed.
      // Save settings
      WriteDialogOptions(hwnd, optlst, std::size(optlst));
      UpdateSettings();
      // Update Test windows in if open.
      EnumThreadWindows(GetCurrentThreadId(), RefreshTestWin, 0);
      have_to_apply = 0;
    }
  }
  return FALSE;
}
}  // namespace altsnap::app
