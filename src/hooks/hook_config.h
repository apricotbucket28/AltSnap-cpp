// Interface of hook_config.cc (internal to hooks.dll).
#ifndef ALTSNAP_HOOK_CONFIG_H_
#define ALTSNAP_HOOK_CONFIG_H_

#include "hooks/hooks_internal.h"

namespace altsnap::hooks {

// Reads all the options of the ini file into `conf`, the blacklists and the
// input sequences. Upstream: the first half of Load().
void ReadConfig(const wchar_t* inipath);

// Registers the keyboard shortcuts of the [KBShortcuts] section.
void registerAllHotkeys(const wchar_t* inipath);

// Releases the memory held by the blacklists and the input sequences.
void freeblacklists();
void freeallinputSequences();

}  // namespace altsnap::hooks

#endif  // ALTSNAP_HOOK_CONFIG_H_
