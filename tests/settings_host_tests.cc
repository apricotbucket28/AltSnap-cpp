// Explicit native-desktop acceptance test; launches real hooks in an isolated
// portable installation. Run with the path to the freshly built AltSnap.exe.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "settings/protocol.h"

namespace {
using namespace altsnap::settings;
void Check(bool ok, const char* expression, int line) {
  if (!ok) {
    std::fprintf(stderr, "%d: %s (error %lu)\n", line, expression,
                 GetLastError());
    std::exit(1);
  }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)
struct Process {
  PROCESS_INFORMATION info{};
  explicit Process(const std::wstring& command) {
    STARTUPINFOW startup{sizeof(startup)};
    std::wstring arguments = command;
    CHECK(CreateProcessW(nullptr, arguments.data(), nullptr, nullptr, FALSE, 0,
                         nullptr, nullptr, &startup, &info));
  }
  ~Process() {
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
  }
};
HWND HostWindow(DWORD pid) {
  struct Search {
    DWORD pid;
    HWND window;
  } search{pid, nullptr};
  EnumWindows(
      [](HWND window, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        wchar_t name[32];
        GetClassNameW(window, name, 32);
        if (owner == search.pid && std::wstring_view(name) == L"AltSnap") {
          search.window = window;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  return search.window;
}
Snapshot Exchange(DWORD pid, Operation operation, uint64_t revision = 0,
                  Patch patch = {}) {
  static uint32_t id = 0;
  const Request request{++id, operation, revision, patch};
  HANDLE pipe = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 100 && pipe == INVALID_HANDLE_VALUE;
       ++attempt) {
    pipe = CreateFileW(PipeName(pid).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                       nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) Sleep(50);
  }
  CHECK(pipe != INVALID_HANDLE_VALUE);
  DWORD mode = PIPE_READMODE_MESSAGE, count = 0;
  CHECK(SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr));
  auto bytes = Encode(request);
  CHECK(WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &count,
                  nullptr));
  bytes.resize(kMaxReply);
  CHECK(ReadFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &count,
                 nullptr));
  Snapshot snapshot;
  CHECK(Decode(std::span(bytes.data(), count), id, snapshot));
  uint8_t ack = 1;
  CHECK(WriteFile(pipe, &ack, 1, &count, nullptr));
  CloseHandle(pipe);
  return snapshot;
}
int Client(DWORD pid) {
  Snapshot initial = Exchange(pid, Operation::kRead);
  CHECK(initial.result == Result::kOk && initial.enabled);
  CHECK(initial.values[Index(Option::kAutoSnap)].effective == 99);
  Draft draft;
  draft.Load(initial.values);
  draft.Set(Option::kSmartSizing, 0);
  draft.Set(Option::kSmartCandidates, 1);
  draft.Set(Option::kStickyResize, 2);
  draft.Set(Option::kAutoSnap, 2);
  Snapshot saved =
      Exchange(pid, Operation::kApply, initial.revision, draft.Changes());
  CHECK(saved.result == Result::kOk && saved.enabled);
  CHECK(saved.values[Index(Option::kSmartSizing)].raw == L"262");
  CHECK(saved.values[Index(Option::kStickyResize)].raw == L"2");
  CHECK(saved.values[Index(Option::kAutoSnap)].raw == L"2");
  CHECK(Exchange(pid, Operation::kRead).revision == saved.revision);
  draft.Load(saved.values);
  draft.Set(Option::kFancyZone, 1);
  draft.Set(Option::kSnapGap, uint32_t(-8));
  draft.Set(Option::kAeroMaxSpeed, 1024);
  draft.Set(Option::kAeroDoubleClickShift, 1);
  saved = Exchange(pid, Operation::kApply, saved.revision, draft.Changes());
  CHECK(saved.result == Result::kOk && saved.enabled);
  CHECK(saved.values[Index(Option::kSnapGap)].raw == L"-8");
  CHECK(saved.values[Index(Option::kFancyZone)].raw == L"1");
  CHECK(saved.values[Index(Option::kAeroMaxSpeed)].raw == L"1024");
  const auto opened = Exchange(pid, Operation::kTestWindow);
  CHECK(opened.result == Result::kOk && opened.revision == saved.revision);
  struct TestSearch {
    DWORD pid;
    HWND window = nullptr;
  } search{pid};
  EnumWindows(
      [](HWND window, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<TestSearch*>(data);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        wchar_t name[64];
        GetClassNameW(window, name, 64);
        if (owner == search.pid && std::wstring_view(name) == L"AltSnap-Test") {
          search.window = window;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  CHECK(search.window);
  PostMessageW(search.window, WM_CLOSE, 0, 0);
  CHECK(Exchange(pid, Operation::kApply, initial.revision,
                 SingleChange(Option::kAero, 0))
            .result == Result::kConflict);
  // The existing host command contract toggles hooks on its owner thread.
  const HWND window = HostWindow(pid);
  CHECK(window);
  DWORD_PTR result = 0;
  CHECK(SendMessageTimeoutW(window, WM_COMMAND, 401 /* CMD_TOGGLE */, 0,
                            SMTO_ABORTIFHUNG, 10000, &result));
  saved = Exchange(pid, Operation::kRead);
  CHECK(saved.result == Result::kOk && !saved.enabled);
  saved = Exchange(pid, Operation::kApply, saved.revision,
                   SingleChange(Option::kAero, 0));
  CHECK(saved.result == Result::kOk && !saved.enabled);
  CHECK(saved.values[Index(Option::kAero)].raw == L"0");
  CHECK(SendMessageTimeoutW(window, WM_COMMAND, 401, 0, SMTO_ABORTIFHUNG, 10000,
                            &result));
  saved = Exchange(pid, Operation::kRead);
  CHECK(saved.result == Result::kOk && saved.enabled);
  // External edits invalidate the complete-file revision.
  CHECK(WritePrivateProfileStringW(L"Unknown", L"Keep", L"external",
                                   saved.path.c_str()));
  CHECK(Exchange(pid, Operation::kApply, saved.revision,
                 SingleChange(Option::kAero, 1))
            .result == Result::kConflict);
  saved = Exchange(pid, Operation::kRead);
  CHECK(Exchange(pid, Operation::kApply, saved.revision,
                 SingleChange(Option::kAero, 1))
            .result == Result::kOk);
  std::puts(
      "Native host: batch apply, reload, conflicts, bit preservation, disabled "
      "apply passed");
  return 0;
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 3 && std::string_view(argv[1]) == "--client")
    return Client(static_cast<DWORD>(std::strtoul(argv[2], nullptr, 10)));
  CHECK(argc == 2);
  std::wstring executable(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, executable.data(), 32768);
  CHECK(length && length < executable.size());
  executable.resize(length);
  const auto directory =
      std::filesystem::path(executable).parent_path() /
      (L"host-settings-test-" + std::to_wstring(GetCurrentProcessId()));
  std::filesystem::create_directories(directory / L"Settings");
  const std::filesystem::path source = std::filesystem::absolute(argv[1]);
  std::filesystem::copy_file(source, directory / L"AltSnap.exe");
  std::filesystem::copy_file(source.parent_path() / L"hooks.dll",
                             directory / L"hooks.dll");
  const auto client = directory / L"Settings" / L"AltSnap.Settings.exe";
  std::filesystem::copy_file(executable, client);
  const auto ini = directory / L"AltSnap.ini";
  CHECK(WritePrivateProfileStringW(L"General", L"SmartAero", L"261",
                                   ini.c_str()));
  CHECK(
      WritePrivateProfileStringW(L"General", L"AutoSnap", L"99", ini.c_str()));
  Process host(L"\"" + (directory / L"AltSnap.exe").wstring() + L"\" -h -m");
  Process test(L"\"" + client.wstring() + L"\" --client " +
               std::to_wstring(host.info.dwProcessId));
  const DWORD waited = WaitForSingleObject(test.info.hProcess, 30000);
  // Always request orderly host shutdown, even when a client assertion fails.
  PostMessageW(HostWindow(host.info.dwProcessId), WM_CLOSE, 0, 0);
  CHECK(WaitForSingleObject(host.info.hProcess, 15000) == WAIT_OBJECT_0);
  CHECK(waited == WAIT_OBJECT_0);
  DWORD exit_code = 1;
  CHECK(GetExitCodeProcess(test.info.hProcess, &exit_code));
  return static_cast<int>(exit_code);
}
