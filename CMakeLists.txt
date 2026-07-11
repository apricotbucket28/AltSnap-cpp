cmake_minimum_required(VERSION 3.20)

project(AltSnap C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
  message(FATAL_ERROR "AltSnap targets Windows only (build natively with MinGW-w64, "
                         "or cross-compile with a MinGW-w64 toolchain file).")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This fork targets x86_64 only. Configure with a 64-bit MinGW-w64 toolchain.")
endif()

# Standalone LLVM installs (e.g. C:\Program Files\LLVM) default to the MSVC
# ABI/target triple on Windows, which defines _MSC_VER for Windows-SDK
# compatibility. hooks.c has real `#if defined(_MSC_VER)` branches meant
# ONLY for cl.exe/clang-cl (they use __FUNCDNAME__ and a linker pragma that
# plain clang.exe does not implement) - a MSVC-ABI clang trips that branch
# without being able to satisfy it, and fails deep in the build with a
# confusing pragma error. Catch it here instead, with an actionable message.
include(CheckCSourceCompiles)
check_c_source_compiles("
#if defined(_MSC_VER)
#error MSVC-ABI-compiler-detected
#endif
int main(void){return 0;}
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

if(NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$")
  message(WARNING "Original build used GCC/MinGW-w64. Untested with ${CMAKE_C_COMPILER_ID}.")
endif()

# --- Resource compilation ------------------------------------------------
# GNU windres (MinGW-w64 GCC, or llvm-mingw's own *-windres wrapper)
# compiles .rc -> a COFF .o that's linked like any other object.
# llvm-mingw's clang normally pairs with llvm-rc instead, which mimics
# MSVC's rc.exe: it produces a .res file that lld links directly,
# with no separate compile-to-object step.
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
  get_filename_component(_altsnap_clang_bin ${CMAKE_C_COMPILER} DIRECTORY)
  find_program(ALTSNAP_RC_COMPILER
        NAMES llvm-rc llvm-rc.exe
        HINTS ${_altsnap_clang_bin}
        DOC "llvm-rc from the same llvm-mingw distribution as the compiler")
  if(NOT ALTSNAP_RC_COMPILER)
    message(FATAL_ERROR "Could not find llvm-rc next to ${CMAKE_C_COMPILER}. "
                             "Set -DALTSNAP_RC_COMPILER=/path/to/llvm-rc.exe explicitly.")
  endif()
else()
  enable_language(RC)
endif()

# altsnap_add_rc(<target> <rcfile> [EXTRA_DEPENDS dep...])
# Adds an .rc as a resource input to <target>, using whichever pipeline
# matches the active compiler.
function(altsnap_add_rc target rcfile)
  cmake_parse_arguments(ARG "" "" "EXTRA_DEPENDS" ${ARGN})
  if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    get_filename_component(_name ${rcfile} NAME_WE)
    set(_res ${CMAKE_CURRENT_BINARY_DIR}/${_name}.res)
    add_custom_command(
            OUTPUT ${_res}
            # TODO: remove constant /I E:/llvm-mingw/include
            COMMAND ${ALTSNAP_RC_COMPILER} /I ${CMAKE_CURRENT_SOURCE_DIR} /I E:/llvm-mingw/include /FO ${_res} ${rcfile}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${rcfile} ${ARG_EXTRA_DEPENDS}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "llvm-rc ${rcfile}"
            VERBATIM
        )
    # A prebuilt .res, not something to compile - hand it straight to the linker.
    set_source_files_properties(${_res} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    target_sources(${target} PRIVATE ${_res})
  else()
    target_sources(${target} PRIVATE ${rcfile})
    set_source_files_properties(${rcfile} PROPERTIES OBJECT_DEPENDS "${ARG_EXTRA_DEPENDS}")
  endif()
endfunction()

# --- Flags shared by both targets, mirroring MakefileX64 ---------------
# NOTE: kept verbatim from the old build for now. Re-evaluate once the
# custom entry points / -nostdlib setup is removed in the next step
# (some of these, e.g. -fno-stack-protector, exist for size and should
# probably be reversed once safety is the actual goal).
set(ALTSNAP_C_FLAGS
    -Os
    -fno-stack-check
    -fno-stack-protector
    -fno-ident
    -fomit-frame-pointer
    -fmerge-all-constants
    -mno-stack-arg-probe
    -momit-leaf-frame-pointer
    -march=x86-64
    -mtune=generic
    -fshort-enums
    -fno-exceptions
    -fno-asynchronous-unwind-tables
    -D__USE_MINGW_ANSI_STDIO=0
    -Wp,-D_FORTIFY_SOURCE=2
)

set(ALTSNAP_WARNINGS
    -Wall -Wformat-security -Wstrict-overflow -Wsign-compare
    -Wempty-body -Wignored-qualifiers -Wuninitialized -Wtype-limits -Woverride-init
    -Wno-multichar -Wno-attributes
    -Wnull-dereference -Wno-unused-function -Wshadow
    -pedantic -Wc++-compat
)

# GCC-only: clang either doesn't implement these or spells them differently
# (e.g. -fno-dwarf2-cfi-asm, -Wclobbered, -Wlogical-op, -Wduplicated-cond/
# -branches, -Wstack-usage don't exist in clang; -fgcse-sm/-las/-fno-plt are
# silently ignored by clang anyway).
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  list(APPEND ALTSNAP_C_FLAGS -fgcse-sm -fgcse-las -fno-plt -fno-dwarf2-cfi-asm)
  list(APPEND ALTSNAP_WARNINGS -Wclobbered -Wlogical-op -Wduplicated-cond
        -Wduplicated-branches -Wstack-usage=4096)
endif()

set(ALTSNAP_LINK_FLAGS
    -s
    -Wl,-dynamicbase,-nxcompat,--no-seh
    -Wl,--disable-runtime-pseudo-reloc,--enable-auto-import,--disable-stdcall-fixup
)

# --- hooks.dll -----------------------------------------------------------
add_library(hooks SHARED
    hooks.c
    hooks.h
)
altsnap_add_rc(hooks hooks.rc)
# Included via #include, not compiled separately (unity build) -
# listed here only so they show up in IDEs and are tracked as deps.
set_source_files_properties(zones.c snap.c unfuck.h nanolibc.h
    PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(hooks PRIVATE zones.c snap.c unfuck.h nanolibc.h)

set_target_properties(hooks PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hooks"
    SUFFIX ".dll"
)
target_compile_options(hooks PRIVATE ${ALTSNAP_C_FLAGS} ${ALTSNAP_WARNINGS})
target_link_options(hooks PRIVATE
    -mdll
    ${ALTSNAP_LINK_FLAGS}
)
target_link_libraries(hooks PRIVATE msvcrt kernel32 user32 gdi32 winmm)

# --- AltSnap.exe ---------------------------------------------------------
# NOTE: window.rc, AltSnap.exe.manifest and resource.h are pulled in via
# #include / RT_MANIFEST *inside* altsnap.rc - they are not separate
# windres inputs. Only altsnap.rc gets compiled; the rest are tracked
# below as HEADER_FILE_ONLY so edits still trigger a rebuild.
add_executable(AltSnap
    altsnap.c
    hooks.h
    languages.h
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
    tray.c config.c languages.c unfuck.h nanolibc.h
    window.rc AltSnap.exe.manifest resource.h
    PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(AltSnap PRIVATE
    tray.c config.c languages.c unfuck.h nanolibc.h
    window.rc AltSnap.exe.manifest resource.h)

set_target_properties(AltSnap PROPERTIES OUTPUT_NAME "AltSnap")
target_compile_options(AltSnap PRIVATE ${ALTSNAP_C_FLAGS} ${ALTSNAP_WARNINGS})
target_link_options(AltSnap PRIVATE
    -mwindows
    -Wl,--tsaware,--disable-reloc-section
    ${ALTSNAP_LINK_FLAGS}
)
target_link_libraries(AltSnap PRIVATE comctl32 advapi32 shell32 msvcrt kernel32 user32 gdi32 winmm)

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
