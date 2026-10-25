#ifndef ALTSNAP_CONFIG_H_
#define ALTSNAP_CONFIG_H_

#include "hooks.h"

void CloseConfig();
BOOL ElevateNow(int showconfig);
HWND NewTestWindow();
HWND NewTestWindowAt(int x, int y, int width, int height);
void OpenConfig(int startpage);

#endif  // ALTSNAP_CONFIG_H_
