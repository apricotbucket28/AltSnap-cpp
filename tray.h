#ifndef ALTSNAP_TRAY_H_
#define ALTSNAP_TRAY_H_

#include "hooks.h"

enum { ICONS_COUNT = 3 };

void CloseAllTestWindows();
int InitTray();
extern int LayoutNumber;
extern int MaxLayouts;
wchar_t* RectToStr(const RECT* rc, wchar_t rectstr[kIntDigits * 4 + 4 + 1]);
int RemoveTray();
void SaveCurrentLayout();
void ShowContextMenu(HWND hwnd);
int UpdateTray();
extern unsigned char UseZones;
void WriteCurrentLayoutNumber();
void catFullLayoutName(wchar_t* txt, size_t len, int laynum);
extern HICON g_icons[ICONS_COUNT];
extern unsigned char tray_added;
extern unsigned char tray_hidden;

#endif  // ALTSNAP_TRAY_H_
