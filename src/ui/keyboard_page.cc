// keyboard page (split out of config.cc).

#include "ui/keyboard_page.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "app/altsnap.h"
#include "app/languages.h"
#include "app/tray.h"
#include "common/ini_section.h"
#include "hooks/hooks.h"
#include "resource.h"
#include "ui/dialog_util.h"
#include "ui/mouse_page.h"

namespace altsnap::app {

/////////////////////////////////////////////////////////////////////////////
[[nodiscard]] static int IsKeyInList(wchar_t* keys, unsigned vkey) {
  unsigned temp = 0, numread = 0;
  wchar_t* pos = keys;
  while (*pos != '\0') {
    numread = 0;
    temp = lstrhex2u(pos);
    while (pos[numread] && pos[numread] != ' ') numread++;
    while (pos[numread] == ' ') numread++;
    if (temp == vkey) {
      return 1;
    }
    pos += numread;
  }
  return 0;
}
static void AddvKeytoList(wchar_t keys[32], unsigned vkey) {
  // Check if it is already in the list.
  if (IsKeyInList(keys, vkey)) return;
  // Add a key to the hotkeys list
  if (*keys != '\0') {
    lstrcat_s(keys, 32, L" ");
  }
  wchar_t buf[kPtrHexDigits + 1];
  lstrcat_s(keys, 32, LPTR2Hex(buf, vkey));
}
static void RemoveKeyFromList(wchar_t keys[32], unsigned vkey) {
  // Remove the key from the hotclick list
  unsigned temp = 0, numread = 0;
  wchar_t* pos = keys;
  while (*pos != '\0') {
    numread = 0;
    temp = lstrhex2u(pos);
    while (pos[numread] && pos[numread] != ' ') numread++;
    while (pos[numread] == ' ') numread++;
    if (temp == vkey) {
      keys[pos - keys] = '\0';
      lstrcat_s(keys, 32, pos + numread);
      break;
    }
    pos += numread;
  }
  // Strip eventual remaining spaces
  unsigned ll = lstrlen(keys);
  while (ll > 0 && keys[--ll] == ' ') keys[ll] = '\0';
}
void SaveHotKeys(const struct hk_struct* const hotkeys, HWND hwnd,
                 const wchar_t* const name) {
  wchar_t keys[32];
  // Get the current config in case there are some user added keys.
  GetPrivateProfileString(L"Input", name, L"", keys, std::size(keys), inipath);
  for (size_t i = 0; hotkeys[i].control; i++) {
    if (IsChecked(hwnd, hotkeys[i].control)) {
      AddvKeytoList(keys, hotkeys[i].vkey);
    } else {
      RemoveKeyFromList(keys, hotkeys[i].vkey);
    }
  }
  WritePrivateProfileString(L"Input", name, keys, inipath);
}
/////////////////////////////////////////////////////////////////////////////
void CheckConfigHotKeys(const struct hk_struct* hotkeys, HWND hwnd,
                        const wchar_t* hotkeystr, const wchar_t* def) {
  // Hotkeys
  unsigned temp = 0;
  wchar_t txt[32];
  GetPrivateProfileString(L"Input", hotkeystr, def, txt, std::size(txt),
                          inipath);
  wchar_t* pos = txt;
  while (*pos != '\0') {
    temp = lstrhex2u(pos);
    while (*pos && *pos != ' ') pos++;
    while (*pos == ' ') pos++;

    // What key was that?
    for (size_t i = 0; hotkeys[i].control; i++) {
      if (temp == hotkeys[i].vkey) {
        CheckDlgButton(hwnd, hotkeys[i].control, BST_CHECKED);
        break;
      }
    }
  }
}

/////////////////////////////////////////////////////////////////////////////
INT_PTR CALLBACK KeyboardPageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam) {
  static int have_to_apply = 0;
  static int edit_shortcut_idx = 0;
  // Hotkeys
  static const struct hk_struct hotkeys[] = {{IDC_LEFTALT, VK_LMENU},
                                             {IDC_RIGHTALT, VK_RMENU},
                                             {IDC_LEFTWINKEY, VK_LWIN},
                                             {IDC_RIGHTWINKEY, VK_RWIN},
                                             {IDC_LEFTCTRL, VK_LCONTROL},
                                             {IDC_RIGHTCTRL, VK_RCONTROL},
                                             {0, 0}};
  static const actiondl_t kb_actions[] = {
      {L"Move", L10nId::InputActionMove},
      {L"Resize", L10nId::InputActionResize},
      {L"Close", L10nId::InputActionClose},
      {L"Minimize", L10nId::InputActionMinimize},
      {L"Maximize", L10nId::InputActionMaximize},
      {L"Lower", L10nId::InputActionLower},
      {L"Roll", L10nId::InputActionRoll},
      {L"AlwaysOnTop", L10nId::InputActionAlwaysOnTop},
      {L"Borderless", L10nId::InputActionBorderless},
      {L"Center", L10nId::InputActionCenter},
      {L"Center2", L10nId::InputActionCenter2},
      {L"MaximizeHV", L10nId::InputActionMaximizeHV},
      {L"MinAllOther", L10nId::InputActionMinAllOther},
      {L"Menu", L10nId::InputActionMenu},
      {L"Nothing", L10nId::InputActionNothing},
      {nullptr, 0}};
  static const actiondl_t kbshortcut_actions[] = {
      {L"Kill", L10nId::InputActionKill},
      {L"Pause", L10nId::InputActionPause},
      {L"Resume", L10nId::InputActionResume},
      {L"ASOnOff", L10nId::InputActionASOnOff},
      {L"Close", L10nId::InputActionClose},
      {L"Minimize", L10nId::InputActionMinimize},
      {L"Maximize", L10nId::InputActionMaximize},
      {L"Lower", L10nId::InputActionLower},
      {L"Roll", L10nId::InputActionRoll, ACPARAM_UPDOWN},
      {L"Transparency", L10nId::InputActionTransparency,
       ACPARAM_UPDOWNSETTOGGLE, ACPARAM_NUMBER},
      {L"AlwaysOnTop", L10nId::InputActionAlwaysOnTop},
      {L"Borderless", L10nId::InputActionBorderless},
      {L"Center", L10nId::InputActionCenter},
      {L"Center2", L10nId::InputActionCenter2},
      {L"Mute", L10nId::InputActionMute},
      {L"Menu", L10nId::InputActionMenu},

      {L"NStacked", L10nId::InputActionNStacked},
      {L"NStacked2", L10nId::InputActionNStacked2},
      {L"PStacked", L10nId::InputActionPStacked},
      {L"PStacked2", L10nId::InputActionPStacked2},
      {L"StackList", L10nId::InputActionStackList},
      {L"StackList2", L10nId::InputActionStackList2},
      {L"AltTabList", L10nId::InputActionAltTabList},
      {L"AltTabFullList", L10nId::InputActionAltTabFullList},

      {L"ExtendTNEdge", L10nId::InputActionExtendTNEdge, ACPARAM_DIRECTION, 0},
      {L"MoveTNEdge", L10nId::InputActionMoveTNEdge, ACPARAM_DIRECTION, 0},

      {L"MZone", L10nId::InputActionMZone, ACPARAM_DIRECTION, 0},
      {L"XZone", L10nId::InputActionXZone, ACPARAM_DIRECTION, 0},

      {L"Step", L10nId::InputActionStep, ACPARAM_DIRECTION, ACPARAM_NUMBER},

      {L"Focus", L10nId::InputActionFocus, ACPARAM_DIRECTION, 0},

      {L"NLayout", L10nId::InputActionNLayout},
      {L"PLayout", L10nId::InputActionPLayout},

      {nullptr, 0}};

  // Hotkeys
  static const actiondl_t togglekeys[] = {
      {L"", L10nId::InputActionNothing},
      {L"A4 A5", L10nId::InputHotkeysAlt},
      {L"5B 5C", L10nId::InputHotkeysWinkey},
      {L"A2 A3", L10nId::InputHotkeysCtrl},
      {L"A0 A1", L10nId::InputHotkeysShift},
      {L"A4", L10nId::InputHotkeysLeftAlt},
      {L"A5", L10nId::InputHotkeysRightAlt},
      {L"5B", L10nId::InputHotkeysLeftWinkey},
      {L"5C", L10nId::InputHotkeysRightWinkey},
      {L"A2", L10nId::InputHotkeysLeftCtrl},
      {L"A3", L10nId::InputHotkeysRightCtrl},
      {L"A0", L10nId::InputHotkeysLeftShift},
      {L"A1", L10nId::InputHotkeysRightShift},
      {nullptr, 0},
  };
  static const optlst_t optlst[] = {
      {IDC_SCROLLLOCKSTATE, T_BMK, 0, L"Input", "ScrollLockState", nullptr},
      {IDC_UNIKEYHOLDMENU, T_BOL, 0, L"Input", "UniKeyHoldMenu", nullptr},
      {IDC_KEYCOMBO, T_BOL, 0, L"Input", "KeyCombo", nullptr},
      {IDC_USEPTWINDOW, T_BOL, 0, L"KBShortcuts", "UsePtWindow", nullptr},
  };

  auto pnmh = reinterpret_cast<LPNMHDR>(lParam);

  // Body shared by several message paths (was a goto target).
  auto apply_all = [&]() {
    //            int i;
    // Action without click
    WriteActionDropListS(hwnd, IDC_GRABWITHALT, L"GrabWithAlt", kb_actions);
    WriteActionDropListS(hwnd, IDC_GRABWITHALTB, L"GrabWithAltB", kb_actions);

    WriteDialogOptions(hwnd, optlst, std::size(optlst));
    ScrollLockState = WriteOptionBoolBit(hwnd, IDC_SCROLLLOCKSTATE, L"Input",
                                         "ScrollLockState", 0);
    // Modifier key (similar to action list)
    WriteActionDropListS(hwnd, IDC_MODKEY, L"ModKey", togglekeys);
    // Hotkeys
    SaveHotKeys(hotkeys, hwnd, L"Hotkeys");
    WriteOptionBool(hwnd, IDC_KEYCOMBO, L"Input", "KeyCombo");
    UpdateSettings();
    have_to_apply = 0;
  };

  if (msg == WM_INITDIALOG) {
    edit_shortcut_idx = 0;
    ReadDialogOptions(hwnd, optlst, std::size(optlst));
    // Agressive Pause
    CheckConfigHotKeys(hotkeys, hwnd, L"Hotkeys", L"A4 A5");

#ifndef _WIN64
    // Always enabled in 64 bit mode.
    EnableDlgItem(hwnd, IDC_AGGRESSIVEPAUSE, TRUE);
    EnableDlgItem(hwnd, IDC_UNIKEYHOLDMENU, TRUE);
#endif

  } else if (msg == WM_COMMAND) {
    int event = HIWORD(wParam);
    int id = LOWORD(wParam);
    if (id == IDC_ALT || id == IDC_WINKEY || id == IDC_CONTROL ||
        id == IDC_SHIFT) {
      EnableDlgItem(hwnd, IDC_SHORTCUTS_SET, 1);
    } else if (event == EN_UPDATE && id == IDC_SHORTCUTS_VK) {
      wchar_t txt[4];
      GetDlgItemText(hwnd, IDC_SHORTCUTS_VK, txt, 3);
      BYTE vKey = lstrhex2u(txt);
      wchar_t keyname[32];
      keyname[0] = L'\0';
      GetKeyNameText(MapVirtualKey(vKey, 0) << 16, keyname,
                     std::size(keyname) - 8);
      SetDlgItemText(hwnd, IDC_SHORTCUTS, keyname);

      EnableDlgItem(hwnd, IDC_SHORTCUTS_SET, 1);
    } else if (event == CBN_EDITCHANGE ||
               ((event == 0 || event == EN_UPDATE || event == CBN_SELCHANGE) &&
                (IDC_SHORTCUTS > id || id > IDC_SHORTCUTS_CLEAR))) {
      PropSheet_Changed(g_cfgwnd, hwnd);
      have_to_apply = 1;
    }

    // KEYBOARD SHORTCUTS HANDLING
    // READ Keyboard Shortcut
    if (id == IDC_SHORTCUTS_AC &&
        (event == CBN_SELCHANGE || event == CBN_EDITCHANGE)) {
      // Update the shortcut with the current one.
      wchar_t txt[64];
      int i =
          CB_GetTextAndIdx(reinterpret_cast<HWND>(lParam), txt, std::size(txt));
      edit_shortcut_idx = i;
      // MessageBox(hwnd, txt, NULL, 0);
      WORD shortcut = GetPrivateProfileInt(L"KBShortcuts", txt, 0, inipath);
      BYTE vKey = LOBYTE(shortcut);
      BYTE mod = HIBYTE(shortcut);
      SetDlgItemText(hwnd, IDC_SHORTCUTS_VK, LPTR2Hex(txt, vKey));

      CheckDlgButton(hwnd, IDC_CONTROL,
                     (mod & MOD_CONTROL) ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_SHIFT,
                     (mod & MOD_SHIFT) ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_WINKEY,
                     (mod & MOD_WIN) ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_ALT,
                     (mod & MOD_ALT) ? BST_CHECKED : BST_UNCHECKED);
      EnableDlgItem(hwnd, IDC_SHORTCUTS_SET, 0);
    }
    // WRITE Current Keyboard Shortcut to ini file
    if (id == IDC_SHORTCUTS_SET) {
      wchar_t txt[kUintDigits + 1], action_string_buf[64];
      // MOD_ALT=1, MOD_CONTROL=2, MOD_SHIFT=4, MOD_WIN=8
      GetDlgItemText(hwnd, IDC_SHORTCUTS_VK, txt, 3);
      BYTE vKey = lstrhex2u(txt);
      BYTE mod = IsChecked(hwnd, IDC_ALT) |
                 (IsChecked(hwnd, IDC_CONTROL) << 1) |
                 (IsChecked(hwnd, IDC_SHIFT) << 2) |
                 (IsChecked(hwnd, IDC_WINKEY) << 3);
      WORD shortcut = vKey | (mod << 8);
      action_string_buf[0] = L'\0';
      int astrlen = GetDlgItemText(hwnd, IDC_SHORTCUTS_AC, action_string_buf,
                                   std::size(action_string_buf));
      if (0 < astrlen && *action_string_buf) {
        const wchar_t* intstr = shortcut ? Uint2lStr(txt, shortcut)
                                         : nullptr;  // NULL will remove the KEY
        edit_shortcut_idx = CB_AddStringIfNotPresentAndGetIdx(
            GetDlgItem(hwnd, IDC_SHORTCUTS_AC), action_string_buf);
        WritePrivateProfileString(L"KBShortcuts", action_string_buf, intstr,
                                  inipath);
      }
      EnableDlgItem(hwnd, IDC_SHORTCUTS_SET, 0);
      SetFocus(GetDlgItem(hwnd, IDC_SHORTCUTS_AC));
      apply_all();
      return FALSE;
    }
    if (id == IDC_SHORTCUTS_PICK) {
      HWND pickhwnd = nullptr;
      WNDCLASS wnd;
      if (!GetClassInfo(g_hinst, APP_NAME L"-PickShortcut", &wnd)) {
        WNDCLASS wndd = {CS_PARENTDC,
                         PickShortcutWinProc,
                         0,
                         0,
                         g_hinst,
                         nullptr  // LoadIcon(g_hinst, iconstr[1])
                         ,
                         nullptr  // LoadCursor(NULL, IDC_ARROW)
                         ,
                         nullptr  //(HBRUSH)(COLOR_HIGHLIGHT+1)
                         ,
                         nullptr,
                         APP_NAME L"-PickShortcut"};
        RegisterClass(&wndd);
      }
      RECT rc;
      GetClientRect(hwnd, &rc);
      pickhwnd = CreateWindowEx(WS_EX_TOPMOST, APP_NAME L"-PickShortcut",
                                nullptr, WS_CHILD | WS_VISIBLE, rc.left, rc.top,
                                rc.right - rc.left, rc.bottom - rc.top, hwnd,
                                nullptr, g_hinst, nullptr);
      // Disable all other child windows
      EnumChildWindows(hwnd, EnableAllControlsProc, 0);
      EnableWindow(pickhwnd, TRUE);
      SetFocus(pickhwnd);
      SetActiveWindow(pickhwnd);
      // SetDlgItemText(hwnd, IDC_SHORTCUTS, L"Press Keys...");
      EnableDlgItem(hwnd, IDC_SHORTCUTS_PICK, 0);
    }
    if (id == IDC_SHORTCUTS_CLEAR) {
      // We must clear the current shotrcut
      CheckDlgButton(hwnd, IDC_SHIFT, BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_CONTROL, BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_WINKEY, BST_UNCHECKED);
      CheckDlgButton(hwnd, IDC_ALT, BST_UNCHECKED);
      SetDlgItemText(hwnd, IDC_SHORTCUTS_VK, L"");
      EnableDlgItem(hwnd, IDC_SHORTCUTS_SET, 1);
    }
    if (id == IDC_ADVANCEDBT_AC) {
      // We must show the advanced action modal dialog
      // and fill the IDC_SHORTCUTS_AC combobox with it.
      advancedActionParam_t acp;
      acp.base_action_lst = kbshortcut_actions;
      acp.outbuf[0] = L'\0';
      int code = DialogBoxParam(g_hinst, MAKEINTRESOURCE(IDD_ACTIONPARAM), hwnd,
                                static_cast<DLGPROC>(AdvancedActionDlgProc),
                                reinterpret_cast<LPARAM>(&acp));
      if (code == IDOK && acp.outbuf[0]) {
        HWND control = GetDlgItem(hwnd, IDC_SHORTCUTS_AC);
        int new_index = CB_AddStringIfNotPresentAndGetIdx(control, acp.outbuf);
        ComboBox_SetCurSel(control, new_index);
        PostMessage(
            hwnd, WM_COMMAND, IDC_SHORTCUTS_AC | CBN_SELCHANGE << 16,
            reinterpret_cast<LPARAM>(GetDlgItem(hwnd, IDC_SHORTCUTS_AC)));
        edit_shortcut_idx = new_index;
        SetFocus(control);
      }
    }

  } else if (msg == WM_NOTIFY) {
    if (pnmh->code == PSN_SETACTIVE) {
      // GrabWithAlt
      wchar_t txt[64];

      GetPrivateProfileString(L"Input", L"ModKey", L"", txt, std::size(txt),
                              inipath);
      EnableDlgItem(hwnd, IDC_GRABWITHALTB_H, txt[0]);
      EnableDlgItem(hwnd, IDC_GRABWITHALTB, txt[0]);

      FillActionDropListS(hwnd, IDC_GRABWITHALT, L"GrabWithAlt", kb_actions);
      FillActionDropListS(hwnd, IDC_GRABWITHALTB, L"GrabWithAltB", kb_actions);

      // FillActionDropListS(hwnd, IDC_SHORTCUTS_AC, NULL, kbshortcut_actions);
      {  // Fill combobox with all the shortcuts that are currently configured
        wchar_t kbsec[1600];
        HWND control = GetDlgItem(hwnd, IDC_SHORTCUTS_AC);
        ComboBox_ResetContent(control);
        GetPrivateProfileSection(L"KBShortcuts", kbsec, std::size(kbsec),
                                 inipath);
        for (wchar_t* p = kbsec; *p != L'\0'; p += lstrlen(p) + 1) {
          // MessageBox(hwnd, p, L"KBShortcuts", 0);
          wchar_t* pp = lstrchr(p, L'=');
          if (!pp) continue;  // No equal sign...
          *pp = L'\0';
          if (!lstrcmpi(p, L"UsePtWindow")) continue;
          ComboBox_AddString(control, p);
          p = pp + 1;
        }
      }
      ComboBox_SetCurSel(GetDlgItem(hwnd, IDC_SHORTCUTS_AC), edit_shortcut_idx);
      EnableDlgItem(hwnd, IDC_SHORTCUTS_SET, 0);
      if (pnmh->hwndFrom != nullptr)
        PostMessage(
            hwnd, WM_COMMAND, IDC_SHORTCUTS_AC | CBN_SELCHANGE << 16,
            reinterpret_cast<LPARAM>(GetDlgItem(hwnd, IDC_SHORTCUTS_AC)));

      // ModKey init
      HWND control = GetDlgItem(hwnd, IDC_MODKEY);
      ComboBox_ResetContent(control);
      unsigned j = 0, sel = 0;
      for (j = 0; j < std::size(togglekeys) - 1; j++) {
        wchar_t key_name[256];
        lstrcpy_noaccel(key_name, std::size(key_name),
                        l10n->Get(togglekeys[j].l10nidx));
        ComboBox_AddString(control, key_name);
        if (!lstrcmpi(txt, togglekeys[j].action)) {
          sel = j;
        }
      }
      // Add the current ModKey string to the list if not found!
      if (sel == 0 && txt[0]) {
        ComboBox_AddString(control, &txt[0]);
        sel = std::size(togglekeys) - 1;
      }
      ComboBox_SetCurSel(control, sel);  // select current ModKey

      // Update text
      static const dialogstring_t strlst[] = {
          {IDC_KEYBOARD_BOX, L10nId::ConfigTabKeyboard},
          {IDC_SCROLLLOCKSTATE, L10nId::InputScrollLockState},
          {IDC_UNIKEYHOLDMENU, L10nId::InputUniKeyHoldMenu},
          {IDC_HOTKEYS_BOX, L10nId::InputHotkeysBox},
          {IDC_MODKEY_H, L10nId::InputHotkeysModKey},

          {IDC_ALT, L10nId::InputHotkeysAlt},
          {IDC_SHIFT, L10nId::InputHotkeysShift},
          {IDC_CONTROL, L10nId::InputHotkeysCtrl},
          {IDC_WINKEY, L10nId::InputHotkeysWinkey},
          {IDC_SHORTCUTS_H, L10nId::InputHotkeysShortcuts},
          {IDC_SHORTCUTS_PICK, L10nId::InputHotkeysShortcutsPick},
          {IDC_SHORTCUTS_CLEAR, L10nId::InputHotkeysShortcutsClear},
          {IDC_SHORTCUTS_SET, L10nId::InputHotkeysShortcutsSet},
          {IDC_USEPTWINDOW, L10nId::InputHotkeysUsePtWindow},
          {IDC_LEFTALT, L10nId::InputHotkeysLeftAlt},
          {IDC_RIGHTALT, L10nId::InputHotkeysRightAlt},
          {IDC_LEFTWINKEY, L10nId::InputHotkeysLeftWinkey},
          {IDC_RIGHTWINKEY, L10nId::InputHotkeysRightWinkey},
          {IDC_LEFTCTRL, L10nId::InputHotkeysLeftCtrl},
          {IDC_RIGHTCTRL, L10nId::InputHotkeysRightCtrl},
          {IDC_HOTKEYS_MORE, L10nId::InputHotkeysMore},
          {IDC_KEYCOMBO, L10nId::InputKeyCombo},
          {IDC_GRABWITHALT_H, L10nId::InputGrabWithAlt},
          {IDC_GRABWITHALTB_H, L10nId::InputGrabWithAltB}};
      UpdateDialogStrings(hwnd, strlst, std::size(strlst));

    } else if (pnmh->code == PSN_APPLY && have_to_apply) {
      apply_all();
    }
  }
  return FALSE;
}
}  // namespace altsnap::app
