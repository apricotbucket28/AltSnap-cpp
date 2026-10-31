#ifndef ALTSNAP_APP_HOOK_LIFECYCLE_H_
#define ALTSNAP_APP_HOOK_LIFECYCLE_H_

#include <windows.h>

#include <deque>
#include <variant>

namespace altsnap::app {

// Effects stay in the app adapter. The controller and all callbacks belong to
// the attached window's thread; rejected stops retain the request for retry.
class HookLifecycleBackend {
 public:
  virtual ~HookLifecycleBackend() = default;
  virtual bool Enabled() const = 0;
  virtual bool Stopping() const = 0;
  virtual bool TryStop() = 0;
  virtual void Start() = 0;
  virtual void SaveZones(int layout) = 0;
};

class HookLifecycle final {
 public:
  enum class Change { kToggle, kReload, kSaveZones };
  static constexpr UINT_PTR kRetryTimer = 0xA17E;
  static constexpr UINT kRetryMessage = WM_APP + 0x40;
  explicit HookLifecycle(HookLifecycleBackend& backend) : backend_(backend) {}
  ~HookLifecycle();
  HookLifecycle(const HookLifecycle&) = delete;
  HookLifecycle& operator=(const HookLifecycle&) = delete;

  void Attach(HWND owner);
  void Detach();
  void Request(Change change, int layout = 0);
  void SessionStopped();
  bool DeferMessage(UINT message, WPARAM wp, LPARAM lp);
  bool HandleMessage(UINT message, WPARAM wp);
  bool pending() const { return !requests_.empty() || !deferred_.empty(); }

 private:
  struct RequestValue {
    Change change;
    int layout;
  };
  struct Tray {
    WPARAM icon;
    LPARAM event;
  };
  struct OpenConfig {
    WPARAM page;
    LPARAM flags;
  };
  struct Reload {};
  struct Command {
    WPARAM command;
    HWND control;
  };
  struct ActionMenu {
    HWND window;
    LPARAM flags;
  };
  struct UniKeyMenu {
    HWND window;
    LPARAM key;
  };
  using Deferred =
      std::variant<Tray, OpenConfig, Reload, Command, ActionMenu, UniKeyMenu>;
  void CheckThread() const;
  void ArmRetry();
  void Service();
  bool Replay(const Deferred& message);

  HookLifecycleBackend& backend_;
  HWND owner_ = nullptr;
  DWORD owner_thread_ = 0;
  bool servicing_ = false;
  std::deque<RequestValue> requests_;
  std::deque<Deferred> deferred_;
};

}  // namespace altsnap::app
#endif  // ALTSNAP_APP_HOOK_LIFECYCLE_H_
