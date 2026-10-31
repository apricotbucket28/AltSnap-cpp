// mouse page (split out of config.cc).

#include "ui/mouse_page.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "app/altsnap.h"
#include "app/languages.h"
#include "app/tray.h"
#include "common/ini_section.h"
#include "common/text_format.h"
#include "hooks/hooks.h"
#include "resource.h"
#include "ui/dialog_util.h"
#include "ui/keyboard_page.h"

namespace altsnap::app {

void FillActionDropListS(HWND hwnd, int idc, const wchar_t* inioption,
                         const actiondl_t* actions) {
  HWND control = GetDlgItem(hwnd, idc);
  wchar_t txt[64];
  int sel = 0, j = 0;
  ComboBox_ResetContent(control);
  if (inioption) {
    sel = -1;  // Selection to be made
    GetPrivateProfileString(L"Input", inioption, L"Nothing", txt,
                            std::size(txt), inipath.c_str());
  }
  for (j = 0; actions[j].action; j++) {
    wchar_t action_name[256];
    CopyWithoutAccelerator(action_name, std::size(action_name),
                           l10n->Get(actions[j].l10nidx));
    ComboBox_AddString(control, action_name);
    if (inioption && !lstrcmpi(txt, actions[j].action)) {
      sel = j;
    }
  }
  if (sel < 0) {
    // sel is negative if the string was not found in the actiondl_t:
    // UNKNOWN ACTION, so we add it manually at the end of the list
    ComboBox_AddString(control, txt);
    sel = j;  // And select this unknown action.
  }
  ComboBox_SetCurSel(control, sel);
}

static int GetActionStringFromDropList(HWND hwnd, int idc,
                                       const actiondl_t* actions, wchar_t* obuf,
                                       size_t obuflen) {
  HWND control = GetDlgItem(hwnd, idc);
  int j = SendMessage(control, CB_GETCURSEL, 0, 0);
  if (j >= 0 && actions[j].action) {               // Inside of known values
    CopyString(obuf, obuflen, actions[j].action);  // DONE!
    return j;
  }

  // User directly Wrote the specified string?
  int len = static_cast<int>(static_cast<DWORD>(SendMessage(
      control, WM_GETTEXT, obuflen, reinterpret_cast<LPARAM>(obuf))));
  if (0 < len && *obuf) {
    // Action was direcly written!
    j = SendMessage(control, CB_FINDSTRINGEXACT, /*start index=*/-1,
                    reinterpret_cast<LPARAM>(obuf));
    if (j >= 0 && actions[j].action)                 // Found index.
      CopyString(obuf, obuflen, actions[j].action);  // DONE!

    return j;
  }
  return -1;
}
void WriteActionDropListS(HWND hwnd, int idc, const wchar_t* inioption,
                          const actiondl_t* actions) {
  wchar_t txt[128];
  txt[0] = L'\0';
  GetActionStringFromDropList(hwnd, idc, actions, txt, std::size(txt));
  WritePrivateProfileString(L"Input", inioption, txt, inipath.c_str());
}
/////////////////////////////////////////////////////////////////////////////
INT_PTR CALLBACK MousePageDialogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {
  static int have_to_apply = 0;
  // Mouse actions
  static const struct {
    int control;               // Same control
    const wchar_t* option[5];  // Prim/alt/TTB/WM/WR
  } mouse_buttons[] = {
      {IDC_LMB, {L"LMB", L"LMBB", L"LMBT", L"LMBM", L"LMBR"}},
      {IDC_MMB, {L"MMB", L"MMBB", L"MMBT", L"MMBM", L"MMBR"}},
      {IDC_RMB, {L"RMB", L"RMBB", L"RMBT", L"RMBM", L"RMBR"}},
      {IDC_MB4, {L"MB4", L"MB4B", L"MB4T", L"MB4M", L"MB4R"}},
      {IDC_MB5, {L"MB5", L"MB5B", L"MB5T", L"MB5M", L"MB5M"}},
  };
  static const struct {
    int control;               // Same control
    const wchar_t* option[5];  // Prim/alt/TTB/WM/WR
  } mouse_buttonsUP[] = {
      {IDC_MOVEUP, {L"MoveUp", L"MoveUpB", L"MoveUpT", L"MoveUp", L"MoveUp"}},
      {IDC_RESIZEUP,
       {L"ResizeUp", L"ResizeUpB", L"ResizeUpT", L"ResizeUp", L"ResizeUp"}},
  };

#define COMMON_ACTIONS                                                      \
  {L"Close", L10nId::InputActionClose}, {L"Kill", L10nId::InputActionKill}, \
      {L"Minimize", L10nId::InputActionMinimize},                           \
      {L"Maximize", L10nId::InputActionMaximize},                           \
      {L"Lower", L10nId::InputActionLower},                                 \
      {L"Focus", L10nId::InputActionFocus},                                 \
      {L"NStacked", L10nId::InputActionNStacked},                           \
      {L"PStacked", L10nId::InputActionPStacked},                           \
      {L"StackList", L10nId::InputActionStackList},                         \
      {L"StackList2", L10nId::InputActionStackList2},                       \
      {L"AltTabList", L10nId::InputActionAltTabList},                       \
      {L"AltTabFullList", L10nId::InputActionAltTabFullList},               \
      {L"Roll", L10nId::InputActionRoll},                                   \
      {L"AlwaysOnTop", L10nId::InputActionAlwaysOnTop},                     \
      {L"Borderless", L10nId::InputActionBorderless},                       \
      {L"Center", L10nId::InputActionCenter},                               \
      {L"Center2", L10nId::InputActionCenter2},                             \
      {L"MaximizeHV", L10nId::InputActionMaximizeHV},                       \
      {L"SideSnap", L10nId::InputActionSideSnap},                           \
      {L"ExtendSnap", L10nId::InputActionExtendSnap},                       \
      {L"ExtendTNEdge", L10nId::InputActionExtendTNEdge},                   \
      {L"MoveTNEdge", L10nId::InputActionMoveTNEdge},                       \
      {L"NLayout", L10nId::InputActionNLayout},                             \
      {L"PLayout", L10nId::InputActionPLayout},                             \
      {L"MinAllOther", L10nId::InputActionMinAllOther},                     \
      {L"Mute", L10nId::InputActionMute}, {L"Menu", L10nId::InputActionMenu},

  static const actiondl_t mouse_actions[] = {
      // Specific to the Primary/Alternate/Titlebar
      // And not available for the MoveUp/ResizeUp
      {L"Move", L10nId::InputActionMove},
      {L"Resize", L10nId::InputActionResize},
      {L"Restore", L10nId::InputActionRestore},
      // Common mouse actions
      COMMON_ACTIONS

      {L"Nothing", L10nId::InputActionNothing},
      {nullptr, 0}};

  // Actions on MoveUp/ResizeUP
  const actiondl_t* mouse_actionsUP = &mouse_actions[3];

  // While moving/resizing
  const actiondl_t* mouse_actionsWMR = &mouse_actions[3];

  //    static const actiondl_t mouse_actionsWMR[] = {
  //        // Spetial actions first
  //        {L"ZoneSnapTogg", L10nId::input_actions_zonesnaptog },
  //        {L"SnapTogg",     L10nId::input_actions_snaptogg },
  //        // Then the common mouse actions
  //        COMMON_ACTIONS
  //
  //        {L"Nothing",     L10nId::input_actions_nothing },
  //        {NULL, 0}
  //    };

  // Scroll
  static const struct {
    int control;               // Same control
    const wchar_t* option[5];  // Prim/alt/TTB/WM/WR
  } mouse_wheels[] = {
      {IDC_SCROLL, {L"Scroll", L"ScrollB", L"ScrollT", L"ScrollM", L"ScrollR"}},
      {IDC_HSCROLL,
       {L"HScroll", L"HScrollB", L"HScrollT", L"HScrollM", L"HScrollR"}}};

  static const actiondl_t scroll_actions[] = {
      {L"AltTab", L10nId::InputActionAltTab},
      {L"Volume", L10nId::InputActionVolume},
      {L"Transparency", L10nId::InputActionTransparency},
      {L"Zoom", L10nId::InputActionZoom},
      {L"Zoom2", L10nId::InputActionZoom2},
      {L"Lower", L10nId::InputActionLower},
      {L"Roll", L10nId::InputActionRoll},
      {L"Maximize", L10nId::InputActionMaximize},
      {L"NPStacked", L10nId::InputActionNStacked},
      {L"NPStacked2", L10nId::InputActionNStacked2},
      {L"HScroll", L10nId::InputActionHScroll},
      {L"NPLayout", L10nId::InputActionNPLayout},
      {L"Nothing", L10nId::InputActionNothing},
      {nullptr, 0}};

  // Hotkeys
  static const struct hk_struct hotclicks[] = {
      {IDC_MMB_HC, 0x04}, {IDC_MB4_HC, 0x05}, {IDC_MB5_HC, 0X06}, {0, 0}};

  static const DialogOption optlst[] = {
      {IDC_TTBACTIONSNA, T_BMK, 0, L"Input", "TTBActions", 0},
      {IDC_TTBACTIONSWA, T_BMK, 1, L"Input", "TTBActions", 0},
      {IDC_LONGCLICKMOVE, T_BOL, 0, L"Input", "LongClickMove", 0}};

  auto pnmh = reinterpret_cast<LPNMHDR>(lParam);

  // Body shared by several message paths (was a goto target).
  auto fill_actions = [&]() {
    unsigned i = 0;
    // Mouse actions
    int optoff = IsChecked(hwnd, IDC_MBA2)     ? 1
                 : IsChecked(hwnd, IDC_INTTB)  ? 2
                 : IsChecked(hwnd, IDC_WHILEM) ? 3
                 : IsChecked(hwnd, IDC_WHILER) ? 4
                                               : 0;

    // We must disable MoveUp and ResizeUp for the
    // While Moving/While resizing opts.
    EnableDlgItem(hwnd, IDC_MOVEUP, optoff < 3);
    EnableDlgItem(hwnd, IDC_RESIZEUP, optoff < 3);
    for (i = 0; i < std::size(mouse_buttons); i++) {
      FillActionDropListS(hwnd, mouse_buttons[i].control,
                          mouse_buttons[i].option[optoff],
                          optoff < 3 ? mouse_actions : mouse_actionsWMR);
    }
    if (optoff < 3) {
      for (i = 0; i < std::size(mouse_buttonsUP); i++) {
        FillActionDropListS(hwnd, mouse_buttonsUP[i].control,
                            mouse_buttonsUP[i].option[optoff], mouse_actionsUP);
      }
    }
    // Scroll actions, always the same.
    for (i = 0; i < std::size(mouse_wheels); i++) {
      FillActionDropListS(hwnd, mouse_wheels[i].control,
                          mouse_wheels[i].option[optoff], scroll_actions);
    }

    // Update text
    static const dialogstring_t strlst[] = {
        {IDC_MOUSE_BOX, L10nId::InputMouseBox},
        {IDC_MBA1, L10nId::InputMouseBtAc1},
        {IDC_MBA2, L10nId::InputMouseBtAc2},
        {IDC_INTTB, L10nId::InputMouseINTTB},
        {IDC_WHILEM, L10nId::InputMouseWhileM},
        {IDC_WHILER, L10nId::InputMouseWhileR},

        {IDC_LMB_HEADER, L10nId::InputMouseLMB},
        {IDC_MMB_HEADER, L10nId::InputMouseMMB},
        {IDC_RMB_HEADER, L10nId::InputMouseRMB},
        {IDC_MB4_HEADER, L10nId::InputMouseMB4},
        {IDC_MB5_HEADER, L10nId::InputMouseMB5},
        {IDC_SCROLL_HEADER, L10nId::InputMouseScroll},
        {IDC_HSCROLL_HEADER, L10nId::InputMouseHScroll},
        {IDC_MOVEUP_HEADER, L10nId::InputMouseMoveUp},
        {IDC_RESIZEUP_HEADER, L10nId::InputMouseResizeUp},
        {IDC_TTBACTIONS_BOX, L10nId::InputMouseTTBActionBox},
        {IDC_TTBACTIONSNA, L10nId::InputMouseTTBActionNA},
        {IDC_TTBACTIONSWA, L10nId::InputMouseTTBActionWA},

        {IDC_HOTCLICKS_BOX, L10nId::InputHotclicksBox},
        {IDC_HOTCLICKS_MORE, L10nId::InputHotclicksMore},
        {IDC_MMB_HC, L10nId::InputMouseMMBHC},
        {IDC_MB4_HC, L10nId::InputMouseMB4HC},
        {IDC_MB5_HC, L10nId::InputMouseMB5HC},
        {IDC_LONGCLICKMOVE, L10nId::InputMouseLongClickMove},
    };
    UpdateDialogStrings(hwnd, strlst, std::size(strlst));
  };

  if (msg == WM_INITDIALOG) {
    // Hotclicks buttons
    ReadDialogOptions(hwnd, optlst, std::size(optlst));
    CheckRadioButton(hwnd, IDC_MBA1, IDC_WHILER,
                     IDC_MBA1);  // Check the primary action
    CheckConfigHotKeys(hotclicks, hwnd, L"Hotclicks", L"");
  } else if (msg == WM_COMMAND) {
    int event = HIWORD(wParam);
    int id = LOWORD(wParam);
    if (id >= IDC_MBA1 && id <= IDC_WHILER) {
      // CheckRadioButton(hwnd, IDC_MBA1, IDC_WHILER, id); // Check the selected
      // action
      fill_actions();
      return FALSE;
    }

    if (event == 0 || event == CBN_SELCHANGE || event == CBN_EDITCHANGE) {
      PropSheet_Changed(g_cfgwnd, hwnd);
      have_to_apply = 1;
    }
  } else if (msg == WM_NOTIFY) {
    if (pnmh->code == PSN_SETACTIVE) {
      wchar_t txt[8];
      GetPrivateProfileString(L"Input", L"ModKey", L"", txt, std::size(txt),
                              inipath.c_str());
      EnableDlgItem(hwnd, IDC_MBA2, txt[0]);
      // Disable inside ttb
      EnableDlgItem(hwnd, IDC_INTTB,
                    IsChecked(hwnd, IDC_TTBACTIONSNA) ||
                        IsChecked(hwnd, IDC_TTBACTIONSWA));

      fill_actions();

    } else if (pnmh->code == PSN_APPLY && have_to_apply) {
      // Mouse actions, for all mouse buttons...
      unsigned i = 0;
      // Add 2 if in titlear add one for secondary action.
      // Mouse actions
      int optoff = IsChecked(hwnd, IDC_MBA2)     ? 1
                   : IsChecked(hwnd, IDC_INTTB)  ? 2
                   : IsChecked(hwnd, IDC_WHILEM) ? 3
                   : IsChecked(hwnd, IDC_WHILER) ? 4
                                                 : 0;
      for (i = 0; i < std::size(mouse_buttons); i++) {
        WriteActionDropListS(hwnd, mouse_buttons[i].control,
                             mouse_buttons[i].option[optoff],
                             optoff < 3 ? mouse_actions : mouse_actionsWMR);
      }
      if (optoff < 3) {
        for (i = 0; i < std::size(mouse_buttonsUP); i++) {
          WriteActionDropListS(hwnd, mouse_buttonsUP[i].control,
                               mouse_buttonsUP[i].option[optoff],
                               mouse_actionsUP);
        }
      }
      // Scroll
      for (i = 0; i < std::size(mouse_wheels); i++) {
        WriteActionDropListS(hwnd, mouse_wheels[i].control,
                             mouse_wheels[i].option[optoff], scroll_actions);
      }

      // Checkboxes...
      WriteDialogOptions(hwnd, optlst, std::size(optlst));
      // Hotclicks
      SaveHotKeys(hotclicks, hwnd, L"Hotclicks");
      UpdateSettings();
      have_to_apply = 0;
    }
  }
  return FALSE;
}
LRESULT WINAPI PickShortcutWinProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  static const UCHAR vkbl[] = {VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_SHIFT,
                               VK_LSHIFT,  VK_RSHIFT,   VK_MENU,     VK_LMENU,
                               VK_RMENU,   VK_LWIN,     VK_RWIN,     '\0'};
  //    static int was_enabled;

  switch (msg) {
      //    case WM_CREATE: {
      //        was_enabled = IsEnabled();
      //        if (was_enabled) UnhookSystem();
      //    } break;
    case WM_CHAR:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_SYSDEADCHAR:
    case WM_SYSCHAR:
      break;
    case WM_KEYUP: {
      if (!IsHotkeyy(static_cast<UCHAR>(wp), vkbl)) {
        HWND phwnd = GetParent(hwnd);
        SetDlgItemText(phwnd, IDC_SHORTCUTS_VK, ToHex(wp).c_str());

        CheckDlgButton(
            phwnd, IDC_SHIFT,
            GetKeyState(VK_SHIFT) & 0x8000 ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(
            phwnd, IDC_CONTROL,
            GetKeyState(VK_CONTROL) & 0x8000 ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(
            phwnd, IDC_WINKEY,
            GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000
                ? BST_CHECKED
                : BST_UNCHECKED);
        CheckDlgButton(
            phwnd, IDC_ALT,
            GetKeyState(VK_MENU) & 0x8000 ? BST_CHECKED : BST_UNCHECKED);
        DestroyWindow(hwnd);
      }
    } break;
    case WM_LBUTTONDOWN:
    case WM_KILLFOCUS: {
      DestroyWindow(hwnd);
    } break;

    case WM_DESTROY: {
      HWND phwnd = GetParent(hwnd);
      EnableDlgItem(phwnd, IDC_SHORTCUTS_PICK, 1);
      EnumChildWindows(phwnd, EnableAllControlsProc, 1);
      //        NMHDR lpn;
      //        lpn.hwndFrom = NULL; lpn.code = PSN_SETACTIVE;
      //        SendMessage(phwnd, WM_NOTIFY, 0, (LPARAM)&lpn);
      HWND sethwnd = GetDlgItem(phwnd, IDC_SHORTCUTS_SET);
      EnableWindow(sethwnd, TRUE);
      SetFocus(sethwnd);
      //        if (was_enabled) HookSystem();
    } break;
    default:
      return DefWindowProc(hwnd, msg, wp, lp);
  }
  return 0;
}
static const char* action_fl_param1_maps[ACPARAM_LAST] = {
    /* ACPARAM_NONE            */ nullptr,  // Nothing...
    /* ACPARAM_DIRECTION       */ "0LURD",  // 0 Left, Up, Right, Down
    /* ACPARAM_NUMBER          */ nullptr,  // Nothing
    /* ACPARAM_UPDOWN          */ "0UD",    // 0 Up, Down
    /* ACPARAM_UPDOWNSETTOGGLE */ "0UDST",  // 0 Up, Down, Set to, Toggle
};
static const short action_fl_strings_direction[] = {
    0, L10nId::WayLeft, L10nId::WayUp, L10nId::WayRight, L10nId::WayDown,
};
static const short action_fl_strings_updown_set_toggle[] = {
    0, L10nId::WayUp, L10nId::WayDown, L10nId::KwdSetTo, L10nId::KwdToggle,
};
static const struct {
  size_t num;
  const short* l10nIdx;
} action_fl_l10n_maps[ACPARAM_LAST] = {
    /* ACPARAM_NONE           */ {0, nullptr},
    /* ACPARAM_DIRECTION      */
    {std::size(action_fl_strings_direction), action_fl_strings_direction},
    /* ACPARAM_NUMBER         */ {0, nullptr},
    /* ACPARAM_UPDOWN         */ {3, action_fl_strings_updown_set_toggle},
    /* ACPARAM_UPDOWNSETTOGGLE*/
    {std::size(action_fl_strings_updown_set_toggle),
     action_fl_strings_updown_set_toggle},
};

INT_PTR CALLBACK AdvancedActionDlgProc(HWND hwnd, UINT msg, WPARAM wp,
                                       LPARAM lp) {
  switch (msg) {
    case WM_INITDIALOG: {
      auto* acp = reinterpret_cast<advancedActionParam_t*>(lp);
      SetWindowLongPtr(hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(lp));

      FillActionDropListS(hwnd, IDC_ACTIONP0, nullptr, acp->base_action_lst);
      SendDlgItemMessage(hwnd, IDC_ACTIONP0, CB_SETCURSEL,
                         static_cast<WPARAM>(-1), 0);

      SetWindowText(hwnd, l10n->TtlAdvancedAction);
      SetDlgItemText(hwnd, IDC_ACTIONT, l10n->TtlAction);

      // Copy localized OK, CANCEL button from parent
      HWND parent = GetAncestor(hwnd, GA_ROOTOWNER);
      if (parent) {
        wchar_t txt[32];
        if (GetDlgItemText(parent, IDOK, txt, std::size(txt)))
          SetDlgItemText(hwnd, IDOK, txt);
        if (GetDlgItemText(parent, IDCANCEL, txt, std::size(txt)))
          SetDlgItemText(hwnd, IDCANCEL, txt);
      }

      return TRUE; /* Autofocus first element in dialog */
    } break;

    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (id == IDOK) {
        auto* acp = reinterpret_cast<advancedActionParam_t*>(
            GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (!acp) return 0;
        ZeroObject(acp->outbuf);
        // Get the text from the action string display to return
        GetDlgItemText(hwnd, IDC_ACTIONTSTRING, acp->outbuf,
                       std::size(acp->outbuf) - 1);
        EndDialog(hwnd, IDOK);
        return TRUE;
      }
      if (id == IDCANCEL) {
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
      } else {
        int ev = HIWORD(wp);
        int update_action_string =
            (ev == CBN_SELCHANGE || ev == CBN_EDITCHANGE || ev == EN_UPDATE) &&
            (id == IDC_ACTIONP0 || id == IDC_ACTIONP1 || IDC_ACTIONP2);

        if (update_action_string) {
          auto* acp = reinterpret_cast<advancedActionParam_t*>(
              GetWindowLongPtr(hwnd, GWLP_USERDATA));
          wchar_t acstr[128], param1[16], param2[32];
          acstr[0] = L'\0';

          // ACTION PARAM0: base action
          int p0idx = GetActionStringFromDropList(hwnd, IDC_ACTIONP0,
                                                  acp->base_action_lst, acstr,
                                                  std::size(acstr) - 8);
          BYTE param1_type = acp->base_action_lst[p0idx].param1_type;
          BYTE param2_type = acp->base_action_lst[p0idx].param2_type;

          if (id == IDC_ACTIONP0 /*|| id == IDC_ACTIONP1*/) {
            // Adjust visibility/settings of param 1 and param 2.
            EnableDlgItem(hwnd, IDC_ACTIONP1, p0idx >= 0 && param1_type != 0);
            EnableDlgItem(hwnd, IDC_ACTIONP2, p0idx >= 0 && param2_type != 0);
            SetDlgItemText(hwnd, IDC_ACTIONP2,
                           L"");  // Clear number content

            // Fill Drop List according to PARAM0 of the action.
            HWND ctrl = GetDlgItem(hwnd, IDC_ACTIONP1);
            ComboBox_ResetContent(ctrl);
            for (size_t i = 0; i < action_fl_l10n_maps[param1_type].num; i++) {
              short l10nIdx = action_fl_l10n_maps[param1_type].l10nIdx[i];
              const wchar_t* str = l10nIdx == 0 ? L"---" : l10n->Get(l10nIdx);
              ComboBox_AddString(ctrl, str);
            }
          }

          // ACTION PARAM1: direction flags
          param1[0] = L'0';
          param1[1] = L'\0';
          if (action_fl_param1_maps[param1_type]) {
            // This action type can have parameter1.
            int direction =
                SendDlgItemMessage(hwnd, IDC_ACTIONP1, CB_GETCURSEL, 0, 0);
            if (direction < 0) {
              GetDlgItemText(hwnd, IDC_ACTIONP1, param1, std::size(param1));
            } else if (direction <
                       lstrlenA(action_fl_param1_maps[param1_type])) {
              // Put the correct character in the action string.
              // We use a single character for each kind of param
              param1[0] = action_fl_param1_maps[param1_type][direction];
              param1[1] = L'\0';
            }
          }
          // ACTION PARAM2 number...
          int param2len =
              GetDlgItemText(hwnd, IDC_ACTIONP2, param2, std::size(param2));

          // Add PARAM1 if needed
          if ((*param1 && *param1 != L'0') || (param2len && *param2)) {
            AppendString(acstr, std::size(acstr), L"_");
            AppendString(acstr, std::size(acstr), param1);
            // Add PARAM2 if needed
            if (param2len && *param2) {
              AppendString(acstr, std::size(acstr), L"_");
              AppendString(acstr, std::size(acstr), param2);
            }
          }

          // show the whole string
          SetDlgItemText(hwnd, IDC_ACTIONTSTRING, acstr);
          return TRUE;
        }
      }
    } break;

    case WM_SYSCOMMAND: {
      if (wp == SC_CLOSE) {
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
      }
    } break;

  } /* end switch(msg) */

  return FALSE; /* non processed */
}
}  // namespace altsnap::app
