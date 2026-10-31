#include "settings/repository.h"

#include "common/strutil.h"
namespace altsnap::settings {
std::optional<uint64_t> Repository::Revision() const {
  HANDLE file =
      CreateFileW(path_.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return std::nullopt;
  uint64_t hash = 14695981039346656037ull;
  std::array<uint8_t, 4096> buffer;
  DWORD count = 0;
  bool ok = true;
  do {
    ok = ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &count, nullptr) != 0;
    for (DWORD i = 0; i < count && ok; ++i)
      hash = (hash ^ buffer[i]) * 1099511628211ull;
  } while (ok && count);
  CloseHandle(file);
  return ok ? std::optional{hash} : std::nullopt;
}
Snapshot Repository::Read() const {
  Snapshot snapshot;
  snapshot.path = path_;
  // Flush this process's profile cache before comparing disk revisions.
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, path_.c_str());
  const auto revision = Revision();
  if (!revision) {
    snapshot.result = Result::kReadFailed;
    return snapshot;
  }
  snapshot.revision = *revision;
  std::vector<wchar_t> section(65536, L'\0');
  const DWORD length = GetPrivateProfileSectionW(
      L"General", section.data(), static_cast<DWORD>(section.size()),
      path_.c_str());
  if (length >= section.size() - 2) {
    snapshot.result = Result::kReadFailed;
    return snapshot;
  }
  for (const wchar_t* line = section.data(); *line;
       line += std::wcslen(line) + 1) {
    if (!_wcsnicmp(line, L"AutoFocus=", 10)) {
      snapshot.present = true;
      snapshot.raw = line + 10;
      break;
    }
  }
  if (snapshot.raw.size() > 4095) {
    snapshot.result = Result::kReadFailed;
    return snapshot;
  }
  // The engine parses decimal text and narrows this option to UCHAR, rather
  // than treating every nonzero UINT returned by GetPrivateProfileInt as true.
  snapshot.auto_focus =
      static_cast<uint8_t>(ParseInt(snapshot.raw.c_str())) != 0;
  if (Revision() != revision) snapshot.result = Result::kConflict;
  return snapshot;
}
Result Repository::Write(uint64_t revision, bool value) const {
  // Cooperating hosts serialize this key even if one starts during an apply.
  // Other keys remain legacy-owned; the companion rejects multi-host editing.
  const std::wstring lock_path = path_ + L".settings-lock";
  HANDLE lock = CreateFileW(
      lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (lock == INVALID_HANDLE_VALUE) return Result::kBusy;
  struct LockOwner {
    HANDLE handle;
    ~LockOwner() { CloseHandle(handle); }
  } owner{lock};
  const Snapshot before = Read();
  if (before.result != Result::kOk) return before.result;
  if (before.revision != revision) return Result::kConflict;
  if (!WritePrivateProfileStringW(L"General", L"AutoFocus", value ? L"1" : L"0",
                                  path_.c_str()))
    return Result::kWriteFailed;
  const Snapshot after = Read();
  return after.result == Result::kOk && after.present &&
                 after.raw == (value ? L"1" : L"0")
             ? Result::kOk
             : Result::kWriteFailed;
}
Result Repository::WriteIfAutoFocusUnchanged(const Snapshot& base,
                                             bool value) const {
  // Other legacy pages can write unrelated keys after General receives Apply.
  // Compare the original key before rebasing onto the latest file revision.
  if (base.result != Result::kOk) return base.result;
  const Snapshot current = Read();
  if (current.result != Result::kOk) return current.result;
  if (current.present != base.present || current.raw != base.raw)
    return Result::kConflict;
  return Write(current.revision, value);
}
}  // namespace altsnap::settings
