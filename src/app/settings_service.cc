#include "app/settings_service.h"

#include <exdisp.h>
#include <sddl.h>
#include <shldisp.h>
#include <shlobj.h>
#include <wrl/client.h>

#include "app/altsnap.h"
#include "common/winutil.h"
#include "settings/apply.h"
#include "ui/config.h"
#include "ui/config_internal.h"
namespace altsnap::app {
SettingsService* settings_service = nullptr;
namespace {
bool MultipleHosts() {
  unsigned count = 0;
  for (HWND window = FindWindowW(L"AltSnap", nullptr); window;
       window = FindWindowExW(nullptr, window, L"AltSnap", nullptr))
    ++count;
  return count > 1;
}
std::wstring ClientPath(DWORD pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return {};
  std::wstring path(32768, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  const bool ok =
      QueryFullProcessImageNameW(process, 0, path.data(), &size) != 0;
  CloseHandle(process);
  path.resize(ok ? size : 0);
  return path;
}
// Grant only the current logon SID (not all sessions for this account),
// including medium-integrity clients when the host is elevated. Remote clients
// are rejected.
PSECURITY_DESCRIPTOR PipeSecurity() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return nullptr;
  DWORD size = 0;
  GetTokenInformation(token, TokenLogonSid, nullptr, 0, &size);
  std::vector<uint8_t> buffer(size);
  const bool ok = GetTokenInformation(token, TokenLogonSid, buffer.data(), size,
                                      &size) != 0;
  CloseHandle(token);
  if (!ok) return nullptr;
  auto groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
  wchar_t* sid = nullptr;
  if (!groups->GroupCount ||
      !ConvertSidToStringSidW(groups->Groups[0].Sid, &sid))
    return nullptr;
  const std::wstring sddl =
      L"D:P(A;;GA;;;" + std::wstring(sid) + L")S:(ML;;NW;;;ME)";
  LocalFree(sid);
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  ConvertStringSecurityDescriptorToSecurityDescriptorW(
      sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr);
  return descriptor;
}
}  // namespace
std::optional<HWND> FindModernSettingsHost() {
  HWND match = nullptr;
  const std::wstring executable = GetExecutablePath();
  for (HWND window = FindWindowW(L"AltSnap", nullptr); window;
       window = FindWindowExW(nullptr, window, L"AltSnap", nullptr)) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!_wcsicmp(ClientPath(pid).c_str(), executable.c_str())) {
      if (match) return std::nullopt;
      match = window;
    }
  }
  return match;
}
SettingsService::SettingsService(HWND window, std::wstring path)
    : window_(window), repository_(std::move(path)) {
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  reply_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (stop_event_ && reply_event_) thread_ = std::thread([this] { Serve(); });
}
SettingsService::~SettingsService() {
  stop_ = true;
  if (stop_event_) SetEvent(stop_event_);
  if (thread_.joinable()) thread_.join();
  if (stop_event_) CloseHandle(stop_event_);
  if (reply_event_) CloseHandle(reply_event_);
  if (frontend_) CloseHandle(frontend_);
}
bool SettingsService::FrontendActive() {
  if (frontend_ && WaitForSingleObject(frontend_, 0) != WAIT_TIMEOUT) {
    CloseHandle(frontend_);
    frontend_ = nullptr;
    frontend_pid_ = 0;
  }
  return frontend_ != nullptr;
}
bool SettingsService::FocusFrontend() {
  if (!FrontendActive()) return false;
  for (HWND window = GetTopWindow(nullptr); window;
       window = GetWindow(window, GW_HWNDNEXT)) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == frontend_pid_ && IsWindowVisible(window)) {
      ShowWindow(window, SW_RESTORE);
      SetForegroundWindow(window);
      return true;
    }
  }
  return false;
}
settings::Snapshot SettingsService::ReadLegacyAutoFocus() const {
  return repository_.Read();
}
bool SettingsService::WriteLegacyAutoFocus(const settings::Snapshot& base,
                                           bool value) {
  if (FrontendActive() || MultipleHosts()) return false;
  return repository_.WriteIfAutoFocusUnchanged(base, value) ==
         settings::Result::kOk;
}
settings::Snapshot SettingsService::Execute(const settings::Request& request,
                                            DWORD client, bool stopping) {
  using settings::Result;
  auto snapshot = repository_.Read();
  snapshot.enabled = IsEnabled();
  if (stopping || MultipleHosts() || IsWindow(g_cfgwnd) ||
      (FrontendActive() && frontend_pid_ != client)) {
    snapshot.result = Result::kBusy;
    return snapshot;
  }
  if (!frontend_) {
    frontend_ = OpenProcess(SYNCHRONIZE, FALSE, client);
    if (!frontend_) {
      snapshot.result = Result::kBusy;
      return snapshot;
    }
    frontend_pid_ = client;
  }
  if (request.operation == settings::Operation::kLegacy) {
    // Release the frontend lease before opening the modal property sheet.
    CloseHandle(frontend_);
    frontend_ = nullptr;
    frontend_pid_ = 0;
    PostMessageW(window_, WM_OPENCONFIG, 0, 1);
    return snapshot;
  }
  if (request.operation == settings::Operation::kApply &&
      snapshot.result == Result::kOk) {
    class HostRuntime final : public settings::Runtime {
     public:
      bool Enabled() const override { return IsEnabled(); }
      bool Stop() override { return UnhookSystem() == 0; }
      bool Start() override { return HookSystem() == 0; }
    } runtime;
    const Result result = settings::ApplyAutoFocus(
        repository_, request.revision, request.auto_focus, runtime);
    snapshot = repository_.Read();
    snapshot.result = result;
    snapshot.enabled = IsEnabled();
  }
  return snapshot;
}
void SettingsService::Dispatch(bool stopping) {
  std::optional<Pending> command;
  {
    std::lock_guard lock(mutex_);
    command.swap(pending_);
  }
  if (!command) return;
  auto result = Execute(command->request, command->client, stopping);
  {
    std::lock_guard lock(mutex_);
    reply_ = std::move(result);
  }
  SetEvent(reply_event_);
}
void SettingsService::Serve() {
  PSECURITY_DESCRIPTOR descriptor = PipeSecurity();
  if (!descriptor) return;
  SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
  const std::wstring name = settings::PipeName(GetCurrentProcessId());
  HANDLE pipe = CreateNamedPipeW(
      name.c_str(),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS, 1,
      static_cast<DWORD>(settings::kMaxReply), 1024, 0, &security);
  LocalFree(descriptor);
  if (pipe == INVALID_HANDLE_VALUE) return;
  HANDLE io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!io_event) {
    CloseHandle(pipe);
    return;
  }
  // Wait on kernel events rather than waking the resident host on a timer.
  auto completed = [&](BOOL immediate, OVERLAPPED& operation, DWORD timeout,
                       DWORD& count) {
    if (!immediate) {
      if (GetLastError() != ERROR_IO_PENDING) return false;
      const HANDLE events[] = {stop_event_, io_event};
      if (WaitForMultipleObjects(2, events, FALSE, timeout) !=
          WAIT_OBJECT_0 + 1) {
        CancelIoEx(pipe, &operation);
        GetOverlappedResult(pipe, &operation, &count, TRUE);
        return false;
      }
    }
    return GetOverlappedResult(pipe, &operation, &count, FALSE) != 0;
  };
  const std::wstring expected =
      GetExecutableDirectory() + L"\\Settings\\AltSnap.Settings.exe";
  while (!stop_) {
    ResetEvent(io_event);
    OVERLAPPED connection{};
    connection.hEvent = io_event;
    const BOOL connected = ConnectNamedPipe(pipe, &connection);
    const DWORD connection_error = connected ? ERROR_SUCCESS : GetLastError();
    DWORD count = 0;
    if (!connected && connection_error != ERROR_PIPE_CONNECTED &&
        !completed(connected, connection, INFINITE, count)) {
      DisconnectNamedPipe(pipe);
      continue;
    }
    ULONG client = 0;
    if (!GetNamedPipeClientProcessId(pipe, &client)) {
      DisconnectNamedPipe(pipe);
      continue;
    }
    DWORD client_session = 0, host_session = 0;
    const bool trusted =
        ProcessIdToSessionId(client, &client_session) &&
        ProcessIdToSessionId(GetCurrentProcessId(), &host_session) &&
        client_session == host_session &&
        !_wcsicmp(ClientPath(client).c_str(), expected.c_str());
    std::array<uint8_t, settings::kRequestSize + 1> bytes{};
    bool received = false;
    if (trusted && !stop_) {
      ResetEvent(io_event);
      OVERLAPPED read{};
      read.hEvent = io_event;
      const BOOL immediate = ReadFile(
          pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &count, &read);
      received = completed(immediate, read, 3000, count);
    }
    settings::Request request;
    if (received && settings::Decode(std::span(bytes.data(), count), request)) {
      ResetEvent(reply_event_);
      {
        std::lock_guard lock(mutex_);
        reply_.reset();
        pending_ = Pending{request, client};
      }
      if (!PostMessageW(window_, settings::kWake, 0, 0)) break;
      // No mutation timeout: after disconnect, a fresh read discovers outcome.
      // Shutdown stops this wait; it never forces DLL unload.
      const HANDLE events[] = {stop_event_, reply_event_};
      if (WaitForMultipleObjects(2, events, FALSE, INFINITE) ==
          WAIT_OBJECT_0 + 1) {
        std::optional<settings::Snapshot> reply;
        {
          std::lock_guard lock(mutex_);
          reply.swap(reply_);
        }
        if (reply) {
          auto encoded = settings::Encode(*reply, request.id);
          ResetEvent(io_event);
          OVERLAPPED write{};
          write.hEvent = io_event;
          const BOOL immediate =
              WriteFile(pipe, encoded.data(),
                        static_cast<DWORD>(encoded.size()), &count, &write);
          if (completed(immediate, write, 3000, count)) {
            // DisconnectNamedPipe discards unread output. Wait for the client's
            // delivery acknowledgement (or its close) before disconnecting.
            ResetEvent(io_event);
            OVERLAPPED acknowledged{};
            acknowledged.hEvent = io_event;
            uint8_t acknowledgement = 0;
            const BOOL read_ack =
                ReadFile(pipe, &acknowledgement, 1, &count, &acknowledged);
            completed(read_ack, acknowledged, 3000, count);
          }
        }
      }
    }
    DisconnectNamedPipe(pipe);
  }
  CloseHandle(pipe);
  CloseHandle(io_event);
}
bool LaunchModernSettings() {
  if (settings_service && settings_service->FrontendActive()) {
    return settings_service->FocusFrontend();
  }
  if (IsWindow(g_cfgwnd)) {
    SetForegroundWindow(g_cfgwnd);
    return false;
  }
  const std::wstring path =
      GetExecutableDirectory() + L"\\Settings\\AltSnap.Settings.exe";
  const std::wstring args = L"--host " + std::to_wstring(GetCurrentProcessId());
  if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    MessageBoxW(g_hwnd,
                L"The modern settings application is unavailable. Opening "
                L"legacy settings.",
                L"AltSnap", MB_OK);
    OpenConfig(0);
    return false;
  }
  // Explorer's desktop automation launches at the interactive shell's
  // integrity, rather than inheriting the elevated host token. Never silently
  // fall back to runas.
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  bool launched = false;
  {
    using Microsoft::WRL::ComPtr;
    ComPtr<IShellWindows> windows;
    ComPtr<IDispatch> desktop;
    ComPtr<IServiceProvider> provider;
    ComPtr<IShellBrowser> browser;
    ComPtr<IShellView> view;
    ComPtr<IDispatch> background;
    ComPtr<IShellFolderViewDual> folder;
    ComPtr<IDispatch> application;
    ComPtr<IShellDispatch2> shell;
    VARIANT empty{};
    long hwnd = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr,
                                   CLSCTX_LOCAL_SERVER,
                                   IID_PPV_ARGS(&windows))) &&
        SUCCEEDED(windows->FindWindowSW(&empty, &empty, SWC_DESKTOP, &hwnd,
                                        SWFO_NEEDDISPATCH, &desktop)) &&
        desktop && SUCCEEDED(desktop.As(&provider)) &&
        SUCCEEDED(provider->QueryService(SID_STopLevelBrowser,
                                         IID_PPV_ARGS(&browser))) &&
        SUCCEEDED(browser->QueryActiveShellView(&view)) &&
        SUCCEEDED(
            view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background))) &&
        SUCCEEDED(background.As(&folder)) &&
        SUCCEEDED(folder->get_Application(&application)) &&
        SUCCEEDED(application.As(&shell))) {
      BSTR executable = SysAllocString(path.c_str());
      VARIANT arguments{};
      arguments.vt = VT_BSTR;
      arguments.bstrVal = SysAllocString(args.c_str());
      launched = SUCCEEDED(
          shell->ShellExecute(executable, arguments, empty, empty, empty));
      VariantClear(&arguments);
      SysFreeString(executable);
    }
  }
  if (SUCCEEDED(initialized)) CoUninitialize();
  if (!launched) {
    MessageBoxW(g_hwnd,
                L"Could not launch settings through the desktop shell. Opening "
                L"legacy settings.",
                L"AltSnap", MB_OK | MB_ICONWARNING);
    OpenConfig(0);
  }
  return launched;
}
}  // namespace altsnap::app
