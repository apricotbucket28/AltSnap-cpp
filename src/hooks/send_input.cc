// send input (split out of hooks.cc).

#include "hooks/send_input.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "common/ini_section.h"
#include "hooks/blacklist.h"
#include "hooks/hooks_internal.h"
#include "hooks/input_state.h"
#include "hooks/mouse_hook.h"
#include "hooks/window/snap.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

// Upstream: Send_KEY
void SendKey(unsigned char vkey) {
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
// Upstream: Send_KEY_UD
static void SendKeyUpDown(unsigned char vkey, WORD flags) {
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
    SendKey(conf.EndSendKey);
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
      SendKey(vKey);
    else
      SendKeyUpDown(vKey, Down ? KEYEVENTF_KEYDOWN : KEYEVENTF_KEYUP);
    // LOGA("Sending %x, %s", (UINT)vKey, Down? "Down": "Up");
  }
}

// Sends the click down/click up sequence to the system
// Upstream: Send_ClickProc
static DWORD WINAPI SendClickProc(LPVOID buttonD) {
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
// Upstream: Send_Click
void SendClick(int button) {
  SendClickProc(reinterpret_cast<LPVOID>(static_cast<LONG_PTR>(button)));
}
// Upstream: Send_Click_Thread
void SendClickThread(enum button button) {
  PostThreadMessage(g_WorkerThreadID, WM_DOWORK,
                    reinterpret_cast<WPARAM>(SendClickProc),
                    static_cast<LPARAM>(button));
}

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
  SendKeyUpDown(VK_MENU, KEYEVENTF_KEYDOWN);
  SendKey(VK_CONTROL);
  SendKeyUpDown(VK_MENU, KEYEVENTF_KEYUP);

  return 1;
}

}  // namespace altsnap::hooks
