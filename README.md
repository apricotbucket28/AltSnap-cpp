# AltSnap
Fork from Stefan Sundin's AltDrag.

Check the Wiki: https://github.com/RamonUnch/AltSnap/wiki

Original documentation: https://stefansundin.github.io/altdrag/doc/

Note that the documentation is not 100% accurate because it is a fork.
Read the changelog for more details.

It allows you to move and resize windows by using the Alt key and clicking wherever on the window instead of relying on very precise clicking.
This behavior is very common on Linux distributions and is not actually hard to implement on Windows.

This fork tries to keep a version up to date with minimal amount of bugs while keeping it feature-rich.

It is oriented towards all Windows users from Windows NT 4 to Windows 11, even though it is mostly tested on Windows XP and Windows 10.

Main differences:
To simplify the code greatly the Hooks windows feature was removed, it allowed you have windows snapping while dragging them normally. It required however to inject a dll in every application and induced thus an obvious security risk. The amount of mess added to the code just for this feature was substantial and in addition forced to have both a 32bits and a 64bits version of the program running at the same time.

This version injects nothing into other applications. This means you do not have to worry whether you have a 32 or a 64bit operating system.

Another feature that was disabled is focus on typing, that was too much unusable for me to even start testing, so I removed it.

Otherwise this has a much simpler source code, added a few extra options, such as transparent windows dragging, Maximize action, pause process options, more blacklists for finer control of AltSnap etc.

Finally it fixed a ton of undesired behavior and bugs from the original AltDrag.

WHAT'S NEW

Many new features can be seen in the option dialog box, however some of them are only available through editing the AltSnap.ini file (middle click on tyhe tray icon for this).

# Build

The supported core is Windows x64, C++23, MinGW-w64 ABI, with a static C++
runtime and no exceptions/RTTI. Install CMake 3.25+, Ninja, and a current x64
MinGW-w64 GCC or LLVM-MinGW toolchain (including windres). Put its bin directory
on PATH. Standalone MSVC-targeting LLVM is not a core toolchain.

From a terminal with that toolchain available:

```powershell
$env:CXX = "clang++" # LLVM-MinGW; use "g++" for GCC
cmake --workflow --preset release
cmake --install build/core/release
```

The workflow configures, builds, and runs CTest with ALTSNAP_BUILD_TESTS=ON.
Use preset `debug` and install from `build/core/debug` for a Debug core.
Individual steps are `cmake --preset release`, `cmake --build --preset release`,
and `ctest --preset release`. Install assembles the runnable core under
`build/stage/release` (or `debug`); ordinary builds do not stage files.
A plain `cmake -S . -B <dir>` remains supported, including MinGW cross-toolchain
files. Use `CMakeUserPresets.json` for local compiler/toolchain paths; it is ignored.
Core builds need no Visual Studio, NuGet, or WinUI packages.

The optional WinUI preview uses native MSBuild, not the core CMake toolchain.
Install Visual Studio 2026 C++ desktop tools: v145 14.50.35717, Windows SDK
10.0.26100.0, and the x64 14.50.35710 CRT redistributable files. These UI-only
pins live in `src/settings_ui/Directory.Build.props`; NuGet package versions
live in `packages.config` and its standard native project imports. The preview
runs on Windows 11 build 26100+. Its only supported configuration is Release x64.

In a Visual Studio Developer PowerShell, from the repository root:

```powershell
msbuild Settings.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Release /p:Platform=x64
msbuild src/settings_ui/SettingsModelTests.vcxproj /t:Test /p:Configuration=Release /p:Platform=x64
msbuild src/settings_ui/AltSnap.Settings.vcxproj /t:Publish /p:Configuration=Release /p:Platform=x64
```

MSBuild restores pinned native packages using `NuGet.Config`; no standalone
NuGet download is needed. Build and intermediate outputs are isolated per
project/configuration under `build/msvc` and `build/obj`. Build does not run
tests implicitly. Publish builds the UI and copies its complete self-contained
runtime layout to `build/stage/release/Settings`. MSBuild's selected redist
supplies the app-local CRT; the SDK supplies runtime dependencies, and their
resource indexes are merged into `resources.pri` for unpackaged XAML startup.

Run `build/stage/release/AltSnap.exe -winui` after both installs. Keep the entire
staged tree together. For a custom destination, pass `--prefix <directory>` to
CMake install and `/p:PublishDir=<directory>/Settings/` to MSBuild. Assemble releases in an
empty destination so removed dependencies cannot survive an older build;
install/publish preserve an existing AltSnap.ini. The old Makefile-era scripts
and installer workflow are retired; the staged tree is the distribution input.

CI uses the same commands in the corresponding toolchain environments; core
Debug/Release jobs need no UI dependencies. Fail on each command's nonzero exit
status. Cache `build/nuget` by `packages.config`, and use clean build and staging
directories when changing toolchains or SDK/package versions. Clean-machine
WinUI deployment acceptance remains tracked in [docs/UI_MODERNIZATION.md](docs/UI_MODERNIZATION.md).
