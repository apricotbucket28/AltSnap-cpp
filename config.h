#ifndef ALTSNAP_CONFIG_H_
#define ALTSNAP_CONFIG_H_

#include "hooks.h"

namespace altsnap::app {

void CloseConfig();
BOOL ElevateNow(int showconfig);
HWND NewTestWindow();
HWND NewTestWindowAt(int x, int y, int width, int height);
void OpenConfig(int startpage);

}  // namespace altsnap::app

#endif  // ALTSNAP_CONFIG_H_
