#ifndef ALTSNAP_SETTINGS_PROTOCOL_H_
#define ALTSNAP_SETTINGS_PROTOCOL_H_
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "settings/model.h"
namespace altsnap::settings {
inline constexpr unsigned kWake = 0x8000 + 71;
inline constexpr uint32_t kMagic = 0x31535441;
inline constexpr size_t kMaxRequest =
    40 + 4 * kOptionCount + kTextCount * (4 + 8190);
inline constexpr size_t kMaxReply = 1048576;
enum class Operation : uint32_t {
  kRead = 1,
  kApply = 2,
  kLegacy = 3,
  kTestWindow = 4
};
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
  Patch patch;
};
inline std::vector<uint8_t> Encode(const Request& request) {
  std::vector<uint8_t> bytes;
  for (uint32_t value :
       {kMagic, 4u, request.id, static_cast<uint32_t>(request.operation),
        static_cast<uint32_t>(request.revision),
        static_cast<uint32_t>(request.revision >> 32), request.patch.changed,
        0u})
    Put(bytes, value);
  for (uint32_t value : request.patch.values) Put(bytes, value);
  Put(bytes, static_cast<uint32_t>(request.patch.text_changed));
  Put(bytes, static_cast<uint32_t>(request.patch.text_changed >> 32));
  for (const auto& value : request.patch.text) {
    Put(bytes, static_cast<uint32_t>(value.size()));
    for (wchar_t c : value) {
      bytes.push_back(static_cast<uint8_t>(c));
      bytes.push_back(static_cast<uint8_t>(c >> 8));
    }
  }
  return bytes;
}
inline bool Decode(std::span<const uint8_t> bytes, Request& request) {
  if (bytes.size() < 40 + 4 * kOptionCount || bytes.size() > kMaxRequest ||
      Get(bytes, 0) != kMagic || Get(bytes, 4) != 4 || Get(bytes, 12) < 1 ||
      Get(bytes, 12) > 4 || Get(bytes, 28))
    return false;
  request = {Get(bytes, 8),
             static_cast<Operation>(Get(bytes, 12)),
             uint64_t(Get(bytes, 16)) | (uint64_t(Get(bytes, 20)) << 32),
             {}};
  request.patch.changed = Get(bytes, 24);
  for (size_t i = 0; i < kOptionCount; ++i)
    request.patch.values[i] = Get(bytes, 32 + 4 * i);
  size_t offset = 32 + 4 * kOptionCount;
  request.patch.text_changed =
      uint64_t(Get(bytes, offset)) | (uint64_t(Get(bytes, offset + 4)) << 32);
  offset += 8;
  for (auto& value : request.patch.text) {
    if (bytes.size() - offset < 4) return false;
    const uint32_t length = Get(bytes, offset);
    offset += 4;
    if (length > 4095 || length > (bytes.size() - offset) / 2) return false;
    for (uint32_t i = 0; i < length; ++i, offset += 2)
      value.push_back(static_cast<wchar_t>(bytes[offset] |
                                           (uint16_t(bytes[offset + 1]) << 8)));
  }
  if (offset != bytes.size()) return false;
  if (!request.patch.Valid() ||
      (request.operation != Operation::kApply && request.patch.Any()))
    return false;
  return true;
}
struct Snapshot {
  Result result = Result::kOk;
  uint64_t revision = 0;
  bool enabled = false;
  Values values;
  std::wstring path;
};
inline std::vector<uint8_t> Encode(const Snapshot& s, uint32_t id) {
  std::vector<uint8_t> bytes;
  for (uint32_t value :
       {kMagic, 4u, id, static_cast<uint32_t>(s.result),
        static_cast<uint32_t>(s.revision),
        static_cast<uint32_t>(s.revision >> 32), uint32_t(s.enabled),
        static_cast<uint32_t>(s.path.size())})
    Put(bytes, value);
  auto text = [&](const std::wstring& value) {
    for (wchar_t c : value) {
      bytes.push_back(static_cast<uint8_t>(c));
      bytes.push_back(static_cast<uint8_t>(c >> 8));
    }
  };
  text(s.path);
  for (const Value& value : s.values) {
    Put(bytes, value.effective);
    Put(bytes, uint32_t(value.present));
    Put(bytes, static_cast<uint32_t>(value.raw.size()));
    text(value.raw);
  }
  for (const auto& value : s.values.text) {
    Put(bytes, uint32_t(value.present));
    Put(bytes, static_cast<uint32_t>(value.raw.size()));
    text(value.raw);
    Put(bytes, static_cast<uint32_t>(value.effective.size()));
    text(value.effective);
  }
  return bytes;
}
inline bool Decode(std::span<const uint8_t> bytes, uint32_t id, Snapshot& s) {
  if (bytes.size() < 32 || bytes.size() > kMaxReply ||
      Get(bytes, 0) != kMagic || Get(bytes, 4) != 4 || Get(bytes, 8) != id ||
      Get(bytes, 12) > 6 || Get(bytes, 24) > 1)
    return false;
  Snapshot decoded;
  decoded.result = static_cast<Result>(Get(bytes, 12));
  decoded.revision =
      uint64_t(Get(bytes, 16)) | (uint64_t(Get(bytes, 20)) << 32);
  decoded.enabled = Get(bytes, 24) != 0;
  size_t offset = 32;
  auto text = [&](uint32_t length, std::wstring& target) {
    if (length > (bytes.size() - offset) / 2) return false;
    for (uint32_t i = 0; i < length; ++i, offset += 2)
      target.push_back(static_cast<wchar_t>(
          bytes[offset] | (uint16_t(bytes[offset + 1]) << 8)));
    return true;
  };
  if (!text(Get(bytes, 28), decoded.path)) return false;
  for (Value& value : decoded.values) {
    if (bytes.size() - offset < 12 || Get(bytes, offset + 4) > 1) return false;
    value.effective = Get(bytes, offset);
    value.present = Get(bytes, offset + 4) != 0;
    const uint32_t length = Get(bytes, offset + 8);
    offset += 12;
    if (length > 4095 || !text(length, value.raw)) return false;
  }
  for (auto& value : decoded.values.text) {
    if (bytes.size() - offset < 8 || Get(bytes, offset) > 1) return false;
    value.present = Get(bytes, offset) != 0;
    uint32_t length = Get(bytes, offset + 4);
    offset += 8;
    if (length > 4095 || !text(length, value.raw) || bytes.size() - offset < 4)
      return false;
    length = Get(bytes, offset);
    offset += 4;
    if (length > 4095 || !text(length, value.effective)) return false;
  }
  if (offset != bytes.size()) return false;
  s = std::move(decoded);
  return true;
}
inline std::wstring PipeName(uint32_t pid) {
  return L"\\\\.\\pipe\\AltSnap.Settings.v4." + std::to_wstring(pid);
}
}  // namespace altsnap::settings
#endif
