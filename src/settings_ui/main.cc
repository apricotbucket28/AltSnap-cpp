// Windows SDK headers depend on windows.h being included first.
// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
// clang-format on
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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>

#include "common/text_format.h"
#include "settings/protocol.h"
namespace altsnap::settings_ui {
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using settings::Operation;
using settings::Option;
using settings::Result;
constexpr std::array<const wchar_t*, settings::kOptionCount> kControlNames{
    L"AutoFocus",         L"Aero",
    L"SmartSizing",       L"SmartCandidates",
    L"KeepSnapped",       L"StickyResize",
    L"AutoSnap",          L"AutoRemaximize",
    L"AeroTopMaximizes",  L"AeroDoubleClickShift",
    L"MultipleInstances", L"FullScreen",
    L"BLMaximized",       L"FancyZone",
    L"MaximizeOnClick",   L"RestoreOnClick",
    L"TopmostIndicator",  L"CenterFraction",
    L"SidesFraction",     L"AeroHoffset",
    L"AeroVoffset",       L"SnapThreshold",
    L"AeroThreshold",     L"SnapGap",
    L"AeroMaxSpeed",      L"AeroSpeedTau",
    L"MoveTrans",         L"TitlebarWithoutAlt",
    L"TitlebarWithAlt",   L"LongClickMove"};
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
            for (const wchar_t* page : {L"General", L"Snapping", L"Mouse",
                                        L"Advanced", L"Appearance", L"About"})
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
    for (size_t i = 0; i < settings::kOptionCount; ++i) {
      if (settings::kOptions[i].kind == settings::ValueKind::kToggle) {
        Find<ToggleSwitch>(kControlNames[i])
            .Toggled([weak, i](auto&& sender, auto&&) {
              if (auto self = weak.lock(); self && !self->loading_) {
                self->draft_.Set(static_cast<Option>(i),
                                 sender.template as<ToggleSwitch>().IsOn());
                self->UpdateApply();
              }
            });
      } else if (settings::kOptions[i].kind == settings::ValueKind::kNumber ||
                 settings::kOptions[i].kind ==
                     settings::ValueKind::kSignedNumber) {
        Find<TextBox>(kControlNames[i])
            .TextChanged([weak, i](auto&& sender, auto&&) {
              if (auto self = weak.lock(); self && !self->loading_) {
                const auto text = sender.template as<TextBox>().Text();
                const auto value = settings::ParseNumber(
                    static_cast<Option>(i), std::wstring_view(text));
                self->invalid_[i] = !value;
                if (value) self->draft_.Set(static_cast<Option>(i), *value);
                self->Find<TextBlock>(
                        (std::wstring(kControlNames[i]) + L"Error").c_str())
                    .Visibility(value ? Visibility::Collapsed
                                      : Visibility::Visible);
                self->UpdateApply();
              }
            });
      } else {
        Find<ComboBox>(kControlNames[i])
            .SelectionChanged([weak, i](auto&& sender, auto&&) {
              if (auto self = weak.lock(); self && !self->loading_) {
                const int selected =
                    sender.template as<ComboBox>().SelectedIndex();
                if (selected >= 0 &&
                    selected <= static_cast<int>(settings::kOptions[i].maximum))
                  self->draft_.Set(static_cast<Option>(i),
                                   static_cast<uint32_t>(selected));
                self->UpdateApply();
              }
            });
      }
    }
    BuildMouse();
    window_.AppWindow().Closing([weak](auto&&, auto&& args) {
      if (auto self = weak.lock(); self && !self->allow_close_) {
        if (self->busy_ || self->dialog_open_) {
          args.Cancel(true);
          return;
        }
        if (self->HasChanges()) {
          args.Cancel(true);
          self->ConfirmDiscard(std::nullopt);
        }
      }
    });
    Find<Button>(L"TestWindow").Click([weak](auto&&, auto&&) {
      if (auto self = weak.lock()) self->Request(Operation::kTestWindow);
    });
    Find<Button>(L"Apply").Click([weak](auto&&, auto&&) {
      if (auto s = weak.lock()) s->Request(Operation::kApply);
    });
    Find<Button>(L"Okay").Click([weak](auto&&, auto&&) {
      if (auto self = weak.lock()) {
        if (self->HasChanges())
          self->Request(Operation::kApply, true);
        else
          self->window_.Close();
      }
    });
    Find<Button>(L"Reload").Click([weak](auto&&, auto&&) {
      if (auto s = weak.lock()) s->ConfirmDiscard(Operation::kRead);
    });
    Find<Button>(L"Legacy").Click([weak](auto&&, auto&&) {
      if (auto s = weak.lock()) s->ConfirmDiscard(Operation::kLegacy);
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
    UpdateApply();
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
  std::vector<settings::ActionChoice> MouseActions(size_t index) const {
    const auto& binding = settings::kBindings[index];
    if (binding.input == settings::MouseInput::kWheel ||
        binding.input == settings::MouseInput::kHorizontalWheel)
      return {settings::kWheelActions.begin(), settings::kWheelActions.end()};
    const bool restricted =
        binding.context == settings::MouseContext::kMoving ||
        binding.context == settings::MouseContext::kResizing ||
        binding.input == settings::MouseInput::kMoveUp ||
        binding.input == settings::MouseInput::kResizeUp;
    std::vector<settings::ActionChoice> actions;
    for (const auto& action : settings::kButtonActions)
      if (!restricted || !action.idle_button_only) actions.push_back(action);
    return actions;
  }
  void ShowMouseBinding(size_t index) {
    const bool was_loading = loading_;
    loading_ = true;
    const auto row = mouse_rows_[index];
    const auto combo = row.FindName(L"Action").as<ComboBox>();
    const auto actions = MouseActions(index);
    while (combo.Items().Size() > actions.size()) combo.Items().RemoveAtEnd();
    const auto& value = draft_.GetText(index);
    int selected = -1;
    for (size_t i = 0; i < actions.size(); ++i)
      if (value == actions[i].id) selected = static_cast<int>(i);
    if (selected < 0) {
      ComboBoxItem custom;
      custom.Content(box_value(value.empty() ? L"Empty action (no action)"
                                             : L"Custom / parameterized: " +
                                                   value.substr(0, 80)));
      combo.Items().Append(custom);
      selected = static_cast<int>(actions.size());
    }
    combo.SelectedIndex(selected);
    row.FindName(L"Raw").as<TextBox>().Text(value);
    row.FindName(L"Error").as<TextBlock>().Visibility(
        (!(draft_.Changes().text_changed & (uint64_t{1} << index)) ||
         settings::ValidText(value))
            ? Visibility::Collapsed
            : Visibility::Visible);
    loading_ = was_loading;
  }
  void ShowModifier() {
    const bool was_loading = loading_;
    loading_ = true;
    const auto& value = draft_.GetText(settings::kModKey);
    Find<TextBox>(L"MouseModifier").Text(value);
    const std::array values{L"", L"A2 A3", L"A0 A1", L"5B 5C"};
    int selected = 4;
    for (size_t i = 0; i < values.size(); ++i)
      if (value == values[i]) selected = static_cast<int>(i);
    Find<ComboBox>(L"ModifierPreset").SelectedIndex(selected);
    Find<TextBlock>(L"MouseError")
        .Visibility((!(draft_.Changes().text_changed &
                       (uint64_t{1} << settings::kModKey)) ||
                     settings::ValidTextOption(settings::kModKey, value))
                        ? Visibility::Collapsed
                        : Visibility::Visible);
    loading_ = was_loading;
  }
  void BuildMouse() {
    const auto weak = weak_from_this();
    for (size_t i = 0; i < settings::kBindings.size(); ++i) {
      const auto row =
          Markup::XamlReader::Load(
              LR"xaml(<UserControl xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"><StackPanel Spacing="8">
        <ComboBox x:Name="Action" HorizontalAlignment="Stretch" />
        <Expander x:Name="Details" Header="Parameters and custom action" HorizontalAlignment="Stretch" HorizontalContentAlignment="Stretch">
          <StackPanel Spacing="10">
            <TextBox x:Name="Raw" Header="Action ID and parameters" />
            <TextBlock Text="Edit the full ID, or choose an action above and build its parameters below. Custom IDs are kept exactly. Examples: MoveTNEdge_L or Step_R_10. Parameter effects depend on the action; wheel actions usually use wheel direction." TextWrapping="Wrap" />
            <ComboBox x:Name="Flag" Header="Direction / operation" SelectedIndex="0" HorizontalAlignment="Stretch">
              <ComboBoxItem Content="Default (0)" /><ComboBoxItem Content="Left (L)" /><ComboBoxItem Content="Up (U)" /><ComboBoxItem Content="Right (R)" /><ComboBoxItem Content="Down (D)" /><ComboBoxItem Content="Set (S)" /><ComboBoxItem Content="Toggle (T)" />
            </ComboBox>
            <TextBox x:Name="Amount" Header="Number (optional, signed 16-bit)" PlaceholderText="Use the action's default" />
            <Button x:Name="Build" Content="Use these parameters" />
            <TextBlock x:Name="Error" Visibility="Collapsed" Text="Use a single-line ID without surrounding whitespace or quotes; numbers must be from -32768 to 32767." TextWrapping="Wrap" Foreground="{ThemeResource SystemFillColorCriticalBrush}" />
          </StackPanel>
        </Expander>
      </StackPanel></UserControl>)xaml")
              .as<Control>();
      mouse_rows_.push_back(row);
      Find<StackPanel>(L"MouseMappings").Children().Append(row);
      const auto combo = row.FindName(L"Action").as<ComboBox>();
      combo.Header(box_value(settings::kBindings[i].label));
      row.FindName(L"Raw").as<TextBox>().Header(
          box_value(std::wstring(settings::kBindings[i].label) +
                    L" — action ID and parameters"));
      for (const auto& action : MouseActions(i)) {
        ComboBoxItem item;
        item.Content(box_value(action.label));
        combo.Items().Append(item);
      }
      combo.SelectionChanged([weak, i](auto&& sender, auto&&) {
        if (auto self = weak.lock(); self && !self->loading_) {
          const int selected = sender.template as<ComboBox>().SelectedIndex();
          const auto actions = self->MouseActions(i);
          if (selected < 0 || static_cast<size_t>(selected) >= actions.size())
            return;
          self->draft_.SetText(i, actions[selected].id);
          self->ShowMouseBinding(i);
          self->UpdateApply();
        }
      });
      row.FindName(L"Raw").as<TextBox>().TextChanged(
          [weak, i](auto&& sender, auto&&) {
            if (auto self = weak.lock(); self && !self->loading_) {
              self->draft_.SetText(
                  i, std::wstring(sender.template as<TextBox>().Text()));
              self->ShowMouseBinding(i);
              self->UpdateApply();
            }
          });
      row.FindName(L"Build").as<Button>().Click([weak, i](auto&&, auto&&) {
        if (auto self = weak.lock()) {
          const auto row = self->mouse_rows_[i];
          const std::wstring amount(
              row.FindName(L"Amount").as<TextBox>().Text());
          const auto result = settings::BuildAction(
              self->draft_.GetText(i),
              row.FindName(L"Flag").as<ComboBox>().SelectedIndex(), amount);
          if (!result) {
            row.FindName(L"Error").as<TextBlock>().Visibility(
                Visibility::Visible);
            return;
          }
          self->draft_.SetText(i, *result);
          self->ShowMouseBinding(i);
          self->UpdateApply();
        }
      });
    }
    Find<ComboBox>(L"MouseContext").SelectionChanged([weak](auto&&, auto&&) {
      if (auto self = weak.lock()) self->UpdateApply();
    });
    Find<ComboBox>(L"ModifierPreset")
        .SelectionChanged([weak](auto&& sender, auto&&) {
          if (auto self = weak.lock(); self && !self->loading_) {
            const int selected = sender.template as<ComboBox>().SelectedIndex();
            const std::array values{L"", L"A2 A3", L"A0 A1", L"5B 5C"};
            if (selected >= 0 && selected < 4) {
              self->draft_.SetText(settings::kModKey, values[selected]);
              self->ShowModifier();
              self->UpdateApply();
            } else
              self->Find<TextBox>(L"MouseModifier")
                  .Focus(FocusState::Programmatic);
          }
        });
    Find<TextBox>(L"MouseModifier").TextChanged([weak](auto&& sender, auto&&) {
      if (auto self = weak.lock(); self && !self->loading_) {
        self->draft_.SetText(
            settings::kModKey,
            std::wstring(sender.template as<TextBox>().Text()));
        self->Find<TextBlock>(L"MouseError")
            .Visibility(
                settings::ValidTextOption(
                    settings::kModKey, self->draft_.GetText(settings::kModKey))
                    ? Visibility::Collapsed
                    : Visibility::Visible);
        self->ShowModifier();
        self->UpdateApply();
      }
    });
    const std::array names{L"HotclickMiddle", L"Hotclick4", L"Hotclick5"};
    for (size_t i = 0; i < names.size(); ++i) {
      const auto change = [weak, key = static_cast<unsigned>(i + 4)](
                              auto&& sender, auto&&) {
        if (auto self = weak.lock(); self && !self->loading_) {
          auto text = self->draft_.GetText(settings::kHotclicks);
          if (sender.template as<CheckBox>().IsChecked().Value())
            text = AddKeyToList(std::move(text), key);
          else
            while (IsKeyInList(text, key))
              text = RemoveKeyFromList(std::move(text), key);
          self->draft_.SetText(settings::kHotclicks, std::move(text));
          self->UpdateApply();
        }
      };
      Find<CheckBox>(names[i]).Checked(change);
      Find<CheckBox>(names[i]).Unchecked(change);
    }
  }
  void Status(const wchar_t* title, const wchar_t* message,
              InfoBarSeverity severity) {
    const auto status = Find<InfoBar>(L"Status");
    status.Title(title);
    status.Message(message);
    status.Severity(severity);
  }
  fire_and_forget ConfirmDiscard(std::optional<Operation> operation) {
    if (busy_ || dialog_open_) co_return;
    const auto lifetime = shared_from_this();
    if (HasChanges()) {
      dialog_open_ = true;
      ContentDialog dialog;
      dialog.XamlRoot(root_.XamlRoot());
      dialog.RequestedTheme(root_.ActualTheme());
      dialog.Title(box_value(L"Discard unsaved changes?"));
      dialog.Content(
          box_value(L"Your changes have not been applied to AltSnap."));
      dialog.PrimaryButtonText(L"Discard changes");
      dialog.CloseButtonText(L"Keep editing");
      dialog.DefaultButton(ContentDialogButton::Close);
      ContentDialogResult result = ContentDialogResult::None;
      try {
        result = co_await dialog.ShowAsync();
      } catch (const hresult_error& error) {
        dialog_open_ = false;
        Status(L"Could not open confirmation", error.message().c_str(),
               InfoBarSeverity::Error);
        co_return;
      }
      dialog_open_ = false;
      if (closed_ || result != ContentDialogResult::Primary) co_return;
    }
    if (operation)
      Request(*operation);
    else {
      allow_close_ = true;
      window_.Close();
    }
  }
  bool ValidInput() const {
    return draft_.Changes().Valid() &&
           std::none_of(invalid_.begin(), invalid_.end(),
                        [](bool value) { return value; });
  }
  bool HasChanges() const { return draft_.Changes().Any() || !ValidInput(); }
  void SetControlsEnabled(bool enabled) {
    Find<Button>(L"TestWindow").IsEnabled(enabled && !HasChanges());
    for (size_t i = 0; i < settings::kOptionCount; ++i)
      Find<Control>(kControlNames[i]).IsEnabled(enabled);
    Find<Control>(L"MouseContext").IsEnabled(enabled);
    Find<Control>(L"MouseModifier").IsEnabled(enabled);
    Find<Control>(L"ModifierPreset").IsEnabled(enabled);
    for (const wchar_t* name : {L"HotclickMiddle", L"Hotclick4", L"Hotclick5"})
      Find<Control>(name).IsEnabled(enabled);
    const int context = Find<ComboBox>(L"MouseContext").SelectedIndex();
    const bool available =
        context == 1   ? !draft_.GetText(settings::kModKey).empty()
        : context == 2 ? draft_.Get(Option::kTitlebarWithoutAlt) ||
                             draft_.Get(Option::kTitlebarWithAlt)
                       : true;
    for (size_t i = 0; i < mouse_rows_.size(); ++i) {
      mouse_rows_[i].Visibility(
          static_cast<int>(settings::kBindings[i].context) == context
              ? Visibility::Visible
              : Visibility::Collapsed);
      mouse_rows_[i].IsEnabled(enabled && available);
    }
    const wchar_t* help =
        context == 1 ? (available ? L"Used while holding any alternate "
                                    L"modifier. Other contexts can have "
                                    L"additional modifier overrides in the INI."
                                  : L"Add a modifier below to enable Alternate "
                                    L"actions. Your mappings are retained.")
        : context == 2
            ? (available ? L"Used over the title bar under the activation "
                           L"conditions selected below."
                         : L"Enable a title-bar activation option below to "
                           L"edit these mappings. Your mappings are retained.")
        : context >= 3 ? L"Used when pressing another button or turning a "
                         L"wheel during an AltSnap drag. Release actions are "
                         L"configured in the first three contexts."
                       : L"Used with your activation key (Alt by default), or "
                         L"a hotclick button.";
    Find<TextBlock>(L"MouseContextHelp").Text(help);
    // Dependent options keep their draft values while the parent is off.
    Find<Control>(L"SmartSizing")
        .IsEnabled(enabled && draft_.Get(Option::kAero));
    Find<Control>(L"SmartCandidates")
        .IsEnabled(enabled && draft_.Get(Option::kAero) &&
                   draft_.Get(Option::kSmartSizing));
    Find<Control>(L"KeepSnapped")
        .IsEnabled(enabled && draft_.Get(Option::kAero));
  }
  void ShowDraft() {
    loading_ = true;
    for (size_t i = 0; i < settings::kOptionCount; ++i) {
      const uint32_t value = draft_.Get(static_cast<Option>(i));
      if (settings::kOptions[i].kind == settings::ValueKind::kToggle)
        Find<ToggleSwitch>(kControlNames[i]).IsOn(value != 0);
      else if (settings::kOptions[i].kind == settings::ValueKind::kNumber ||
               settings::kOptions[i].kind ==
                   settings::ValueKind::kSignedNumber) {
        const std::wstring text =
            settings::kOptions[i].kind == settings::ValueKind::kSignedNumber
                ? std::to_wstring(settings::SignedNumber(value))
                : std::to_wstring(value);
        Find<TextBox>(kControlNames[i]).Text(text);
        invalid_[i] = false;
        Find<TextBlock>((std::wstring(kControlNames[i]) + L"Error").c_str())
            .Visibility(Visibility::Collapsed);
      } else {
        const auto combo = Find<ComboBox>(kControlNames[i]);
        const uint32_t count = settings::kOptions[i].maximum + 1;
        while (combo.Items().Size() > count) combo.Items().RemoveAtEnd();
        if (value >= count) {
          ComboBoxItem custom;
          custom.Content(
              box_value(L"Custom value (" + std::to_wstring(value) + L")"));
          custom.IsEnabled(false);
          combo.Items().Append(custom);
          combo.SelectedIndex(static_cast<int>(count));
        } else
          combo.SelectedIndex(static_cast<int>(value));
      }
    }
    for (size_t i = 0; i < settings::kBindings.size(); ++i) ShowMouseBinding(i);
    ShowModifier();
    const std::array names{L"HotclickMiddle", L"Hotclick4", L"Hotclick5"};
    for (size_t i = 0; i < names.size(); ++i)
      Find<CheckBox>(names[i]).IsChecked(IsKeyInList(
          draft_.GetText(settings::kHotclicks), static_cast<unsigned>(i + 4)));
    Find<TextBlock>(L"MouseError").Visibility(Visibility::Collapsed);
    loading_ = false;
  }
  void UpdateApply() {
    const auto patch = draft_.Changes();
    Find<Button>(L"Okay").IsEnabled(connected_ && !busy_ && patch.Valid() &&
                                    ValidInput());
    Find<Button>(L"Apply").IsEnabled(connected_ && !busy_ && patch.Any() &&
                                     patch.Valid() && ValidInput());
    Find<Button>(L"Cancel").IsEnabled(!busy_);
    Find<TextBlock>(L"DraftStatus")
        .Text(HasChanges() ? L"You have unsaved changes."
                           : L"All changes are saved.");
    SetControlsEnabled(connected_ && !busy_);
  }
  void UpdateTitleTheme() {
    using Microsoft::UI::Windowing::TitleBarTheme;
    window_.AppWindow().TitleBar().PreferredTheme(
        root_.ActualTheme() == ElementTheme::Dark ? TitleBarTheme::Dark
                                                  : TitleBarTheme::Light);
  }
  fire_and_forget Request(Operation operation, bool close_after = false) {
    if (busy_ || closed_ || (operation == Operation::kApply && !ValidInput()))
      co_return;
    busy_ = true;
    UpdateApply();
    Find<Button>(L"Reload").IsEnabled(false);
    Find<Button>(L"Legacy").IsEnabled(false);
    SetControlsEnabled(false);
    Status(operation == Operation::kApply        ? L"Saving changes"
           : operation == Operation::kTestWindow ? L"Opening test window"
                                                 : L"Reading settings",
           L"Waiting for the AltSnap host.", InfoBarSeverity::Informational);
    const settings::Request request{
        ++request_id_, operation, snapshot_.revision,
        operation == Operation::kApply ? draft_.Changes() : settings::Patch{}};
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

    if (!response) {
      connected_ = false;
      Status(L"Connection interrupted",
             L"The outcome may be unknown. Reload to read the persisted value "
             L"before applying again. Your draft is retained.",
             InfoBarSeverity::Error);
    } else if (response->result == Result::kOk) {
      if (operation == Operation::kTestWindow) {
        Status(L"Test window opened",
               L"The test window uses your saved settings.",
               InfoBarSeverity::Success);
        UpdateApply();
        return;
      }
      snapshot_ = *response;
      draft_.Load(snapshot_.values);
      ShowDraft();
      Find<TextBlock>(L"ConfigPath").Text(L"Configuration\n" + snapshot_.path);
      Status(operation == Operation::kApply ? L"Changes saved"
                                            : L"Connected to AltSnap",
             snapshot_.enabled ? L"Hooks are enabled. Settings are active."
                               : L"Hooks remain disabled. Saved settings take "
                                 L"effect when AltSnap is enabled.",
             InfoBarSeverity::Success);
      if (operation == Operation::kLegacy || close_after) {
        allow_close_ = true;
        window_.Close();
        return;
      }
    } else if (operation == Operation::kTestWindow &&
               response->result == Result::kActivationFailed) {
      Status(L"Could not open the test window",
             L"Your saved settings are unchanged.", InfoBarSeverity::Error);
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
  bool loading_ = false, allow_close_ = false, dialog_open_ = false;
  std::array<bool, settings::kOptionCount> invalid_{};
  std::vector<Control> mouse_rows_;
  settings::Draft draft_;
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
