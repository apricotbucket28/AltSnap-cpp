#ifndef ALTSNAP_SETTINGS_APPLY_H_
#define ALTSNAP_SETTINGS_APPLY_H_
#include "settings/repository.h"
namespace altsnap::settings {
class Runtime {
 public:
  virtual ~Runtime() = default;
  virtual bool Enabled() const = 0;
  virtual bool Stop() = 0;
  virtual bool Start() = 0;
};
// Called only on the runtime owner's thread. Never start after a failed stop,
// never enable a disabled host, and never reload after a failed persistence.
inline Result ApplyAutoFocus(Repository& repository, uint64_t revision,
                             bool value, Runtime& runtime) {
  const bool was_enabled = runtime.Enabled();
  const Result persisted = repository.Write(revision, value);
  if (persisted != Result::kOk) return persisted;
  if (was_enabled && (!runtime.Stop() || !runtime.Start()))
    return Result::kActivationFailed;
  return Result::kOk;
}
}  // namespace altsnap::settings
#endif
