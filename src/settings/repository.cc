#include "settings/repository.h"

#include <map>

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
  std::map<std::wstring, std::vector<wchar_t>> sections;
  for (size_t i = 0; i < kOptionCount; ++i) {
    auto [entry, inserted] = sections.try_emplace(kOptions[i].section);
    auto& section = entry->second;
    if (inserted) {
      section.resize(65536, L'\0');
      const DWORD length = GetPrivateProfileSectionW(
          kOptions[i].section, section.data(),
          static_cast<DWORD>(section.size()), path_.c_str());
      if (length >= section.size() - 2) {
        snapshot.result = Result::kReadFailed;
        return snapshot;
      }
    }
    Value& value = snapshot.values[i];
    const auto& definition = kOptions[i];
    const std::wstring prefix = std::wstring(definition.key) + L"=";
    for (const wchar_t* line = section.data(); *line;
         line += std::wcslen(line) + 1) {
      if (!_wcsnicmp(line, prefix.c_str(), prefix.size())) {
        value.present = true;
        value.raw = line + prefix.size();
        break;
      }
    }
    if (value.raw.size() > 4095) {
      snapshot.result = Result::kReadFailed;
      return snapshot;
    }
    const uint32_t parsed =
        value.present ? static_cast<uint32_t>(ParseInt(value.raw.c_str()))
                      : definition.engine_default;
    const uint32_t number = definition.storage == Storage::kDword ? parsed
                            : definition.storage == Storage::kWord
                                ? static_cast<uint16_t>(parsed)
                                : static_cast<uint8_t>(parsed);
    value.effective =
        definition.mask
            ? (definition.mask == 3 ? (number & 1 ? 1u : number & 2)
                                    : uint32_t((number & definition.mask) != 0))
            : (definition.kind == ValueKind::kToggle ? uint32_t(number != 0)
                                                     : number);
    if (definition.storage == Storage::kSignedByte)
      value.effective = std::bit_cast<uint32_t>(
          static_cast<int32_t>(static_cast<int8_t>(number)));
  }
  for (size_t i = 0; i < kTextCount; ++i) {
    auto& value = snapshot.values.text[i];
    const std::wstring prefix = std::wstring(TextKey(i)) + L"=";
    for (const wchar_t* line = sections.at(L"Input").data(); *line;
         line += std::wcslen(line) + 1) {
      if (!_wcsnicmp(line, prefix.c_str(), prefix.size())) {
        value.present = true;
        value.raw = line + prefix.size();
        break;
      }
    }
    if (value.raw.size() > 4095) {
      snapshot.result = Result::kReadFailed;
      return snapshot;
    }
    value.effective = value.present ? value.raw : TextDefault(i);
  }
  if (Revision() != revision) snapshot.result = Result::kConflict;
  return snapshot;
}
Result Repository::Write(uint64_t revision, const Patch& patch) const {
  if (!patch.Valid()) return Result::kInvalid;
  // Cooperating modern writers serialize the batch. The frontend lease excludes
  // the legacy dialog; multi-host editing remains conservatively refused.
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
  // Coalesce edits to the same bit field into one checked key write.
  for (size_t i = 0; i < kOptionCount; ++i) {
    if (!(patch.changed & (1u << i))) continue;
    const auto& definition = kOptions[i];
    bool already_written = false;
    for (size_t j = 0; j < i; ++j)
      if ((patch.changed & (1u << j)) &&
          std::wcscmp(kOptions[j].key, definition.key) == 0 &&
          std::wcscmp(kOptions[j].section, definition.section) == 0)
        already_written = true;
    if (already_written) continue;
    // Preserve unedited bits, including bits above the engine's byte.
    uint32_t number =
        before.values[i].present
            ? static_cast<uint32_t>(ParseInt(before.values[i].raw.c_str()))
            : definition.engine_default;
    for (size_t j = i; j < kOptionCount; ++j) {
      if (!(patch.changed & (1u << j)) ||
          std::wcscmp(kOptions[j].key, definition.key) != 0 ||
          std::wcscmp(kOptions[j].section, definition.section) != 0)
        continue;
      const uint32_t mask = kOptions[j].mask;
      const uint32_t value = patch.values[j];
      number = mask
                   ? (number & ~mask) | (mask == 3 ? value : (value ? mask : 0))
                   : value;
    }
    const std::wstring text = definition.kind == ValueKind::kSignedNumber
                                  ? std::to_wstring(SignedNumber(number))
                                  : std::to_wstring(number);
    if (!WritePrivateProfileStringW(definition.section, definition.key,
                                    text.c_str(), path_.c_str()))
      return Result::kWriteFailed;
    const Snapshot after = Read();
    if (after.result != Result::kOk || !after.values[i].present ||
        after.values[i].raw != text)
      return Result::kWriteFailed;
  }
  for (size_t i = 0; i < kTextCount; ++i) {
    if (!(patch.text_changed & (uint64_t{1} << i))) continue;
    if (!WritePrivateProfileStringW(L"Input", TextKey(i), patch.text[i].c_str(),
                                    path_.c_str()))
      return Result::kWriteFailed;
    const Snapshot after = Read();
    if (after.result != Result::kOk || !after.values.text[i].present ||
        after.values.text[i].raw != patch.text[i])
      return Result::kWriteFailed;
  }
  return Result::kOk;
}
Result Repository::WriteIfUnchanged(const Snapshot& base,
                                    const Patch& patch) const {
  if (!patch.Valid()) return Result::kInvalid;
  if (base.result != Result::kOk) return base.result;
  const Snapshot current = Read();
  if (current.result != Result::kOk) return current.result;
  for (size_t i = 0; i < kOptionCount; ++i) {
    if (!(patch.changed & (1u << i))) continue;
    if (current.values[i].present != base.values[i].present ||
        current.values[i].raw != base.values[i].raw)
      return Result::kConflict;
  }
  for (size_t i = 0; i < kTextCount; ++i) {
    if (!(patch.text_changed & (uint64_t{1} << i))) continue;
    if (current.values.text[i].present != base.values.text[i].present ||
        current.values.text[i].raw != base.values.text[i].raw)
      return Result::kConflict;
  }
  return Write(current.revision, patch);
}
Result Repository::WriteIfAutoFocusUnchanged(const Snapshot& base,
                                             bool value) const {
  return WriteIfUnchanged(base, SingleChange(Option::kAutoFocus, value));
}
}  // namespace altsnap::settings
