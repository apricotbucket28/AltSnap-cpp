# C++ modernization notes

Minimum OS: Windows 10 version 1607. Build: C++23, Unicode (`UNICODE`/`_UNICODE`),
C++ runtime linked statically (hooks.dll is injected into other processes).

## Layout
Two programs, each a set of small translation units in its own namespace
(`altsnap::hooks` for hooks.dll, `altsnap::app` for AltSnap.exe, `altsnap` for the
shared `IniSection`/`Blacklist`). `DllMain` and `WinMain` stay global.

`hooks.dll` (exports exactly `Load` and `Unload`; internals in `hooks_internal.h`):
- `hooks.cc` Load/Unload, timers and the main message proc
- `hook_config.cc` ReadConfig(), one function per ini section
- `keyboard_hook.cc`, `mouse_hook.cc` the two low-level hook procedures
- `movement.cc` start/finish of a move or resize; `click_actions.cc` what clicks do
- `window_actions.cc` the actions themselves; `window_menu.cc`, `pin_window.cc`,
  `volume.cc` (COM), `trans_window.cc` (hollow frame + cursor), `send_input.cc`
- `window_info.cc` queries, `window_enum.cc` enumeration, `window_move.cc` executing
  moves, `snap_move.cc` edge/aero snapping geometry, `input_state.cc` key state,
  `mouse_move.cc`, `snap.cc`, `zones.cc`

`AltSnap.exe`: `altsnap.cc` (WinMain, tray window), `tray.cc`, `languages.cc`,
`config.cc` (property sheet) plus one file per settings page (`general_page.cc`,
`mouse_page.cc`, `keyboard_page.cc`, `blacklist_page.cc`, `advanced_page.cc`,
`about_page.cc`), `test_window.cc` and `dialog_util.cc` (shared dialog helpers).

Every header compiles on its own. The module split was done with a small script
(parse top-level declarations, assign them to modules, generate headers from the
real definitions), so the headers cannot drift from the code.

## Testing
- `cmake -DALTSNAP_BUILD_TESTS=ON ...` builds `unit_tests` (string helpers,
  `IniSection`, `Blacklist`); run with `ctest`. When cross-compiling on Linux set
  `-DCMAKE_CROSSCOMPILING_EMULATOR=wine64`.
- A `-DLOG_STUFF` build writes `ad.log`; starting it under Wine and diffing the
  log against an older commit is a cheap regression check for config parsing.

## Design notes
- `IniSection` (ini_section.h) is the only way config is read: one snapshot of a
  `[section]`, `GetString()/GetInt()` lookups. Replaces raw double-NUL buffers and
  the fixed 1800-character KBShortcuts buffer.
- `Blacklist` (blacklist.h) owns its patterns as `std::wstring`; matching and
  whitelist mode are explicit instead of pointer-into-buffer tricks.
- Translations: `LANGUAGE_MAP` still generates `struct strings`, but strings are
  addressed by `L10nId::X` constants and pointer-to-member tables, not by
  reinterpreting the struct as an array.
- `winutil.h` (upstream: `unfuck.h`) is what is left of the old compat layer.

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
