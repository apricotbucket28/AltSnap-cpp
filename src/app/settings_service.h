#ifndef ALTSNAP_APP_SETTINGS_SERVICE_H_
#define ALTSNAP_APP_SETTINGS_SERVICE_H_
#include <windows.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

#include "settings/repository.h"
namespace altsnap::app {
inline constexpr UINT kLaunchSettingsMessage = WM_APP + 72;
// nullopt means more than one matching installation instance; nullptr means
// none.
std::optional<HWND> FindModernSettingsHost();
// Host lifetime owns this object; Dispatch and repository access are STA-only.
// The transport thread owns its pipe and exchanges owning values under mutex_.
class SettingsService {
 public:
  SettingsService(HWND window, std::wstring path);
  ~SettingsService();
  SettingsService(const SettingsService&) = delete;
  SettingsService& operator=(const SettingsService&) = delete;
  void Dispatch(bool stopping);
  bool FrontendActive();
  bool FocusFrontend();
  settings::Snapshot ReadLegacyAutoFocus() const;
  bool WriteLegacyAutoFocus(const settings::Snapshot& base, bool value);

 private:
  void Serve();
  settings::Snapshot Execute(const settings::Request& request, DWORD client,
                             bool stopping);
  HWND window_;
  settings::Repository repository_;
  HANDLE frontend_ = nullptr;
  DWORD frontend_pid_ = 0;
  std::atomic<bool> stop_ = false;
  HANDLE stop_event_ = nullptr;
  HANDLE reply_event_ = nullptr;
  struct Pending {
    settings::Request request;
    DWORD client;
  };
  std::mutex mutex_;
  std::optional<Pending> pending_;
  std::optional<settings::Snapshot> reply_;
  std::thread thread_;
};
// Non-owning access on host thread, installed only for the host lifetime.
extern SettingsService* settings_service;
bool LaunchModernSettings();
}  // namespace altsnap::app
#endif
