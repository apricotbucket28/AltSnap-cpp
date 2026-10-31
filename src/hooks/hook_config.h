// Interface of hook_config.cc (internal to hooks.dll).
#ifndef ALTSNAP_HOOK_CONFIG_H_
#define ALTSNAP_HOOK_CONFIG_H_

#include "hooks/hooks_internal.h"
#include "hooks/overlay/trans_window.h"

namespace altsnap::hooks {

// Reads all the options of the ini file into `conf`, the blacklists and the
// input sequences. Upstream: the first half of Load().
[[nodiscard]] bool ReadConfig(
    const wchar_t* inipath,
    bool (*create_trans_window)(const IniSection&) = CreateTransWin);

// Registers the keyboard shortcuts of the [KBShortcuts] section.
void RegisterAllHotkeys(const wchar_t* inipath);

// Successful registrations only; the span is invalidated by registration or
// teardown.
std::span<const action_t> RegisteredHotkeys();
void UnregisterAllHotkeys();

// Releases the memory held by the blacklists and the input sequences.
void FreeBlacklists();
void FreeAllInputSequences();

}  // namespace altsnap::hooks

#endif  // ALTSNAP_HOOK_CONFIG_H_
