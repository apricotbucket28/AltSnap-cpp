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

/////////////////////////////////////////////////////////////////////////////
INT_PTR CALLBACK AboutPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {
  if (msg == WM_INITDIALOG) {
    MoveToCorner(g_cfgwnd);
  } else if (msg == WM_NOTIFY) {
    auto pnmh = reinterpret_cast<LPNMHDR>(lParam);
    if (pnmh->code == PSN_SETACTIVE) {
      wchar_t buf[128];
      // Update text
      SetDlgItemText(hwnd, IDC_ABOUT_BOX, l10n->AboutBox);
      // Append version number to version....
      CopyString(buf, std::size(buf), l10n->AboutVersion);
      AppendString(buf, std::size(buf), L" " APP_VERSION);
      SetDlgItemText(hwnd, IDC_VERSION, buf /*AboutVersion X.XX*/);

      SetDlgItemText(hwnd, IDC_URL, L"https://github.com/RamonUnch/AltSnap");
      SetDlgItemText(hwnd, IDC_AUTHOR, l10n->AboutAuthor);
      SetDlgItemText(hwnd, IDC_AUTHOR2, l10n->AboutAuthor2);
      SetDlgItemText(hwnd, IDC_LICENSE, l10n->AboutLicense);
      SetDlgItemText(hwnd, IDC_TRANSLATIONS_BOX, l10n->AboutTranslationCredit);

      wchar_t txt[1024];
      txt[0] = L'\0';
      for (size_t i = 0; i < langinfo.size(); i++) {
        AppendString(txt, std::size(txt), langinfo[i].lang_english.c_str());
        AppendString(txt, std::size(txt), L": ");
        AppendString(txt, std::size(txt), langinfo[i].author.c_str());
        if (i + 1 != langinfo.size()) {
          AppendString(txt, std::size(txt), L"\r\n");
        }
      }
      SetDlgItemText(hwnd, IDC_TRANSLATIONS, txt);
    }
  }

  return FALSE;
}
}  // namespace altsnap::app
