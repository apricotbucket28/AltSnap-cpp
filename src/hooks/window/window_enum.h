// Interface of window_enum.cc (internal to hooks.dll).
#ifndef ALTSNAP_WINDOW_ENUM_H_
#define ALTSNAP_WINDOW_ENUM_H_

#include "hooks/hooks_internal.h"

namespace altsnap::hooks {

void EnumSnapped();
const RECT& EnumOnce();
void RecalcInvisibleBorders();
void EnumSnappedOnce();
BOOL CALLBACK EnumAltTabWindows(HWND window, LPARAM lParam);
BOOL CALLBACK EnumAllAltTabWindows(HWND window, LPARAM lParam);
BOOL CALLBACK EnumTopMostWindows(HWND window, LPARAM lParam);
BOOL CALLBACK EnumStackedWindowsProc(HWND hwnd, LPARAM lasermode);
BOOL CALLBACK MinimizeWindowProc(HWND hwnd, LPARAM lParam);
HWND FindTiledWindow(HWND hwnd, unsigned char direction);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_WINDOW_ENUM_H_
