#include <windows.h>

#include <shellapi.h>
#include <shobjidl.h>
#undef GetCurrentTime
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>

#include "settings/protocol.h"
namespace altsnap::settings_ui {
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using settings::Operation;
using settings::Result;
struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  ~Handle() {
    if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value);
  }
};
std::wstring ExecutablePath(DWORD pid = 0) {
  std::wstring path(32768, L'\0');
  DWORD length = static_cast<DWORD>(path.size());
  if (!pid)
    length = GetModuleFileNameW(nullptr, path.data(), length);
  else {
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process.value ||
        !QueryFullProcessImageNameW(process.value, 0, path.data(), &length))
      length = 0;
  }
  path.resize(length);
  return path;
}
std::wstring HostPath() {
  return (std::filesystem::path(ExecutablePath()).parent_path().parent_path() /
          L"AltSnap.exe")
      .wstring();
}
DWORD DiscoverHost() {
  DWORD target = 0;
  for (HWND window = FindWindowW(L"AltSnap", nullptr); window;
       window = FindWindowExW(nullptr, window, L"AltSnap", nullptr)) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    // A medium client may not query an elevated process on every system. Use
    // --host from the tray for that case; the pipe validates the association.
    if (!_wcsicmp(ExecutablePath(pid).c_str(), HostPath().c_str())) {
      if (target) return 0;
      target = pid;
    }
  }
  return target;
}
std::optional<settings::Snapshot> Exchange(DWORD host,
                                           const settings::Request& request,
                                           HANDLE cancelled) {
  const std::wstring pipe_name = settings::PipeName(host);
  Handle pipe{CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                          nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                          nullptr)};
  if (pipe.value == INVALID_HANDLE_VALUE) return {};
  ULONG server = 0;
  if (!GetNamedPipeServerProcessId(pipe.value, &server) || server != host)
    return {};
  DWORD mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(pipe.value, &mode, nullptr, nullptr)) return {};
  Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  if (!event.value) return {};
  auto transfer = [&](bool read, void* data, DWORD length, DWORD& count) {
    ResetEvent(event.value);
    OVERLAPPED overlapped{};
    overlapped.hEvent = event.value;
    BOOL ok = read ? ReadFile(pipe.value, data, length, &count, &overlapped)
                   : WriteFile(pipe.value, data, length, &count, &overlapped);
    if (!ok && GetLastError() != ERROR_IO_PENDING) return false;
    const HANDLE events[] = {event.value, cancelled};
    if (!ok &&
        WaitForMultipleObjects(2, events, FALSE, 10000) != WAIT_OBJECT_0) {
      CancelIoEx(pipe.value, &overlapped);
      GetOverlappedResult(pipe.value, &overlapped, &count, TRUE);
      return false;
    }
    return GetOverlappedResult(pipe.value, &overlapped, &count, FALSE) != 0;
  };
  auto bytes = settings::Encode(request);
  DWORD count = 0;
  if (!transfer(false, bytes.data(), static_cast<DWORD>(bytes.size()), count) ||
      count != bytes.size())
    return {};
  bytes.resize(settings::kMaxReply);
  if (!transfer(true, bytes.data(), static_cast<DWORD>(bytes.size()), count))
    return {};
  settings::Snapshot snapshot;
  if (!settings::Decode(std::span(bytes.data(), count), request.id, snapshot))
    return {};
  uint8_t acknowledgement = 1;
  transfer(false, &acknowledgement, 1, count);
  return snapshot;
}
class Shell : public std::enable_shared_from_this<Shell> {
 public:
  explicit Shell(DWORD host)
      : host_(host),
        cancellation_(std::make_shared<Handle>(
            CreateEventW(nullptr, TRUE, FALSE, nullptr))) {
    if (!cancellation_->value) throw_last_error();
  }
  void Open() {
    window_ = Window();
    window_.Title(L"AltSnap Preferences");
    std::ifstream file(
        std::filesystem::path(ExecutablePath()).parent_path() / L"Shell.xaml",
        std::ios::binary);
    const std::string xaml((std::istreambuf_iterator<char>(file)), {});
    root_ = Markup::XamlReader::Load(to_hstring(xaml)).as<FrameworkElement>();
    window_.Content(root_);
    window_.SystemBackdrop(Media::MicaBackdrop());
    HWND hwnd = nullptr;
    check_hresult(window_.as<IWindowNative>()->get_WindowHandle(&hwnd));
    SetPropW(hwnd, L"AltSnap.Settings.Host",
             reinterpret_cast<HANDLE>(static_cast<uintptr_t>(host_)));
    SetPropW(hwnd, L"AltSnap.Settings.Window", reinterpret_cast<HANDLE>(1));
    window_.AppWindow().Resize({1040, 780});
    window_.AppWindow().SetIcon(hstring(
        (std::filesystem::path(ExecutablePath()).parent_path() / L"AltSnap.ico")
            .wstring()));
    const auto presenter =
        window_.AppWindow()
            .Presenter()
            .as<Microsoft::UI::Windowing::OverlappedPresenter>();
    presenter.PreferredMinimumWidth(600);
    presenter.PreferredMinimumHeight(520);
    const auto weak = weak_from_this();
    window_.Closed([weak, hwnd](auto&&, auto&&) {
      RemovePropW(hwnd, L"AltSnap.Settings.Host");
      RemovePropW(hwnd, L"AltSnap.Settings.Window");
      if (auto self = weak.lock()) {
        self->closed_ = true;
        SetEvent(self->cancellation_->value);
      }
    });
    auto nav = Find<NavigationView>(L"Navigation");
    nav.SelectionChanged(
        [weak](auto&&, NavigationViewSelectionChangedEventArgs const& args) {
          if (auto self = weak.lock()) {
            const hstring tag =
                args.SelectedItemContainer().Tag().as<hstring>();
            for (const wchar_t* page : {L"General", L"Appearance", L"About"})
              self->Find<FrameworkElement>(
                      (std::wstring(page) + L"Page").c_str())
                  .Visibility(tag == page ? Visibility::Visible
                                          : Visibility::Collapsed);
          }
        });
    nav.SelectedItem(nav.MenuItems().GetAt(0));
    Find<ComboBox>(L"Theme").SelectionChanged([weak](auto&& sender, auto&&) {
      if (auto self = weak.lock()) {
        const int index = sender.template as<ComboBox>().SelectedIndex();
        self->root_.RequestedTheme(index == 1   ? ElementTheme::Light
                                   : index == 2 ? ElementTheme::Dark
                                                : ElementTheme::Default);
      }
    });
    root_.ActualThemeChanged([weak](auto&&, auto&&) {
      if (auto self = weak.lock()) self->UpdateTitleTheme();
    });
    UpdateTitleTheme();
    Find<ToggleSwitch>(L"AutoFocus").Toggled([weak](auto&&, auto&&) {
      if (auto self = weak.lock()) self->UpdateApply();
    });
    Find<Button>(L"Apply").Click([weak](auto&&, auto&&) {
      if (auto s = weak.lock()) s->Request(Operation::kApply);
    });
    Find<Button>(L"Okay").Click([weak](auto&&, auto&&) {
      if (auto self = weak.lock()) {
        if (self->Find<ToggleSwitch>(L"AutoFocus").IsOn() !=
            self->snapshot_.auto_focus)
          self->Request(Operation::kApply, true);
        else
          self->window_.Close();
      }
    });
    Find<Button>(L"Reload").Click([weak](auto&&, auto&&) {
      if (auto s = weak.lock()) s->Request(Operation::kRead);
    });
    Find<Button>(L"Legacy").Click([weak](auto&&, auto&&) {
      if (auto s = weak.lock()) s->Request(Operation::kLegacy);
    });
    Find<Button>(L"Cancel").Click([weak](auto&&, auto&&) {
      if (auto s = weak.lock()) s->window_.Close();
    });
    Find<Button>(L"StartHost").Click([weak](auto&&, auto&&) {
      if (auto s = weak.lock()) {
        // Starting the resident host enables hooks; this is an explicit user
        // action.
        const auto result = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", HostPath().c_str(), L"-winui",
                          nullptr, SW_SHOWNORMAL));
        if (result > 32)
          s->window_.Close();
        else
          s->Status(L"Could not start AltSnap",
                    L"Check that AltSnap.exe is beside the Settings folder.",
                    InfoBarSeverity::Error);
      }
    });
    window_.Activate();
    if (host_)
      Request(Operation::kRead);
    else {
      Status(L"AltSnap is not connected",
             L"Start the resident host to enable hooks and edit settings. If "
             L"several hosts are running, launch with AltSnap.exe -winui for "
             L"an explicit target.",
             InfoBarSeverity::Warning);
      Find<Button>(L"StartHost").Visibility(Visibility::Visible);
    }
  }

 private:
  template <class T>
  T Find(const wchar_t* name) {
    return root_.FindName(name).as<T>();
  }
  void Status(const wchar_t* title, const wchar_t* message,
              InfoBarSeverity severity) {
    const auto status = Find<InfoBar>(L"Status");
    status.Title(title);
    status.Message(message);
    status.Severity(severity);
  }
  void UpdateApply() {
    Find<Button>(L"Okay").IsEnabled(connected_ && !busy_);
    Find<Button>(L"Apply").IsEnabled(connected_ && !busy_ &&
                                     Find<ToggleSwitch>(L"AutoFocus").IsOn() !=
                                         snapshot_.auto_focus);
  }
  void UpdateTitleTheme() {
    using Microsoft::UI::Windowing::TitleBarTheme;
    window_.AppWindow().TitleBar().PreferredTheme(
        root_.ActualTheme() == ElementTheme::Dark ? TitleBarTheme::Dark
                                                  : TitleBarTheme::Light);
  }
  fire_and_forget Request(Operation operation, bool close_after = false) {
    if (busy_ || closed_) co_return;
    busy_ = true;
    UpdateApply();
    Find<Button>(L"Reload").IsEnabled(false);
    Find<Button>(L"Legacy").IsEnabled(false);
    Find<ToggleSwitch>(L"AutoFocus").IsEnabled(false);
    Status(operation == Operation::kApply ? L"Saving changes"
                                          : L"Reading settings",
           L"Waiting for the AltSnap host.", InfoBarSeverity::Informational);
    const settings::Request request{++request_id_, operation,
                                    snapshot_.revision,
                                    Find<ToggleSwitch>(L"AutoFocus").IsOn()};
    const auto dispatcher = root_.DispatcherQueue();
    const DWORD host = host_;
    const auto cancellation = cancellation_;
    const auto weak = weak_from_this();
    co_await resume_background();
    std::optional<settings::Snapshot> response;
    try {
      response = Exchange(host, request, cancellation->value);
    } catch (...) {
      response.reset();
    }
    dispatcher.TryEnqueue(
        [weak, response = std::move(response), operation, close_after]() {
          const auto self = weak.lock();
          if (!self || self->closed_) return;
          try {
            self->Complete(response, operation, close_after);
          } catch (const hresult_error& error) {
            self->Status(L"Settings could not be displayed",
                         error.message().c_str(), InfoBarSeverity::Error);
          }
        });
  }
  void Complete(const std::optional<settings::Snapshot>& response,
                Operation operation, bool close_after) {
    busy_ = false;
    connected_ = response && response->result != Result::kBusy &&
                 response->result != Result::kInvalid &&
                 response->result != Result::kReadFailed;
    Find<Button>(L"Reload").IsEnabled(true);
    Find<Button>(L"Legacy").IsEnabled(connected_);
    Find<ToggleSwitch>(L"AutoFocus").IsEnabled(connected_);
    if (!response) {
      connected_ = false;
      Status(L"Connection interrupted",
             L"The outcome may be unknown. Reload to read the persisted value "
             L"before applying again. Your draft is retained.",
             InfoBarSeverity::Error);
    } else if (response->result == Result::kOk) {
      snapshot_ = *response;
      Find<ToggleSwitch>(L"AutoFocus").IsOn(snapshot_.auto_focus);
      Find<TextBlock>(L"ConfigPath").Text(L"Configuration\n" + snapshot_.path);
      Status(operation == Operation::kApply ? L"Changes saved"
                                            : L"Connected to AltSnap",
             snapshot_.enabled ? L"Hooks are enabled. Settings are active."
                               : L"Hooks remain disabled. Saved settings take "
                                 L"effect when AltSnap is enabled.",
             InfoBarSeverity::Success);
      if (operation == Operation::kLegacy || close_after) {
        window_.Close();
        return;
      }
    } else {
      // Preserve both the original revision and draft on conflict/error. Reload
      // is explicit; never blindly repeat an uncertain mutation.
      connected_ = false;
      const wchar_t* message =
          L"The host could not read the configuration. Check file access, then "
          L"Reload.";
      if (response->result == Result::kBusy)
        message =
            L"Close legacy settings and other AltSnap instances, then Reload. "
            L"Only one settings frontend may edit at a time.";
      if (response->result == Result::kConflict)
        message =
            L"The configuration changed since it was read. Your draft is "
            L"retained. Reload before applying again.";
      if (response->result == Result::kWriteFailed)
        message =
            L"The save could not be verified. Your draft is retained. Check "
            L"configuration access, then Reload to inspect the persisted "
            L"value.";
      if (response->result == Result::kActivationFailed)
        message =
            L"The setting was saved, but hook activation failed. Your draft is "
            L"retained. Reload to inspect the state; check the resident host "
            L"before enabling hooks.";
      Status(L"Action needed", message, InfoBarSeverity::Error);
    }
    UpdateApply();
  }
  DWORD host_;
  uint32_t request_id_ = 0;
  bool closed_ = false, busy_ = false, connected_ = false;
  settings::Snapshot snapshot_;
  std::shared_ptr<Handle> cancellation_;
  Window window_{nullptr};
  FrameworkElement root_{nullptr};
};
struct App : ApplicationT<App, Markup::IXamlMetadataProvider> {
  explicit App(DWORD host) : host_(host) {
    UnhandledException([](auto&&, UnhandledExceptionEventArgs const& args) {
      MessageBoxW(nullptr, args.Message().c_str(), L"AltSnap Settings",
                  MB_OK | MB_ICONERROR);
      args.Handled(true);
    });
  }
  Markup::IXamlType GetXamlType(
      Windows::UI::Xaml::Interop::TypeName const& type) {
    return metadata_.GetXamlType(type);
  }
  Markup::IXamlType GetXamlType(hstring const& name) {
    return metadata_.GetXamlType(name);
  }
  com_array<Markup::XmlnsDefinition> GetXmlnsDefinitions() {
    return metadata_.GetXmlnsDefinitions();
  }
  void OnLaunched(LaunchActivatedEventArgs const&) {
    try {
      Resources().MergedDictionaries().Append(XamlControlsResources());
      shell_ = std::make_shared<Shell>(host_);
      shell_->Open();
    } catch (const hresult_error& error) {
      std::ofstream diagnostic(
          std::filesystem::path(ExecutablePath()).parent_path() /
          L"startup-error.txt");
      diagnostic << to_string(error.message()) << "\nHRESULT: " << std::hex
                 << static_cast<uint32_t>(error.code().value) << '\n';
      diagnostic.close();
      MessageBoxW(nullptr, error.message().c_str(),
                  L"AltSnap Settings could not start", MB_OK | MB_ICONERROR);
      Exit();
    }
  }
  DWORD host_;
  XamlTypeInfo::XamlControlsXamlMetaDataProvider metadata_;
  std::shared_ptr<Shell> shell_;
};
}  // namespace altsnap::settings_ui
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  using namespace altsnap::settings_ui;
  try {
    SetCurrentProcessExplicitAppUserModelID(L"AltSnap.Settings");
    OSVERSIONINFOEXW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    version.dwMajorVersion = 10;
    version.dwBuildNumber = 26100;
    ULONGLONG condition =
        VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL);
    condition =
        VerSetConditionMask(condition, VER_BUILDNUMBER, VER_GREATER_EQUAL);
    if (!VerifyVersionInfoW(&version, VER_MAJORVERSION | VER_BUILDNUMBER,
                            condition)) {
      MessageBoxW(nullptr,
                  L"AltSnap Settings requires Windows 11 24H2 (build 26100) or "
                  L"newer. The legacy settings remain available in AltSnap.",
                  L"AltSnap Settings", MB_OK | MB_ICONINFORMATION);
      return 1;
    }
    DWORD host = 0;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc == 3 && std::wstring_view(argv[1]) == L"--host") {
      wchar_t* end = nullptr;
      const unsigned long parsed = wcstoul(argv[2], &end, 10);
      if (end && !*end) host = parsed;
    }
    LocalFree(argv);
    if (!host) host = DiscoverHost();
    const std::wstring mutex_name =
        L"Local\\AltSnap.Settings.UI." + std::to_wstring(host);
    Handle instance{CreateMutexW(nullptr, FALSE, mutex_name.c_str())};
    if (!instance.value) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
      EnumWindows(
          [](HWND window, LPARAM pid) -> BOOL {
            if (GetPropW(window, L"AltSnap.Settings.Window") &&
                reinterpret_cast<uintptr_t>(
                    GetPropW(window, L"AltSnap.Settings.Host")) ==
                    static_cast<uintptr_t>(pid)) {
              ShowWindow(window, SW_RESTORE);
              SetForegroundWindow(window);
              return FALSE;
            }
            return TRUE;
          },
          host);
      return 0;
    }
    init_apartment(apartment_type::single_threaded);
    Application::Start([host](auto&&) { make<App>(host); });
    return 0;
  } catch (const hresult_error& error) {
    MessageBoxW(nullptr, error.message().c_str(), L"AltSnap Settings",
                MB_OK | MB_ICONERROR);
    return 1;
  }
}
