# Working on AltSnap

- Read `MODERNIZATION.md` before changing architecture; inspect the code to
  establish what is already complete. Keep changes focused and backportable
  where practical, preserving intended behavior and existing INI names/defaults.
- Use idiomatic C++23 and Google's C++ Style Guide (`.clang-format` is based on
  Google). Prefer explicit types, owning values, bounded interfaces, and RAII;
  avoid layout-dependent indexing and untyped payloads in new interfaces.
- Keep source under `src/`, includes relative to `src/`, and headers independently
  compilable. Keep resources in the project root. Preserve the app/hooks namespace
  boundaries and the DLL's `Load`/`Unload` exports.
- Make ownership and thread affinity explicit. Do not mistake namespaces or a
  class containing shared globals for an ownership design. Keep Win32 effects
  separate from testable parsing and decision logic when practical.
- Build constraints: Windows 10 1607+, x86-64, Unicode, C++23, MinGW-w64 ABI,
  static C++ runtime, no exceptions or RTTI. Keep `DllMain` minimal.
- Add focused regression tests for correctness changes. Configure with
  `ALTSNAP_BUILD_TESTS=ON`, build the affected targets, and run CTest. Validate
  runtime behavior when a change affects hooks, messages, or window interaction;
  report checks actually performed and any remaining limitations.
- Avoid additional documentation unless requested or necessary. Reference
  `MODERNIZATION.md` for design history instead of duplicating it.
