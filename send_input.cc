// send input (split out of hooks.cc).

#include "send_input.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "blacklist.h"
#include "hooks_internal.h"
#include "ini_section.h"
#include "input_state.h"
#include "mouse_hook.h"
#include "snap.h"
#include "zones.h"

namespace altsnap::hooks {

/////////////////////////////////////////////////////////////////////////////
void Send_KEY(unsigned char vkey) {
  KEYBDINPUT ctrl[2] = {{0, 0, 0, 0, 0}, {0, 0, KEYEVENTF_KEYUP, 0, 0}};
  ctrl[0].wVk = ctrl[1].wVk = vkey;
  ctrl[0].dwExtraInfo = ctrl[1].dwExtraInfo = GetMessageExtraInfo();
  //    ctrl[0].time = ctrl[1].time = GetTickCount();
  INPUT input[2] = {{0}, {0}};
  input[0].type = input[1].type = INPUT_KEYBOARD;
  input[0].ki = ctrl[0];
  input[1].ki = ctrl[1];
  InterlockedIncrement(&state.ignorekey);
  SendInput(2, input, sizeof(*input));
  InterlockedDecrement(&state.ignorekey);
}
enum { KEYEVENTF_KEYDOWN = 0 };
// Call with or KEYEVENTF_KEYDOWN/KEYEVENTF_KEYUP
static void Send_KEY_UD(unsigned char vkey, WORD flags) {
  KEYBDINPUT ctrl = {0, 0, 0, 0, 0};
  ctrl.wVk = vkey;
  ctrl.dwExtraInfo = GetMessageExtraInfo();
  ctrl.dwFlags = flags;
  ctrl.time = GetTickCount();
  INPUT input = {0};
  input.type = INPUT_KEYBOARD;
  input.ki = ctrl;

  InterlockedIncrement(&state.ignorekey);
  SendInput(1, &input, sizeof(input));
  InterlockedDecrement(&state.ignorekey);
}
// Sends the key configured with EndSendKey, if any.
void SendEndKey() {
  if (conf.EndSendKey) {
    Send_KEY(conf.EndSendKey);
    LOG("END KEY SENT");
  }
}

// Send a sequence of Inputs.....
void SendInputSequence(const UCHAR* seq) {
  UCHAR len = *seq;
  while (len--) {
    UCHAR vKey = *++seq;
    UCHAR Down = *++seq;
    if (Down == 2)  // Combined U, then D
      Send_KEY(vKey);
    else
      Send_KEY_UD(vKey, Down ? KEYEVENTF_KEYDOWN : KEYEVENTF_KEYUP);
    // LOGA("Sending %x, %s", (UINT)vKey, Down? "Down": "Up");
  }
}

/////////////////////////////////////////////////////////////////////////////
// Sends the click down/click up sequence to the system
static DWORD WINAPI Send_ClickProc(LPVOID buttonD) {
  auto button = static_cast<enum button>(reinterpret_cast<LONG_PTR>(buttonD));
  static const WORD bmapping[] = {MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_RIGHTDOWN,
                                  MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_XDOWN,
                                  MOUSEEVENTF_XDOWN};
  if (!button || button > BT_MB5) return 0;

  DWORD MouseEvent = bmapping[button - 2];
  DWORD mdata = 0;
  if (MouseEvent == MOUSEEVENTF_XDOWN)  // XBUTTON
    mdata = button - 0x04;              // mdata = 1 for X1 and 2 for X2
  // MouseEvent<<1 corresponds to MOUSEEVENTF_*UP
  MOUSEINPUT click[2];
  ZeroObject(click);
  click[0].mouseData = click[1].mouseData = mdata;
  click[0].dwFlags = MouseEvent;
  click[1].dwFlags = MouseEvent << 1;
  click[0].dwExtraInfo = click[1].dwExtraInfo = GetMessageExtraInfo();
  INPUT input[2];
  input[0].type = input[1].type = INPUT_MOUSE;
  input[0].mi = click[0];
  input[1].mi = click[1];

  InterlockedIncrement(&state.ignoreclick);
  SendInput(2, input, sizeof(*input));
  InterlockedDecrement(&state.ignoreclick);
  return 0;
}
void Send_Click(int button) {
  Send_ClickProc(reinterpret_cast<LPVOID>(static_cast<LONG_PTR>(button)));
}
void Send_Click_Thread(enum button button) {
  PostThreadMessage(g_WorkerThreadID, WM_DOWORK,
                    reinterpret_cast<WPARAM>(Send_ClickProc),
                    static_cast<LPARAM>(button));
}
/////////////////////////////////////////////////////////////////////////////
// Sends an unicode character to the system.
// KEYEVENTF_UNICODE requires at least Windows 2000
// Extended unicode page can be accessed by sending both lo&hi surrogates
void SendUnicodeKey(WORD w) {
  KEYBDINPUT ctrl[2] = {{0, 0, KEYEVENTF_UNICODE, 0, 0},
                        {0, 0, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP, 0, 0}};
  // mem00(&click[0], sizeof(KEYBDINPUT)*2);
  ctrl[0].wScan = ctrl[1].wScan = w;
  //    ctrl[0].dwFlags = KEYEVENTF_UNICODE;
  //    ctrl[1].dwFlags = KEYEVENTF_UNICODE|KEYEVENTF_KEYUP;
  ctrl[0].dwExtraInfo = ctrl[1].dwExtraInfo = GetMessageExtraInfo();
  //    ctrl[0].time = ctrl[1].time = GetTickCount();
  INPUT input[2];
  input[0].type = input[1].type = INPUT_KEYBOARD;
  input[0].ki = ctrl[0];
  input[1].ki = ctrl[1];

  InterlockedIncrement(&state.ignorekey);
  SendInput(2, input, sizeof(*input));
  InterlockedDecrement(&state.ignorekey);
}
int SimulateXButton(WPARAM wp, WORD xbtidx) {
  //    WORD xbtidx = XXButtonIndex(vkey);
  MSLLHOOKSTRUCT msg;
  GetCursorPos(&msg.pt);
  // XButton number is in HIWORD(mouseData)
  msg.mouseData = xbtidx << 16;
  msg.flags = 0;
  msg.time = GetTickCount();
  LowLevelMouseProc(HC_ACTION, wp, reinterpret_cast<LPARAM>(&msg));
  return 1;
}
DWORD WINAPI SendAltCtrlAlt(LPVOID p) {
  Send_KEY_UD(VK_MENU, KEYEVENTF_KEYDOWN);
  Send_KEY(VK_CONTROL);
  Send_KEY_UD(VK_MENU, KEYEVENTF_KEYUP);

  return 1;
}

}  // namespace altsnap::hooks
