# Settings UI modernization

Architecture recommendation, 2026-10-31; repository inspected at `f50f0ea`.
This is a plan, not an implemented interface. See [MODERNIZATION.md](../MODERNIZATION.md)
for existing C++ modernization and hook lifecycle design.

## Recommendation and alternatives

Add an on-demand, unpackaged `AltSnap.Settings.exe` using WinUI 3 and C++/WinRT.
Keep `AltSnap.exe` as the resident tray/hook host and `hooks.dll` as its private
engine. The host owns configuration writes and runtime commands; settings owns
its window, editable draft, and presentation. Retain the INI format and legacy
dialog during migration. This adds a small process protocol and a second build
toolchain, but isolates XAML startup, failures, dependencies, and thread affinity
from the input path.

| Alternative | Benefit | Cost / assessment |
| --- | --- | --- |
| In-process WinUI settings, created on demand | Direct calls; avoids process IPC | A host port brings MSVC/SDK changes into the utility. A separate MSVC UI DLL could retain the MinGW host through a C ABI bridge, but adds dispatcher/runtime lifetime integration and shares UI failures with hooks. Lazy creation does not provide process isolation. |
| Host WinUI XAML Islands in the existing process | Incremental control replacement | Supported hosting technique, but adds dispatcher, focus, HWND, and lifetime integration while retaining both UI systems and host dependencies. Little benefit for six small pages. |
| Companion writes INI and posts existing reload message | Smallest initial integration; offline editing | Independent writers race with tray/zone writes; ambiguous path/instance selection; no reliable apply result or runtime status. Useful only as a disposable feasibility experiment. |
| Companion plus a narrow host settings interface **(chosen)** | Clear authority, lightweight resident process, independent UI build | Requires bounded IPC, instance routing, and elevated-host testing. Keep the interface small and expand by feature. |

See Microsoft's [Islands hosting model](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/host-controls-existing-desktop-apps).
The chosen split needs no service or additional engine process.

## What exists today

| Area | Actual behavior and source |
| --- | --- |
| Startup and executable | [WinMain / WindowProc](../src/app/altsnap.cc) resolves the INI **before** CLI handling, checks elevation and previous instances, possibly relaunches elevated, loads language, creates a hidden top-level `AltSnap` window, initializes tray, enables hooks, and runs a Win32 message loop. `-c`, `-r`, `-h`, `-lX`, and action commands forward to an existing instance; `-m` / `Advanced/MultipleInstances` bypass that discovery. |
| DLL boundary | `HookSystem()` loads adjacent `hooks.dll` by absolute path. `Load(HWND, const wchar_t*)` returns an in-process `WNDPROC`; `Unload()` returns `BOOL`. [hooks.cc](../src/hooks/hooks.cc) owns low-level hooks, timers, overlays, config loading, and the worker session. `DllMain` only records the module and disables thread notifications. |
| Lifecycle and communication | Host thread calls the returned callback for hotkeys, layouts, and zone queries. `WM_GETZONES` returns borrowed `RECT*`; `WM_STACKLIST` carries a callback. These are private in-process calls, **not IPC**; see [hooks.h](../src/hooks/hooks.h). DLL-to-host messages request tray updates and native action/Unicode menus. |
| Settings shell | [config.cc](../src/ui/config.cc) opens a modal common-controls `PropertySheet` with six pages, tracked by `g_cfgwnd`; repeat opening selects/focuses a page. [window.rc](../window.rc) supplies fixed dialog layouts; [dialog_util.cc](../src/ui/dialog_util.cc) binds HWND controls to INI values. There is no UI-independent settings model/controller. |
| Persistence | `FindIniPath()` chooses `<host-executable-name>.ini` beside the executable if present; otherwise the same filename under `%APPDATA%/AltSnap`, seeded from adjacent `.dni` when absent. It falls back to the portable path if APPDATA is unavailable. Selection/creation failures are largely unchecked. The path is a host global. |
| Engine configuration | [hook_config.cc](../src/hooks/hook_config.cc) owns writable `g_config`, exposed internally as `const config& conf`; reads sections, actions, blacklists, sequences, zones, and registers hotkeys. Parsing also creates overlays and consults OS state. `config` in [hooks_internal.h](../src/hooks/hooks_internal.h) is effective runtime state, not a serializable settings schema. |
| Resources and localization | [languages.cc](../src/app/languages.cc) loads `Lang/*.ini`, falls back to built-in English from `LANGUAGE_MAP`, and owns global translation storage. [tray.cc](../src/app/tray.cc) reads theme icons from `Themes/`. Localization is also used by runtime menus and test windows. |

[AltSnap.exe.manifest](../AltSnap.exe.manifest) declares `asInvoker`, common-controls
v6, and PerMonitorV2/PerMonitor DPI awareness. Startup has no XAML/WinRT application
initialization; the volume action manages its own scoped COM initialization.

The DLL is loaded into the host; the inspected hook installation uses
`WH_KEYBOARD_LL` / `WH_MOUSE_LL`, not injection into target applications.
Some modernization/build comments describe it as injected; retain its static
runtime constraint.
Microsoft documents the [low-level hook execution context](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelmouseproc).

### Behavior that a control-for-control rewrite could lose

- [General](../src/ui/general_page.cc): snapping/resize bit fields, language,
  HKCU `Run` registration targeting **AltSnap.exe**, and elevation by replacing
  the host. Autostart is registry state; elevated scheduled startup is separate
  ([sch_On.bat](../sch_On.bat), [sch_Off.bat](../sch_Off.bat), [AltSnap.xml](../AltSnap.xml)).
- [Mouse](../src/ui/mouse_page.cc): primary/alternate/titlebar/moving/resizing
  mappings, wheel actions, move/resize release actions, hotclicks, long-click
  move, editable unknown action strings, and an advanced action parameter dialog.
  [input_options.cc](../src/hooks/input_options.cc) supports additional buttons
  6–20, secondary contextual bindings, and upward-wheel inheritance.
- [Keyboard](../src/ui/keyboard_page.cc): activation key lists, modifier lists,
  grab actions, Scroll Lock bits, Unicode hold menu, foreground/pointed-window
  shortcuts, key capture, and parameterized actions. Shortcut **Set** writes and
  reloads immediately; other edits use Apply. Key-list editing preserves custom
  keys outside the displayed checkboxes.
- [Blacklist](../src/ui/blacklist_page.cc): five editors plus a crosshair picker
  reporting process, title/class, styles, hit tests, caption buttons, rectangles,
  and handles. [blacklist.h](../src/hooks/blacklist.h) has additional categories
  and whitelist/wildcard semantics.
- [Advanced](../src/ui/advanced_page.cc) and [About](../src/ui/about_page.cc):
  geometry, thresholds, transparency, behavior flags, FancyZones integration,
  test-window launch, version/license, and translation credits.
- [Test windows](../src/ui/test_window.cc) are also the zone editor. Tray code
  enumerates **host-thread** `AltSnap-Test` windows to save/close them; editing a
  layout copies DLL rectangles into these windows. Keep these tools in the host.
- Many `[Performance]`, `[Zones]`, `[Input]`, `[Advanced]`, and blacklist options
  exist only in [AltSnap.dni](../AltSnap.dni) and engine readers. Preserve them
  and the Open INI workflow without requiring a new control for every option.

Defaults have three distinct sources: absent-key engine defaults, dialog
defaults, and the shipped `.dni`. For example, `StickyResize` and `MDI` default
to 0 in the engine but 1 in the General page; shipped `MDI=1`.
`AutoSnap` is 0 when missing but 2 in `.dni`; `AeroSpeedTau` is 64 in engine/default
file but 32 in the Advanced page. Empty, missing, and explicit values matter.
Preserve untouched values and characterize discrepancies before choosing any
intentional behavior change; do not initialize/save the whole file from UI defaults.

`IniSection` is only a read snapshot. Despite the broader wording in
MODERNIZATION.md, direct profile reads remain in app/UI code, including a
fixed-size shortcut enumeration buffer.

### Apply is more than reloading the DLL

Each dirty legacy page writes individual keys with `WritePrivateProfileString`
and calls `UpdateSettings()`. One property-sheet Apply can restart hooks several
times. Writes generally ignore failures and are not a transaction.
`UpdateSettings()` unloads/loads only when enabled; it does not enable a disabled
utility. Host language updates and test-window refreshes happen separately in
page code. `ScrollLockState` is explicitly updated by the keyboard page;
`InitTray()` caches `MaxLayouts`, `LayoutNumber`, and icons; `HookSystem()` refreshes
`UseZones`, `SnapGap`, and `ACMenuItems`. A generic reload therefore does not refresh
all host state. Theme changes currently require a full process restart.

Load/Unload and callbacks belong to the host window thread. Shutdown closes
worker admission, removes hooks/timers/hotkeys, drains/joins, then releases shared
state; the join can block on a foreign window and services synchronous messages.
[hook_shutdown.h](../src/app/hook_shutdown.h) blocks reentrant modal/reload commands.
New settings requests must honor this lifecycle; an IPC timeout must never force
DLL unload or allow a second concurrent apply.

## Proposed ownership and interfaces

```text
AltSnap.Settings.exe (WinUI window + draft/view models; normally medium integrity)
    bounded local requests / owned replies
AltSnap.exe (settings service + INI repository + native tools; tray/main thread)
    existing Load / Unload / private callback
hooks.dll (effective config, hooks, worker, overlays)
```

1. **Host settings service:** explicitly owned by the host lifetime. Its repository
   receives the resolved absolute INI path; it never derives a path from the
   companion executable. Serialize service, legacy-page, and tray/zone writes
   through this authority. Allow only one settings frontend per host initially.
2. **Settings values:** introduce a small UI-independent area under `src/settings/`
   for owning values, option identity/types, validation, patches, and protocol
   encoding. Add definitions as pages migrate. Keep raw value/presence separate
   from displayed/effective values; encode bit changes as masks, preserving
   unrelated bits. Preserve engine defaults/conversions; extract shared parsing
   as needed. Snapshot/preview reads must not call effectful `ReadConfig()`.
3. **WinUI presentation:** source/XAML under `src/settings_ui/`; per-window draft
   and view models live on the UI STA/dispatcher. IPC runs asynchronously and
   returns owned results; marshal updates to that dispatcher and cancel pending
   work on close. Use standard controls, responsive NavigationView pages, system
   light/dark/high-contrast resources, keyboard navigation, and accessible labels.
   Start with the existing page groups. Keep resource assets at the project root.
4. **Runtime adapter:** host code coordinates apply, host caches/localization,
   test-window refresh, and exactly one hook reload per successful batch. Keep
   native menus, overlays, zone tools, and hook operations out of view models.
   Check shutdown success before re-enabling. The UI never loads `hooks.dll` or
   includes `hooks_internal.h`.

### Minimum process contract

Use a local named pipe with versioned, length-bounded requests and request IDs.
Encode fields explicitly, independent of native struct/STL/pointer layouts.
Start with handshake/status, read snapshot, and apply patch; add named tool,
startup, and elevation commands as pages migrate. Identify host instance,
protocol capabilities, config path/revision, and result. Reject incompatible
versions. After timeout/disconnect, query outcome before retrying a mutation.

The pipe worker owns transport handles and bounded queues. It validates/decodes
requests and queues owning commands to the host thread; a private wake message
carries no caller-owned payload. The host thread never waits for a pipe client.
Stop admission and cancel/join transport work during host shutdown. Keep lifecycle
operations on the existing owner thread; reply busy while stopping/applying.

Restrict access to the local logon session, reject remote clients, and validate
peer/instance association. Explicitly support medium-integrity clients of an
elevated host; test both access rights and integrity policy. Bind mutations to the
host's configuration and allowlisted operations, excluding arbitrary write paths
or executable commands. See
[named-pipe access guidance](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights).

Preserve scalar CLI/window-message automation; adapt `WM_OPENCONFIG`/About to
companion activation/page selection. Existing messages lack structured results;
keep DLL pointer messages private. Tray launch targets that exact host; direct
launch must identify the installation/instance. With no host, offer to start it
before enabling hooks. Defer offline editing.
`-m` means the first `FindWindow` match is insufficient routing. Hosts sharing an
INI need a per-config write lock plus revision checks, while each retains its own
runtime status/reload; never claim every instance has adopted a save.

Keep settings normally unelevated. Launching directly from an elevated tray
inherits elevation unless an explicit unelevated launch path is used; prove that
path in the first milestone. Elevate/restart the **host** for operations requiring
it, reconnect to the replacement, and preserve the draft on cancellation/failure.

### Save and failure semantics

- Snapshot includes persisted values/presence, revision, runtime enabled state,
  and host-specific settings. Draft changes survive navigation. Use explicit
  Apply/OK/Cancel initially; retain an explicit immediate Set operation for
  shortcuts until its semantics are deliberately redesigned.
- Validate the complete patch before writing. Compare its base revision against
  current file contents under the writer lock; report a conflict instead of
  overwriting newer edits. Preserve unknown sections/keys, custom action strings,
  hidden key-list entries, and uninterpreted bits. Permit untouched legacy values
  even when they are outside a new control's editable range.
- Begin with checked profile-API key updates, matching current encoding/parsing
  behavior. Do not reconstruct the INI from the engine struct. Test Unicode and
  existing encodings/comments; profile APIs are not a byte-preserving document
  editor. Manual external editors do not honor our lock, so a residual external
  write race remains; expose Reload and detect changes before apply.
- Return persistence and activation outcomes separately, including partial-write
  failures. Re-read after write failure and report the actual persisted state;
  retain the draft for recovery. Multi-key profile writes cannot promise atomic
  rollback. A temporary-copy/replace writer is a later option if needed, after
  proving profile-cache, encoding, metadata, and recovery behavior.
- On successful persistence, refresh affected host state and reload hooks once
  if previously enabled. Surface failed re-enable/hotkey conflicts rather than
  reporting success from a posted message. Registration diagnostics need a new
  host-facing result path because `Load` currently reports only overall startup
  success and hotkey failures are mostly logged. Distinguish saved, active, and
  restart-required settings; startup registry operations report their own result.

## Reuse and replacement boundaries

| Component | Migration treatment |
| --- | --- |
| Hook actions, movement, snapping, zones, worker, Load/Unload | Reuse unchanged in the first slice. Retain lifecycle ownership and private DLL contract; broader engine cleanup remains separate. |
| `common/ini_section.*`, `strutil.h`, `text_format.h` | Reuse relevant readers/key-list/rectangle helpers. Compile shared sources independently per toolchain; several helpers are Win32-dependent, not portable libraries. |
| `ACTION_MAP` / `MapActionW` in `hooks.h`, `input_options.*`, blacklist parser | Reuse semantics and tests. Extract needed value/parsing declarations from broad hook headers into the settings/common layer incrementally; do not link the engine into the UI for a dropdown. Preserve action strings as persistence IDs, not translated labels or enum ordinals. |
| `FindIniPath`, profile writes, startup/elevation helpers | New explicit host interfaces and checked results. Registry helpers currently derive the executable from their own process; copying them into the companion would register/elevate the wrong executable. |
| Localization files/default strings | Reuse existing keys, English fallback, credits, and Language/Auto semantics through an owning catalog adapter with explicit resource root. Current loader depends on app globals/executable location. Each process owns its strings; do not share mutable pointers or import all of `languages.cc` into WinUI. New UI text needs additional translations. |
| `config.cc`, page dialog procedures, `DialogOption`, `dialog_util`, dialog templates | Temporary fallback/reference; eventually replace with XAML/view models and settings services. Control-ID tables are not the new model schema. Remove only retired dialog resources; tray/icons/manifests remain needed by the host. |
| Test-window and picker functionality | Preserve native host test/zone windows. Separate the picker query/result logic from property-sheet control access; expose owned diagnostic results when the Blacklist page migrates. Visual replacement can follow later. |

## Build, OS, and delivery

[CMakeLists.txt](../CMakeLists.txt) is the current build: Windows x64, C++23,
MinGW ABI (explicitly rejects `_MSC_VER`), no exceptions/RTTI, static C++ runtime;
two production targets plus optional unit/hook/worker tests. There is no current
WinUI project, MSIX manifest, or checked-in CI workflow. `mkmsvc.bat` still names
removed root `.c` files; README/ARM64/Makefile release instructions are historical,
not proof that the current C++ tree builds with MSVC or ARM64.

Use a separate x64 MSBuild `.vcxproj` with MSVC, Windows SDK, pinned stable
Windows App SDK and C++/WinRT NuGet packages for settings. Microsoft's
[FAQ](https://learn.microsoft.com/en-us/windows/apps/get-started/windows-developer-faq)
states that WinUI XAML projects require MSBuild. Generic C++/WinRT compiler
portability does not establish MinGW support for the XAML build pipeline.
Keep core CMake independently buildable; orchestrate/stage the two outputs rather
than changing the core compiler checks or passing MinGW libraries into MSVC.
Record a UI-only toolchain/exception policy exception before adding the target:
recommend C++ exceptions for projection errors, caught at UI/async boundaries;
retain core no-exception/no-RTTI flags. Verify C++23 mode and UI RTTI needs.
See [C++/WinRT error handling](https://learn.microsoft.com/en-us/windows/uwp/cpp-and-winrt-apis/error-handling).

Recommend Windows 11 24H2 (build 26100) or newer as the new UI product floor;
this is a product simplification, not a WinUI technical requirement. WinUI itself
supports Windows 10 1809+, so current 1607 cannot be retained for the new UI.
See [WinUI platform requirements](https://learn.microsoft.com/en-us/windows/apps/winui).
The core can retain its current compile floor during coexistence. Pin exact SDK,
compiler, and serviced OS test versions in the first implementation milestone.

Start with unpackaged, self-contained output in a sibling `Settings/` folder,
retaining host/INI/Lang/Themes layout. This fits ZIP/NSIS delivery, at the cost of
size and AltSnap-managed runtime servicing. Framework-dependent delivery is
smaller with shared servicing but needs prerequisites/bootstrap; MSIX offers
identity/updates but changes install, writable-data, and startup assumptions.
Packaging is independent of the process split. See
[deployment tradeoffs](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/deploy-overview).

Ship the complete generated output, including XAML/resource/runtime dependencies;
self-contained Windows App SDK does not automatically settle C++ CRT deployment.
Use the documented C++ hybrid CRT approach or explicitly deploy the required CRT,
and retain the unpackaged registration-free WinRT initialization. Verify on a
clean machine without developer tools/shared runtime. See
[self-contained C++ deployment](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps).

Reconcile release plumbing during delivery:
`stage` omits `AltSnap.dni` although first-run startup/NSIS use it;
[ziprelease.bat](../ziprelease.bat) expects `AltSnap.ini` (not tracked) and differs
from staging; [altsnap.nsi](../altsnap.nsi) has a hardcoded version/file list and
only shuts down the host. Stage defaults intentionally, preserve an existing INI,
include companion output/assets, close both processes for updates, and update
uninstall handling. A missing/broken companion must leave hooks/tray working and
offer the legacy settings fallback during rollout.

## Phased implementation and gates

1. **First milestone: one complete settings slice behind an opt-in launcher.**
   Build/run the unpackaged C++/WinRT companion on a clean Windows 11 machine.
   Extract the host path/repository seam only as needed; implement handshake,
   status/read, and revision-checked `General/AutoFocus` apply through the host.
   Route the legacy writer for that key through the same authority and prevent
   concurrent frontends/ambiguous shared-file writers. Prove a
   single reload, disabled-state preservation, save/reload failure reporting,
   repeated activation, and elevated-host/unelevated-UI communication. Retain
   legacy settings as default.
2. **General/settings foundations.** Inventory each migrated control's key,
   default source, bits/ranges, and application scope in code/test fixtures.
   Introduce the draft model, localization adapter, checked host cache refresh,
   startup/elevation commands, and shared writer routing. Extend per-config
   conflict handling before exposing more mutations.
3. **Feature parity in small page increments.** Port Mouse/Keyboard, then
   Blacklist/Advanced/About. Cover custom strings, advanced action parameters,
   key capture/Set semantics, picker diagnostics, and existing test/zone tools.
   Preserve INI-only options; resolve mapping discrepancies with focused tests.
4. **Default switch and retirement.** Exercise install/update/portable/recovery
   paths, polish accessibility/DPI/theme behavior, and measure host idle footprint
   plus settings cold start. Make companion the default only after parity gates;
   remove retired property-sheet code/resources after a fallback period.

Extend existing
[unit_tests](../tests/unit_tests.cc), [hook_tests](../tests/hook_tests.cc), and
[worker_tests](../tests/worker_tests.cc) where appropriate and add focused model,
repository, protocol, and host-adapter tests. Cover absent/empty/unknown values,
default discrepancies, bit preservation, binding suffixes/wheel fallback,
Unicode/long paths, two stale drafts, partial writes, malformed/oversized protocol
input, disconnect during apply, shutdown/reload reentrancy, and host replacement.
Configure `ALTSNAP_BUILD_TESTS=ON`, build affected targets, and run CTest.
Build/test the MSVC shared-source copy and WinUI project separately.

Native acceptance must exercise real hooks while settings opens/closes/applies,
enable/disable, elevated targets/UAC cancellation, keyboard conflicts/capture,
zone save/order, mixed-DPI monitors, display changes, Explorer tray recreation,
keyboard-only operation, Narrator, high contrast, and light/dark changes. Existing
hook lifecycle tests provide a useful baseline but do not validate a WinUI UI or
installed dependency layout.

## Decisions still requiring evidence

- Confirm pinned toolchain/SDK/CRT output and unelevated launch plus pipe access
  from an elevated host; these are first-milestone gates.
- Confirm the Windows 11-only product floor versus a Windows 10 compatibility
  offering, and whether self-contained size warrants a second deployment flavor.
- Decide multi-instance UX and shared-file reload policy before enabling editing
  across multiple hosts; the first slice should require an explicit target and
  reject ambiguous/shared-writer scenarios it cannot coordinate.
- Decide whether shortcut Set should remain immediate long term, and whether
  atomic multi-key persistence/offline editing justify later writer work.
- Resolve default/control mapping inconsistencies individually. For example,
  Mouse's resizing entry for MB5 names `MB5M` while the engine reads `MB5R`.
  Preserve documented engine keys; characterize the old UI behavior before fixing
  it. These findings were inspected, not runtime-reproduced in this analysis.

Validation for this document: source/build/release/test inspection and Microsoft
documentation review only. No implementation files changed; no build, CTest, or
interactive runtime checks were performed for this documentation-only pass.
