cmake_minimum_required(VERSION 3.20)

project(AltSnap CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
  message(FATAL_ERROR "AltSnap targets Windows only (build natively with MinGW-w64, "
                         "or cross-compile with a MinGW-w64 toolchain file).")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This fork targets x86_64 only. Configure with a 64-bit MinGW-w64 toolchain.")
endif()

# Standalone LLVM installs (e.g. C:\Program Files\LLVM) default to the MSVC
# ABI/target triple on Windows, which defines _MSC_VER for Windows-SDK
# compatibility. src/hooks/hooks.cc has real `#if defined(_MSC_VER)` branches meant
# ONLY for cl.exe/clang-cl (they use __FUNCDNAME__ and a linker pragma that
# plain clang.exe does not implement) - a MSVC-ABI clang trips that branch
# without being able to satisfy it, and fails deep in the build with a
# confusing pragma error. Catch it here instead, with an actionable message.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#if defined(_MSC_VER)
#error MSVC-ABI-compiler-detected
#endif
int main(){return 0;}
" ALTSNAP_COMPILER_IS_NOT_MSVC_ABI)
if(NOT ALTSNAP_COMPILER_IS_NOT_MSVC_ABI)
  message(FATAL_ERROR
        "Your C compiler defines _MSC_VER, i.e. it's targeting the MSVC ABI "
        "(this happens with the standalone LLVM Windows installer's clang.exe, "
        "which defaults to x86_64-pc-windows-msvc). This project needs a "
        "MinGW-w64-targeted compiler. Fix: install MSYS2's clang64 environment "
        "(pacman -S mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-cmake "
        "mingw-w64-clang-x86_64-ninja) and configure from the 'MSYS2 CLANG64' "
        "shell, or use MSYS2's plain mingw-w64 GCC instead.")
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
  message(WARNING "Original build used GCC/MinGW-w64. Untested with ${CMAKE_CXX_COMPILER_ID}.")
endif()

# --- Resource compilation ------------------------------------------------
# Resources are compiled with the toolchain's windres (CMAKE_RC_COMPILER),
# which produces a COFF object that is linked like any other object file.
enable_language(RC)

# altsnap_add_rc(<target> <rcfile> [EXTRA_DEPENDS dep...])
# Adds an .rc file to <target> and rebuilds it when an #included file changes.
function(altsnap_add_rc target rcfile)
  cmake_parse_arguments(ARG "" "" "EXTRA_DEPENDS" ${ARGN})
  target_sources(${target} PRIVATE ${rcfile})
  set_source_files_properties(${rcfile} PROPERTIES OBJECT_DEPENDS "${ARG_EXTRA_DEPENDS}")
endfunction()

set(ALTSNAP_CXX_FLAGS
    -DUNICODE -D_UNICODE -DNOMINMAX
    # Minimum supported OS: Windows 10 version 1607 (NTDDI_WIN10_RS1).
    -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000002
    -fno-rtti
    -fno-ident
    -mno-stack-arg-probe
    -march=x86-64
    -mtune=generic
    -fshort-enums
    -fno-exceptions
    -D__USE_MINGW_ANSI_STDIO=0
    -Wp,-D_FORTIFY_SOURCE=2
)

set(ALTSNAP_WARNINGS
    -Wall -Wformat-security -Wstrict-overflow -Wsign-compare
    -Wempty-body -Wignored-qualifiers -Wuninitialized -Wtype-limits
    -Wno-multichar -Wno-attributes
    -Wnull-dereference -Wno-unused-function -Wshadow
    -pedantic
    -Wno-writable-strings -Wno-tautological-constant-out-of-range-compare # TODO: revisit later
)

# GCC-only: clang either doesn't implement these or spells them differently
# (e.g. -Wclobbered, -Wlogical-op, -Wduplicated-cond/
# -branches, -Wstack-usage don't exist in clang).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(APPEND ALTSNAP_WARNINGS -Wclobbered -Wlogical-op -Wduplicated-cond
        -Wduplicated-branches -Wstack-usage=4096)
endif()

# The C++ runtime is linked statically: hooks.dll is injected into other
# processes, where a dependency on libc++.dll/libstdc++-6.dll cannot be
# guaranteed to resolve.
set(ALTSNAP_LINK_FLAGS
    -static
    -Wl,-dynamicbase,-nxcompat
    -Wl,--disable-runtime-pseudo-reloc,--enable-auto-import,--disable-stdcall-fixup
)

# --- hooks.dll -----------------------------------------------------------
add_library(hooks SHARED
    src/hooks/hooks.cc
    src/hooks/command_dispatcher.cc
    src/hooks/command_dispatcher.h
    src/hooks/worker_command.h
    src/hooks/worker.cc
    src/hooks/worker.h
    src/hooks/window/window_info.cc
    src/hooks/window/window_enum.cc
    src/hooks/window/window_move.cc
    src/hooks/window/snap_move.cc
    src/hooks/input_state.cc
    src/hooks/overlay/trans_window.cc
    src/hooks/mouse_move.cc
    src/hooks/send_input.cc
    src/hooks/actions/volume.cc
    src/hooks/overlay/pin_window.cc
    src/hooks/actions/window_actions.cc
    src/hooks/actions/click_actions.cc
    src/hooks/overlay/window_menu.cc
    src/hooks/keyboard_hook.cc
    src/hooks/mouse_hook.cc
    src/hooks/movement.cc
    src/hooks/input_options.cc
    src/hooks/hook_config.cc
    src/hooks/window/window_info.h
    src/hooks/window/window_enum.h
    src/hooks/window/window_move.h
    src/hooks/window/snap_move.h
    src/hooks/input_state.h
    src/hooks/overlay/trans_window.h
    src/hooks/mouse_move.h
    src/hooks/send_input.h
    src/hooks/actions/volume.h
    src/hooks/overlay/pin_window.h
    src/hooks/actions/window_actions.h
    src/hooks/actions/click_actions.h
    src/hooks/overlay/window_menu.h
    src/hooks/keyboard_hook.h
    src/hooks/mouse_hook.h
    src/hooks/movement.h
    src/hooks/hook_config.h
    src/hooks/blacklist.cc
    src/hooks/blacklist.h
    src/common/ini_section.cc
    src/common/ini_section.h
    src/hooks/window/snap.cc
    src/hooks/window/zones.cc
    src/hooks/hooks.h
    src/hooks/hooks_internal.h
    src/hooks/window/snap.h
    src/hooks/window/zones.h
)
altsnap_add_rc(hooks hooks.rc)
target_sources(hooks PRIVATE src/common/winutil.h src/common/strutil.h)

set_target_properties(hooks PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hooks"
    SUFFIX ".dll"
)
# Sources include each other relative to src/ (e.g. "hooks/window/snap.h");
# resource.h lives next to the .rc files in the project root.
target_include_directories(hooks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(hooks PRIVATE ${ALTSNAP_CXX_FLAGS} ${ALTSNAP_WARNINGS})
target_link_options(hooks PRIVATE
    ${ALTSNAP_LINK_FLAGS}
)
target_link_libraries(hooks PRIVATE msvcrt kernel32 user32 gdi32 winmm dwmapi shcore ole32 dxva2)

# --- AltSnap.exe ---------------------------------------------------------
# NOTE: window.rc, AltSnap.exe.manifest and resource.h are pulled in via
# #include / RT_MANIFEST *inside* altsnap.rc - they are not separate
# windres inputs. Only altsnap.rc gets compiled; the rest are tracked
# below as HEADER_FILE_ONLY so edits still trigger a rebuild.
add_executable(AltSnap
    src/app/altsnap.cc
    src/app/hook_lifecycle.cc
    src/app/hook_lifecycle.h
    src/ui/dialog_util.cc
    src/ui/general_page.cc
    src/ui/mouse_page.cc
    src/ui/keyboard_page.cc
    src/ui/blacklist_page.cc
    src/ui/about_page.cc
    src/ui/advanced_page.cc
    src/ui/test_window.cc
    src/ui/config_internal.h
    src/ui/dialog_util.h
    src/ui/general_page.h
    src/ui/mouse_page.h
    src/ui/keyboard_page.h
    src/ui/blacklist_page.h
    src/ui/about_page.h
    src/ui/advanced_page.h
    src/ui/test_window.h
    src/common/ini_section.cc
    src/common/ini_section.h
    src/ui/config.cc
    src/app/languages.cc
    src/app/tray.cc
    src/app/altsnap.h
    src/ui/config.h
    src/hooks/hooks.h
    src/app/languages.h
    src/app/tray.h
)
altsnap_add_rc(AltSnap altsnap.rc EXTRA_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/window.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/resource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/AltSnap.exe.manifest
    ${CMAKE_CURRENT_SOURCE_DIR}/media/find.cur
    ${CMAKE_CURRENT_SOURCE_DIR}/media/find.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/media/icon.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/media/tray-disabled.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/media/tray-enabled.ico
)
set_source_files_properties(
    window.rc AltSnap.exe.manifest resource.h
    PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(AltSnap PRIVATE
    src/common/winutil.h src/common/strutil.h
    window.rc AltSnap.exe.manifest resource.h)

set_target_properties(AltSnap PROPERTIES OUTPUT_NAME "AltSnap")
target_include_directories(AltSnap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(AltSnap PRIVATE ${ALTSNAP_CXX_FLAGS} ${ALTSNAP_WARNINGS})
target_link_options(AltSnap PRIVATE
    -mwindows
    -Wl,--tsaware
    ${ALTSNAP_LINK_FLAGS}
)
target_link_libraries(AltSnap PRIVATE comctl32 advapi32 shell32 msvcrt kernel32 user32 gdi32 winmm dwmapi shcore ole32 dxva2)

# hooks.dll must exist next to AltSnap.exe at runtime (it's loaded, not linked).
add_dependencies(AltSnap hooks)
add_custom_command(TARGET AltSnap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:hooks> $<TARGET_FILE_DIR:AltSnap>
)

# --- Runtime staging -----------------------------------------------------
# Mirrors what altsnap.nsi / ziprelease.bat actually ship. AltSnap.exe and
# hooks.dll alone are NOT a working install: Lang/*.ini and translations
# are read from disk at runtime (GetPrivateProfileSection), and the
# erasmion theme icons are LoadImage'd from disk too, separate from the
# default icons embedded via altsnap.rc. Running straight out of the raw
# build dir with nothing else copied will "work" but silently run with
# no translations and no alternate theme - an easy false negative.
set(ALTSNAP_STAGE_DIR ${CMAKE_BINARY_DIR}/stage)

add_custom_target(stage ALL
    COMMENT "Assembling runtime layout in ${ALTSNAP_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ALTSNAP_STAGE_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:AltSnap> $<TARGET_FILE:hooks>
        ${ALTSNAP_STAGE_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/AltSnap.txt
        ${CMAKE_SOURCE_DIR}/AltSnap.xml
        ${CMAKE_SOURCE_DIR}/License.txt
        ${CMAKE_SOURCE_DIR}/sch_On.bat
        ${CMAKE_SOURCE_DIR}/sch_Off.bat
        ${ALTSNAP_STAGE_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Lang ${ALTSNAP_STAGE_DIR}/Lang
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Themes ${ALTSNAP_STAGE_DIR}/Themes
)
add_dependencies(stage AltSnap hooks)

# --- Unit tests ---------------------------------------------------------------
# cmake -DALTSNAP_BUILD_TESTS=ON ... && cmake --build build && ctest --test-dir build
# When cross-compiling (e.g. llvm-mingw on Linux) set CMAKE_CROSSCOMPILING_EMULATOR
# to wine64 to run them.
option(ALTSNAP_BUILD_TESTS "Build the unit tests" OFF)
if(ALTSNAP_BUILD_TESTS)
  enable_testing()
  add_executable(unit_tests tests/unit_tests.cc src/common/ini_section.cc src/hooks/blacklist.cc)
  target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(unit_tests PRIVATE ${ALTSNAP_CXX_FLAGS})
  target_link_options(unit_tests PRIVATE ${ALTSNAP_LINK_FLAGS})
  add_test(NAME unit_tests COMMAND unit_tests)

  # Compile the production hook implementation into the test executable so its
  # internal message boundary and native lifecycle (including real low-level
  # hooks) can be exercised without expanding the DLL's public exports.
  get_target_property(ALTSNAP_HOOK_TEST_SOURCES hooks SOURCES)
  list(FILTER ALTSNAP_HOOK_TEST_SOURCES INCLUDE REGEX "\\.cc$")
  add_executable(hook_tests tests/hook_tests.cc ${ALTSNAP_HOOK_TEST_SOURCES}
    src/app/hook_lifecycle.cc)
  target_include_directories(hook_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(hook_tests PRIVATE ${ALTSNAP_CXX_FLAGS} ${ALTSNAP_WARNINGS})
  target_link_options(hook_tests PRIVATE ${ALTSNAP_LINK_FLAGS})
  target_link_libraries(hook_tests PRIVATE msvcrt kernel32 user32 gdi32 winmm dwmapi shcore ole32 dxva2)
  add_test(NAME hook_tests COMMAND hook_tests)
  set_tests_properties(hook_tests PROPERTIES TIMEOUT 30)
  add_test(NAME hook_survival_tests COMMAND hook_tests --hook-survival)
  set_tests_properties(hook_survival_tests PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

  add_executable(hook_lifecycle_tests tests/hook_lifecycle_tests.cc src/app/hook_lifecycle.cc)
  target_include_directories(hook_lifecycle_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_options(hook_lifecycle_tests PRIVATE ${ALTSNAP_CXX_FLAGS} ${ALTSNAP_WARNINGS})
  target_link_options(hook_lifecycle_tests PRIVATE ${ALTSNAP_LINK_FLAGS})
  target_link_libraries(hook_lifecycle_tests PRIVATE kernel32 user32)
  add_test(NAME hook_lifecycle_tests COMMAND hook_lifecycle_tests)
  set_tests_properties(hook_lifecycle_tests PROPERTIES TIMEOUT 15)

  add_executable(worker_tests tests/worker_tests.cc src/hooks/worker.cc
    src/hooks/command_dispatcher.cc)
  target_include_directories(worker_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_options(worker_tests PRIVATE ${ALTSNAP_CXX_FLAGS} ${ALTSNAP_WARNINGS})
  target_link_options(worker_tests PRIVATE ${ALTSNAP_LINK_FLAGS})
  target_link_libraries(worker_tests PRIVATE kernel32 user32)
  add_test(NAME worker_tests COMMAND worker_tests)
  set_tests_properties(worker_tests PROPERTIES TIMEOUT 30)
endif()
