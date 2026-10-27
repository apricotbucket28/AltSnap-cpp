// Interface of send_input.cc (internal to hooks.dll).
#ifndef ALTSNAP_SEND_INPUT_H_
#define ALTSNAP_SEND_INPUT_H_

#include "hooks_internal.h"

namespace altsnap::hooks {

void Send_KEY(unsigned char vkey);
void SendEndKey();
void SendInputSequence(const UCHAR* seq);
void Send_Click(int button);
void Send_Click_Thread(enum button button);
void SendUnicodeKey(WORD w);
int SimulateXButton(WPARAM wp, WORD xbtidx);
DWORD WINAPI SendAltCtrlAlt(LPVOID p);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_SEND_INPUT_H_
