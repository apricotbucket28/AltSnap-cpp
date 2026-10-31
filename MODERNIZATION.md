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
- `conf` (the configuration) is a `const config&` for every module; only
  `hook_config.cc` owns the writable `g_config`. Values that change at runtime are
  not configuration and live with their owner: the active zone layout
  (`CurrentLayout()` in zones.cc) and "keep the mouse hook installed"
  (`SetKeepMouseHook()` in mouse_hook.cc).
- Settings pages describe their controls with `DialogOption` (typed default value),
  not a `void*` that held either an integer or a string.
- UI text is built with `std::wstring`/`std::format`, not fixed buffers:
  `common/text_format.h` holds the pure helpers (`FormatRect`/`ParseRect` for zone
  rectangles, `ToHex`, `RemoveAccelerator`, the key-list editing used by the
  keyboard page, the test-window log lines). They are covered by the unit tests,
  and were checked against the old implementations on ~130k random inputs.
- Paths: `GetExecutablePath()`/`GetExecutableDirectory()` (winutil.h) replace
  `GetModuleFileName` into a `MAX_PATH` buffer; the ini location is chosen by
  `FindIniPath()` and stored in a `std::wstring`.
- Naming follows the Google style: CamelCase functions, `kConstants`. Renamed
  upstream identifiers carry a `// Upstream: OldName` note; the string helpers
  from nanolibc.h are listed at the top of `common/strutil.h`.

## Worker lifecycle
- `Worker` owns its event, thread handle and queue. Initialize it before installing
  hooks; close admission, remove hooks/timers/hotkeys, and drain/join before
  releasing shared configuration or windows. Pending droppable tasks are limited
  to 64; control tasks bypass capacity but are rejected after admission closes.
  Adjacent mouse moves coalesce; window-move requests own their payloads.
- `Load`/`Unload` belong to the main window's thread. Backport the DLL and app
  caller together: `Unload` now returns `BOOL` to confirm an idle session before
  `FreeLibrary`. Shutdown services synchronous sends, so the app blocks commands
  that could enter modal loops or reload settings during the join. The app
  retains lifecycle requests and retries rejected stops on its owner-thread
  timer; replayable guarded messages are posted after shutdown, while menu
  requests belonging to a stopped session are discarded.
- The join deliberately has no timeout: foreign-window operations may block on a
  hung target, but releasing state under a live worker is unsafe. Preserve the
  existing synchronous positioning where subsequent border/MDI geometry reads
  depend on completion. The worker object is intentionally process-lived and its
  destructor never joins under loader lock; sessions require explicit cleanup.
- Mutable drag/input state, enumeration collections and per-drag geometry belong
  to the Load caller's thread, enforced by Session(). WorkerCommand is a closed
  value variant; the queue no longer accepts closures or pointer payloads. A
  private, presentation-independent command window executes stateful commands
  synchronously on that owner. The worker retains FIFO/coalescing and executes
  process-kill commands from captured arguments without reading session state.
- Finish is a barrier; accepted moves drain first, duplicate finishes collapse,
  and busy starts pass mouse input through as matched down/up pairs without
  changing the finishing drag. Gesture IDs discard stale deferred moves
  and combo/finish commands. Unload rejects reentry while commands or hook/window/timer callbacks are active.
  This confinement trades background drag execution for owner-thread execution:
  slow synchronous foreign-window operations can delay hook/input processing.
  Future background geometry/effects must use explicit snapshots and completion
  values, never access the session directly.

- Long-click grab attempts that encounter a finishing/running drag are cancelled
  without synthesizing a button release. A new click is required to try again;
  an old held click is never replayed onto a later target.

- The rehook timer also refreshes the keyboard hook every five seconds, even
  without a permanent mouse hook. Alternating callback slots keep only one
  handler active during replacement; failed installs retain the previous hook.

## Done
- macro-free `strutil.h` (replaces nanolibc.h); direct Win32 linking, no
  GetProcAddress shims; thunks.cc removed
- STL containers / RAII instead of malloc/realloc/free and goto; COM via ComPtr
- bounded, format-checked logging; named casts; `wchar_t` and `L""` literals
- option tables fill `conf` by `offsetof` instead of walking the struct as an array
- `SegT`, `EnumOnce`: no more out-of-bounds pointer arithmetic / sentinel pointers

## Open
- Remaining owner-thread module stores (overlay handles, zones, configuration)
  still expose broad internal interfaces; narrow these as their modules change.
- `LANGUAGE_MAP` X-macro (kept as the single source of the string table)
- ~190 C arrays (`modernize-avoid-c-arrays`), ~100 narrowing conversions,
  the module headers are still wide (about 150 cross-module functions in hooks.dll)
- Win32 `A/W` macros (`GetWindowText` ...) instead of explicit `...W` calls;
  `lstrcmpiA`/`lstrlenA` remain for the narrow log text
