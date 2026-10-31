#ifndef ALTSNAP_SETTINGS_PROTOCOL_H_
#define ALTSNAP_SETTINGS_PROTOCOL_H_
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace altsnap::settings {
inline constexpr unsigned kWake = 0x8000 + 71;
inline constexpr uint32_t kMagic = 0x31535441;
inline constexpr size_t kRequestSize = 32;
inline constexpr size_t kMaxReply = 131072;
enum class Operation : uint32_t { kRead = 1, kApply = 2, kLegacy = 3 };
enum class Result : uint32_t {
  kOk,
  kBusy,
  kConflict,
  kWriteFailed,
  kActivationFailed,
  kInvalid,
  kReadFailed
};
inline void Put(std::vector<uint8_t>& bytes, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
inline uint32_t Get(std::span<const uint8_t> bytes, size_t offset) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i)
    value |= uint32_t(bytes[offset + i]) << (i * 8);
  return value;
}
struct Request {
  uint32_t id = 0;
  Operation operation = Operation::kRead;
  uint64_t revision = 0;
  bool auto_focus = false;
};
inline std::vector<uint8_t> Encode(const Request& request) {
  std::vector<uint8_t> bytes;
  for (uint32_t value :
       {kMagic, 1u, request.id, static_cast<uint32_t>(request.operation),
        static_cast<uint32_t>(request.revision),
        static_cast<uint32_t>(request.revision >> 32),
        uint32_t(request.auto_focus), 0u})
    Put(bytes, value);
  return bytes;
}
inline bool Decode(std::span<const uint8_t> bytes, Request& request) {
  if (bytes.size() != kRequestSize || Get(bytes, 0) != kMagic ||
      Get(bytes, 4) != 1 || Get(bytes, 12) < 1 || Get(bytes, 12) > 3 ||
      Get(bytes, 24) > 1 || Get(bytes, 28))
    return false;
  request = {Get(bytes, 8), static_cast<Operation>(Get(bytes, 12)),
             uint64_t(Get(bytes, 16)) | (uint64_t(Get(bytes, 20)) << 32),
             Get(bytes, 24) != 0};
  return true;
}
struct Snapshot {
  Result result = Result::kOk;
  uint64_t revision = 0;
  bool enabled = false;
  bool auto_focus = false;
  bool present = false;
  std::wstring path;
  std::wstring raw;
};
inline std::vector<uint8_t> Encode(const Snapshot& s, uint32_t id) {
  std::vector<uint8_t> bytes;
  for (uint32_t value :
       {kMagic, 1u, id, static_cast<uint32_t>(s.result),
        static_cast<uint32_t>(s.revision),
        static_cast<uint32_t>(s.revision >> 32), uint32_t(s.enabled),
        uint32_t(s.auto_focus), uint32_t(s.present),
        static_cast<uint32_t>(s.path.size()),
        static_cast<uint32_t>(s.raw.size())})
    Put(bytes, value);
  for (const auto& text : {s.path, s.raw})
    for (wchar_t c : text) {
      bytes.push_back(static_cast<uint8_t>(c));
      bytes.push_back(static_cast<uint8_t>(c >> 8));
    }
  return bytes;
}
inline bool Decode(std::span<const uint8_t> bytes, uint32_t id, Snapshot& s) {
  if (bytes.size() < 44 || bytes.size() > kMaxReply ||
      Get(bytes, 0) != kMagic || Get(bytes, 4) != 1 || Get(bytes, 8) != id ||
      Get(bytes, 12) > 6 || Get(bytes, 24) > 1 || Get(bytes, 28) > 1 ||
      Get(bytes, 32) > 1)
    return false;
  const uint64_t path_length = Get(bytes, 36), raw_length = Get(bytes, 40);
  if (44 + 2 * (path_length + raw_length) != bytes.size()) return false;
  s = {static_cast<Result>(Get(bytes, 12)),
       uint64_t(Get(bytes, 16)) | (uint64_t(Get(bytes, 20)) << 32),
       Get(bytes, 24) != 0,
       Get(bytes, 28) != 0,
       Get(bytes, 32) != 0,
       {},
       {}};
  size_t offset = 44;
  for (auto [target, length] :
       {std::pair{&s.path, path_length}, std::pair{&s.raw, raw_length}}) {
    for (uint64_t i = 0; i < length; ++i, offset += 2)
      target->push_back(static_cast<wchar_t>(
          bytes[offset] | (uint16_t(bytes[offset + 1]) << 8)));
  }
  return true;
}
inline std::wstring PipeName(uint32_t pid) {
  return L"\\\\.\\pipe\\AltSnap.Settings.v1." + std::to_wstring(pid);
}
}  // namespace altsnap::settings
#endif
