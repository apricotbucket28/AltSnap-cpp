#ifndef ALTSNAP_SETTINGS_REPOSITORY_H_
#define ALTSNAP_SETTINGS_REPOSITORY_H_
#include <windows.h>

#include <optional>

#include "settings/protocol.h"
namespace altsnap::settings {
// All methods run on the host window thread. The caller serializes writers.
class Repository {
 public:
  explicit Repository(std::wstring path) : path_(std::move(path)) {}
  Snapshot Read() const;
  Result Write(uint64_t revision, bool value) const;
  Result WriteIfAutoFocusUnchanged(const Snapshot& base, bool value) const;

 private:
  std::optional<uint64_t> Revision() const;
  std::wstring path_;
};
}  // namespace altsnap::settings
#endif
