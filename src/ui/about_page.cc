// about page (split out of config.cc).

#include "ui/about_page.h"

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

namespace altsnap::app {

INT_PTR CALLBACK AboutPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {
  if (msg == WM_INITDIALOG) {
    MoveToCorner(g_cfgwnd);
  } else if (msg == WM_NOTIFY) {
    auto pnmh = reinterpret_cast<LPNMHDR>(lParam);
    if (pnmh->code == PSN_SETACTIVE) {
      // Update text
      SetDlgItemText(hwnd, IDC_ABOUT_BOX, l10n->AboutBox);
      // Append version number to version....
      const std::wstring version =
          std::wstring(l10n->AboutVersion) + L" " APP_VERSION;
      SetDlgItemText(hwnd, IDC_VERSION, version.c_str());

      SetDlgItemText(hwnd, IDC_URL, L"https://github.com/RamonUnch/AltSnap");
      SetDlgItemText(hwnd, IDC_AUTHOR, l10n->AboutAuthor);
      SetDlgItemText(hwnd, IDC_AUTHOR2, l10n->AboutAuthor2);
      SetDlgItemText(hwnd, IDC_LICENSE, l10n->AboutLicense);
      SetDlgItemText(hwnd, IDC_TRANSLATIONS_BOX, l10n->AboutTranslationCredit);

      // "Language: author", one translation per line.
      std::wstring credits;
      for (size_t i = 0; i < langinfo.size(); i++) {
        if (i > 0) credits += L"\r\n";
        credits += langinfo[i].lang_english + L": " + langinfo[i].author;
      }
      SetDlgItemText(hwnd, IDC_TRANSLATIONS, credits.c_str());
    }
  }

  return FALSE;
}
}  // namespace altsnap::app
