// Interface of send_input.cc (internal to hooks.dll).
#ifndef ALTSNAP_SEND_INPUT_H_
#define ALTSNAP_SEND_INPUT_H_

#include "hooks/hooks_internal.h"

namespace altsnap::hooks {

void SendKey(unsigned char vkey);
void SendEndKey();
void SendInputSequence(const UCHAR* seq);
void SendClick(int button);
void SendClickThread(enum button button);
void SendUnicodeKey(WORD w);
int SimulateXButton(WPARAM wp, WORD xbtidx);
DWORD WINAPI SendAltCtrlAlt(LPVOID p);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_SEND_INPUT_H_
