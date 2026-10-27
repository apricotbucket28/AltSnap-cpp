// Interface of dialog_util.cc (internal).
#ifndef ALTSNAP_DIALOG_UTIL_H_
#define ALTSNAP_DIALOG_UTIL_H_

#include "ui/config_internal.h"

namespace altsnap::app {

// State of a check box or radio button of the dialog `hwnd`.
inline UINT IsChecked(HWND hwnd, int id) {
  return IsDlgButtonChecked(hwnd, id);
}

// Writes/reads one dialog control to/from an ini option. These used to be
// macros (WriteOptionBool(id, ...)) that silently captured a variable named
// `hwnd`; the dialog is now passed explicitly.
// Upstream: WriteOptionBoolW, WriteOptionBoolBW, WriteOptionStrW,
// ReadOptionStrW, ReadOptionIntW.
void WriteOptionBool(HWND hwnd, WORD id, const wchar_t* section,
                     const char* name_s);
int WriteOptionBoolBit(HWND hwnd, WORD id, const wchar_t* section,
                       const char* name_s, int bit);
void WriteOptionStr(HWND hwnd, WORD id, const wchar_t* section,
                    const char* name_s);
void ReadOptionStr(HWND hwnd, WORD id, const wchar_t* section,
                   const char* name_s, const wchar_t* def);
int ReadOptionInt(HWND hwnd, WORD id, const wchar_t* section,
                  const char* name_s, int def, int mask);
int CB_GetCurSelDlgItem(HWND hwnd, UINT id);

void MoveButtonUporDown(WORD id, WINDOWPLACEMENT* wndpl, int diffrows);
void MoveToCorner(HWND hwnd);
LRESULT CB_GetTextAndIdx(HWND hwnd, wchar_t* txt, size_t txtlen);
LRESULT CB_AddStringIfNotPresentAndGetIdx(HWND hwnd, const wchar_t* txt);
HWND CreateInfoTip(HWND hDlg, int toolID, const wchar_t* const pszText);
void UpdateDialogStrings(HWND hwnd, const dialogstring_t* const strlst,
                         size_t size);
void ReadDialogOptions(HWND hwnd, const optlst_t* ol, size_t size);
void WriteDialogOptions(HWND hwnd, const optlst_t* ol, size_t size);
void ShowContextHelp(const dialogstring_t sl[], size_t len, HWND hwnd,
                     LPHELPINFO hi);
BOOL CALLBACK EnableAllControlsProc(HWND hwnd, LPARAM lp);

}  // namespace altsnap::app

#endif  // ALTSNAP_DIALOG_UTIL_H_
