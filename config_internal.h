// Internal interface shared by the modules split out of config.cc.
#ifndef ALTSNAP_CONFIG_INTERNAL_H_
#define ALTSNAP_CONFIG_INTERNAL_H_

#include <algorithm>

#include "altsnap.h"
#include "config.h"
#include "hooks.h"
#include "ini_section.h"
#include "languages.h"
#include "resource.h"
#include "tray.h"

namespace altsnap::app {

// Types and constants shared by the modules.
struct dialogstring {
  short idc;
  short l10nidx; /* const wchar_t *const helpstr; */
};

using dialogstring_t = dialogstring;

enum opttype { T_BOL = 0, T_BMK = 1, T_STR = 2 };

struct optlst {
  short idc;
  UCHAR type;
  UCHAR bitN;
  const wchar_t* section;
  const char* name;
  const void* def;
};

using optlst_t = optlst;

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
