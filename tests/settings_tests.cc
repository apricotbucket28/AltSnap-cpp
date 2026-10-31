#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "settings/apply.h"
#include "settings/repository.h"
namespace {
void Check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "line %d: %s (Win32 error %lu)\n", line, expression,
                 GetLastError());
    std::exit(1);
  }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)
}  // namespace
int main() {
  using namespace altsnap::settings;
  Request request{73, Operation::kApply, 0x123456789abcdef0ull, true}, decoded;
  auto bytes = Encode(request);
  CHECK(Decode(bytes, decoded));
  CHECK(decoded.id == request.id && decoded.revision == request.revision &&
        decoded.auto_focus);
  CHECK(!Decode(std::span(bytes.data(), bytes.size() - 1), decoded));
  bytes.push_back(0);
  CHECK(!Decode(bytes, decoded));
  bytes.pop_back();
  for (size_t offset : {0u, 4u, 12u, 24u, 28u}) {
    auto bad = bytes;
    bad[offset] = 255;
    CHECK(!Decode(bad, decoded));
  }
  Snapshot snapshot{Result::kConflict,
                    0xfedcba9876543210ull,
                    false,
                    true,
                    true,
                    L"C:\\設定\\AltSnap.ini",
                    L"custom"},
      restored;
  bytes = Encode(snapshot, 73);
  CHECK(Decode(bytes, 73, restored));
  CHECK(restored.path == snapshot.path && restored.raw == snapshot.raw &&
        restored.revision == snapshot.revision);
  CHECK(!Decode(bytes, 74, restored));
  auto oversized = bytes;
  oversized.resize(kMaxReply + 1);
  CHECK(!Decode(oversized, 73, restored));
  bytes[36] = 255;
  CHECK(!Decode(bytes, 73, restored));
  wchar_t temp[MAX_PATH], filename[MAX_PATH];
  CHECK(GetTempPathW(MAX_PATH, temp));
  CHECK(GetTempFileNameW(temp, L"ast", 0, filename));
  const std::wstring path = std::wstring(filename) + L"-設定.ini";
  HANDLE unicode_file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_NEW, 0, nullptr);
  CHECK(unicode_file != INVALID_HANDLE_VALUE);
  const std::wstring unicode_text = L"\xfeff[Unknown]\r\nUnicode=設定\r\n";
  DWORD written = 0;
  CHECK(WriteFile(unicode_file, unicode_text.data(),
                  static_cast<DWORD>(unicode_text.size() * sizeof(wchar_t)),
                  &written, nullptr));
  CloseHandle(unicode_file);
  Repository repository(path);
  auto initial = repository.Read();
  CHECK(initial.result == Result::kOk && !initial.present &&
        !initial.auto_focus);
  CHECK(WritePrivateProfileStringW(L"Unknown", L"Keep", L"unchanged",
                                   path.c_str()));
  CHECK(repository.Write(initial.revision, true) == Result::kConflict);
  initial = repository.Read();
  CHECK(repository.Write(initial.revision, true) == Result::kOk);
  auto current = repository.Read();
  CHECK(current.present && current.raw == L"1" && current.auto_focus);
  CHECK(repository.Write(initial.revision, false) == Result::kConflict);
  wchar_t value[64];
  GetPrivateProfileStringW(L"Unknown", L"Keep", L"", value, 64, path.c_str());
  CHECK(std::wstring(value) == L"unchanged");
  GetPrivateProfileStringW(L"Unknown", L"Unicode", L"", value, 64,
                           path.c_str());
  CHECK(std::wstring(value) == L"設定");
  for (const auto [text, expected] : {std::pair{L"256", false},
                                      {L"257", true},
                                      {L"-256", false},
                                      {L"0x1", false},
                                      {L"2", true}}) {
    CHECK(WritePrivateProfileStringW(L"General", L"AutoFocus", text,
                                     path.c_str()));
    CHECK(repository.Read().auto_focus == expected);
  }
  CHECK(
      WritePrivateProfileStringW(L"General", L"AutoFocus", L"", path.c_str()));
  current = repository.Read();
  CHECK(current.present && current.raw.empty() && !current.auto_focus);
  CHECK(WritePrivateProfileStringW(L"General", L"AutoFocus", L"custom",
                                   path.c_str()));
  current = repository.Read();
  CHECK(current.present && current.raw == L"custom");
  CHECK(WritePrivateProfileStringW(L"General", L"AutoFocus", nullptr,
                                   path.c_str()));
  current = repository.Read();
  CHECK(!current.present && !current.auto_focus);
  // Deny writes without relying on administrator/read-only attribute behavior.
  HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(locked != INVALID_HANDLE_VALUE);
  CHECK(repository.Write(current.revision, true) == Result::kWriteFailed);
  CloseHandle(locked);
  struct FakeRuntime final : Runtime {
    bool enabled = true;
    bool stop_ok = true, start_ok = true;
    int stops = 0, starts = 0;
    bool Enabled() const override { return enabled; }
    bool Stop() override {
      ++stops;
      if (stop_ok) enabled = false;
      return stop_ok;
    }
    bool Start() override {
      ++starts;
      enabled = start_ok;
      return start_ok;
    }
  } runtime;
  current = repository.Read();
  CHECK(ApplyAutoFocus(repository, current.revision, true, runtime) ==
        Result::kOk);
  CHECK(runtime.stops == 1 && runtime.starts == 1 && runtime.enabled);
  CHECK(ApplyAutoFocus(repository, current.revision, false, runtime) ==
        Result::kConflict);
  CHECK(runtime.stops == 1 && runtime.starts == 1);
  runtime.enabled = false;
  CHECK(ApplyAutoFocus(repository, repository.Read().revision, false,
                       runtime) == Result::kOk);
  CHECK(runtime.stops == 1 && runtime.starts == 1 && !runtime.enabled);
  runtime.enabled = true;
  runtime.stop_ok = false;
  CHECK(ApplyAutoFocus(repository, repository.Read().revision, true, runtime) ==
        Result::kActivationFailed);
  CHECK(runtime.stops == 2 && runtime.starts == 1);
  CHECK(repository.Read().auto_focus);
  runtime.stop_ok = true;
  runtime.start_ok = false;
  CHECK(ApplyAutoFocus(repository, repository.Read().revision, false,
                       runtime) == Result::kActivationFailed);
  CHECK(runtime.stops == 3 && runtime.starts == 2 && !runtime.enabled);
  CHECK(!repository.Read().auto_focus);
  const Snapshot legacy_draft = repository.Read();
  CHECK(WritePrivateProfileStringW(L"Unknown", L"Keep", L"other legacy page",
                                   path.c_str()));
  CHECK(repository.WriteIfAutoFocusUnchanged(legacy_draft, true) ==
        Result::kOk);
  CHECK(repository.WriteIfAutoFocusUnchanged(legacy_draft, false) ==
        Result::kConflict);
  CHECK(DeleteFileW(path.c_str()));
  CHECK(DeleteFileW(filename));
  CHECK(repository.Read().result == Result::kReadFailed);
  std::puts("Settings protocol and repository tests passed");
}
