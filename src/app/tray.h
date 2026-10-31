#ifndef ALTSNAP_TRAY_H_
#define ALTSNAP_TRAY_H_

#include <string>

#include "hooks/hooks.h"

namespace altsnap::app {

enum { ICONS_COUNT = 3 };

void CloseAllTestWindows();
int InitTray();
extern int LayoutNumber;
extern int MaxLayouts;
int RemoveTray();
void SaveCurrentLayout(int layout);
void ShowContextMenu(HWND hwnd);
int UpdateTray();
extern unsigned char UseZones;
void WriteCurrentLayoutNumber();
std::wstring FullLayoutName(int laynum);
extern HICON g_icons[ICONS_COUNT];
extern unsigned char tray_added;
extern unsigned char tray_hidden;

}  // namespace altsnap::app

#endif  // ALTSNAP_TRAY_H_
