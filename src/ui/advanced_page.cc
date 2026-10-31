// advanced page (split out of config.cc).

#include "ui/advanced_page.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "app/altsnap.h"
#include "app/languages.h"
#include "app/settings_service.h"
#include "app/tray.h"
#include "common/winutil.h"
#include "hooks/hooks.h"
#include "resource.h"
#include "ui/dialog_util.h"
#include "ui/test_window.h"

namespace altsnap::app {

INT_PTR CALLBACK AdvancedPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam) {
  using namespace altsnap::settings;
  struct Binding {
    int control;
    Option option;
  };
  static constexpr std::array<Binding, 21> bindings{
      {{IDC_AUTOREMAXIMIZE, Option::kAutoRemaximize},
       {IDC_AEROTOPMAXIMIZES, Option::kAeroTopMaximizes},
       {IDC_AERODBCLICKSHIFT, Option::kAeroDoubleClickShift},
       {IDC_MULTIPLEINSTANCES, Option::kMultipleInstances},
       {IDC_FULLSCREEN, Option::kFullScreen},
       {IDC_BLMAXIMIZED, Option::kBLMaximized},
       {IDC_FANCYZONE, Option::kFancyZone},
       {IDC_NORESTORE, Option::kKeepSnapped},
       {IDC_MAXWITHLCLICK, Option::kMaximizeOnClick},
       {IDC_RESTOREONCLICK, Option::kRestoreOnClick},
       {IDC_TOPMOSTINDICATOR, Option::kTopmostIndicator},
       {IDC_CENTERFRACTION, Option::kCenterFraction},
       {IDC_SIDESFRACTION, Option::kSidesFraction},
       {IDC_AEROHOFFSET, Option::kAeroHoffset},
       {IDC_AEROVOFFSET, Option::kAeroVoffset},
       {IDC_SNAPTHRESHOLD, Option::kSnapThreshold},
       {IDC_AEROTHRESHOLD, Option::kAeroThreshold},
       {IDC_SNAPGAP, Option::kSnapGap},
       {IDC_AEROSPEED, Option::kAeroMaxSpeed},
       {IDC_AEROSPEEDTAU, Option::kAeroSpeedTau},
       {IDC_MOVETRANS, Option::kMoveTrans}}};
  static Snapshot snapshot;
  static std::array<std::wstring, std::size(bindings)> original_text;
  //    static HWND testwnd=NULL;
  static int have_to_apply = 0;
  if (msg == WM_INITDIALOG) {
    snapshot =
        settings_service ? settings_service->ReadLegacySettings() : Snapshot{};
    for (size_t i = 0; i < std::size(bindings); ++i) {
      const auto& binding = bindings[i];
      const size_t index = Index(binding.option);
      const auto& value = snapshot.values[index];
      if (kOptions[index].kind == ValueKind::kToggle)
        CheckDlgButton(hwnd, binding.control,
                       value.effective ? BST_CHECKED : BST_UNCHECKED);
      else {
        original_text[i] =
            value.present
                ? value.raw
                : (kOptions[index].kind == ValueKind::kSignedNumber
                       ? std::to_wstring(SignedNumber(value.effective))
                       : std::to_wstring(value.effective));
        SetDlgItemTextW(hwnd, binding.control, original_text[i].c_str());
      }
    }
    have_to_apply = 0;
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
      Patch patch;
      for (size_t i = 0; i < std::size(bindings); ++i) {
        const auto& binding = bindings[i];
        const size_t index = Index(binding.option);
        uint32_t value = 0;
        if (kOptions[index].kind == ValueKind::kToggle) {
          value = IsChecked(hwnd, binding.control);
          if (value == snapshot.values[index].effective) continue;
        } else {
          const HWND control = GetDlgItem(hwnd, binding.control);
          std::wstring text(GetWindowTextLengthW(control) + 1, L'\0');
          text.resize(GetWindowTextW(control, text.data(),
                                     static_cast<int>(text.size())));
          if (text == original_text[i]) continue;
          const auto parsed = ParseNumber(binding.option, text);
          if (!parsed) {
            MessageBoxW(hwnd, L"Enter a whole number in the supported range.",
                        L"AltSnap", MB_OK | MB_ICONERROR);
            SetFocus(control);
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT,
                              PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
          }
          value = *parsed;
        }
        patch.changed |= 1u << index;
        patch.values[index] = value;
      }
      if (patch.changed &&
          (!settings_service ||
           !settings_service->WriteLegacySettings(snapshot, patch))) {
        MessageBoxW(hwnd,
                    L"Could not save advanced settings. Reopen settings and "
                    L"check configuration access.",
                    L"AltSnap", MB_OK | MB_ICONERROR);
        SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
        return TRUE;
      }
      if (settings_service) snapshot = settings_service->ReadLegacySettings();
      for (size_t i = 0; i < std::size(bindings); ++i) {
        const HWND control = GetDlgItem(hwnd, bindings[i].control);
        std::wstring text(GetWindowTextLengthW(control) + 1, L'\0');
        text.resize(GetWindowTextW(control, text.data(),
                                   static_cast<int>(text.size())));
        original_text[i] = std::move(text);
      }
      UpdateSettings();
      // Update Test windows in if open.
      EnumThreadWindows(GetCurrentThreadId(), RefreshTestWin, 0);
      have_to_apply = 0;
    }
  }
  return FALSE;
}
}  // namespace altsnap::app
