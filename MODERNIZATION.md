# C++ modernization notes

Minimum OS: Windows 10 version 1607. Build: C++23, Unicode (`UNICODE`/`_UNICODE`),
C++ runtime linked statically (hooks.dll is injected into other processes).

## Layout
All C++ sources live under `src/` and include each other relative to it, e.g.
`#include "hooks/window/snap.h"`. Resource files (`*.rc`, `resource.h`, the
manifest) stay in the project root next to the icons they reference.

```
src/common/         shared helpers: strutil.h, winutil.h, ini_section.{h,cc}
src/app/            AltSnap.exe core: altsnap (WinMain, tray window), tray, languages
src/ui/             settings dialog: config (property sheet), one file per page
                    (general, mouse, keyboard, blacklist, advanced, about),
                    test_window, dialog_util, config_internal.h
src/hooks/          hooks.dll core: hooks.cc (Load/Unload), hooks.h (API used by the
                    exe), hooks_internal.h, hook_config, blacklist, keyboard_hook,
                    mouse_hook, movement, mouse_move, input_state, send_input
src/hooks/actions/  what hotkeys and clicks do: window_actions, click_actions, volume
src/hooks/window/   window queries and geometry: window_info, window_enum,
                    window_move, snap_move, snap, zones
src/hooks/overlay/  windows AltSnap draws: window_menu, pin_window, trans_window
tests/              unit tests (see below)
```

Two programs, each a set of small translation units in its own namespace
(`altsnap::hooks` for hooks.dll, `altsnap::app` for AltSnap.exe, `altsnap` for the
shared `IniSection`/`Blacklist`). `DllMain` and `WinMain` stay global, and hooks.dll
exports exactly `Load` and `Unload`. Every header compiles on its own.

## Testing
- `cmake -DALTSNAP_BUILD_TESTS=ON ...` builds `unit_tests` (string helpers,
  `IniSection`, `Blacklist`); run with `ctest`. When cross-compiling on Linux set
  `-DCMAKE_CROSSCOMPILING_EMULATOR=wine64`.
- A `-DLOG_STUFF` build writes `ad.log`; starting it under Wine and diffing the
  log against an older commit is a cheap regression check for config parsing.

## Design notes
- `IniSection` (common/ini_section.h) is the only way config is read: one snapshot of a
  `[section]`, `GetString()/GetInt()` lookups. Replaces raw double-NUL buffers and
  the fixed 1800-character KBShortcuts buffer.
- `Blacklist` (hooks/blacklist.h) owns its patterns as `std::wstring`; matching and
  whitelist mode are explicit instead of pointer-into-buffer tricks.
- Translations: `LANGUAGE_MAP` still generates `struct strings`, but strings are
  addressed by `L10nId::X` constants and pointer-to-member tables, not by
  reinterpreting the struct as an array.
- `common/winutil.h` (upstream: `unfuck.h`) is what is left of the old compat layer.

## Done
- macro-free `strutil.h` (replaces nanolibc.h); direct Win32 linking, no
  GetProcAddress shims; thunks.cc removed
- STL containers / RAII instead of malloc/realloc/free and goto; COM via ComPtr
- bounded, format-checked logging; named casts; `wchar_t` and `L""` literals
- option tables fill `conf` by `offsetof` instead of walking the struct as an array
- `SegT`, `EnumOnce`: no more out-of-bounds pointer arithmetic / sentinel pointers

## Open
- `static` globals shared through `extern` (`state`, `conf`, ...): wrap in
  namespaces / classes
- `LANGUAGE_MAP` X-macro (kept as the single source of the string table)
- ~190 C arrays (`modernize-avoid-c-arrays`), ~100 narrowing conversions,
  the module headers are still wide (about 150 cross-module functions in hooks.dll)
- Win32 `A/W` macros (`GetWindowText` ...) instead of explicit `...W` calls;
  `lstrcmpiA`/`lstrlenA` remain for the narrow log text

If this tree is imported into an existing repo, run `git add --renormalize .` once.
