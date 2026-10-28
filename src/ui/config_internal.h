// Internal interface shared by the modules split out of config.cc.
#ifndef ALTSNAP_CONFIG_INTERNAL_H_
#define ALTSNAP_CONFIG_INTERNAL_H_

#include <algorithm>
#include <variant>

#include "app/altsnap.h"
#include "app/languages.h"
#include "app/tray.h"
#include "common/ini_section.h"
#include "hooks/hooks.h"
#include "resource.h"
#include "ui/config.h"

namespace altsnap::app {

// Types and constants shared by the modules.
struct dialogstring {
  short idc;
  short l10nidx; /* const wchar_t *const helpstr; */
};

using dialogstring_t = dialogstring;

// How a dialog control maps onto an ini option.
// Upstream: opttype.
enum OptionKind : UCHAR {
  T_BOL = 0,  // check box <-> 0/1
  T_BMK = 1,  // check box <-> one bit of an integer option
  T_STR = 2,  // text field <-> string option
};

// One control of a settings page. Upstream: optlst (its `def` member was a
// `void*` that held either an integer or a string).
struct DialogOption {
  short idc;
  OptionKind kind;
  UCHAR bit;  // T_BMK: which bit of the option
  const wchar_t* section;
  const char* name;
  std::variant<int, const wchar_t*> default_value;  // int, or string for T_STR
};

struct hk_struct {
  unsigned control;
  unsigned vkey;
};

struct tagActiondl {
  const wchar_t* action;
  short l10nidx;
  BYTE param1_type;
  BYTE param2_type;
};

using actiondl_t = tagActiondl;

struct tagAdvancedActionParam {
  const actiondl_t* base_action_lst;
  wchar_t outbuf[64];
};

using advancedActionParam_t = tagAdvancedActionParam;

enum {
  ACPARAM_NONE = 0,
  ACPARAM_DIRECTION,
  ACPARAM_NUMBER,
  ACPARAM_UPDOWN,
  ACPARAM_UPDOWNSETTOGGLE,
  ACPARAM_LAST,
};

// State shared by the modules (defined in config.cc).
extern HWND g_cfgwnd;

// Defined in config.cc.
void UpdateStrings();

}  // namespace altsnap::app

#endif  // ALTSNAP_CONFIG_INTERNAL_H_
