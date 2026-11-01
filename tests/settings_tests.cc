#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "common/text_format.h"
#include "settings/apply.h"
#include "settings/repository.h"
namespace {
void Check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "line %d: %s (Win32 error %lu)\n", line, expression,
                 GetLastError());
    std::exit(1);
  }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)
}  // namespace
int main() {
  using namespace altsnap::settings;
  Request request{73, Operation::kApply, 0x123456789abcdef0ull,
                  SingleChange(Option::kAutoFocus, 1)},
      decoded;
  auto bytes = Encode(request);
  CHECK(Decode(bytes, decoded));
  CHECK(decoded.id == request.id && decoded.revision == request.revision &&
        decoded.patch.values[0]);
  CHECK(!Decode(std::span(bytes.data(), bytes.size() - 1), decoded));
  bytes.push_back(0);
  CHECK(!Decode(bytes, decoded));
  bytes.pop_back();
  for (size_t offset : {0u, 4u, 12u, 28u}) {
    auto bad = bytes;
    bad[offset] = 255;
    CHECK(!Decode(bad, decoded));
  }
  auto bad_mask = bytes;
  bad_mask[27] = 128;
  CHECK(!Decode(bad_mask, decoded));
  Request tool{74, Operation::kTestWindow};
  CHECK(Decode(Encode(tool), decoded));
  tool.patch = SingleChange(Option::kAero, 1);
  CHECK(!Decode(Encode(tool), decoded));
  Snapshot snapshot, restored;
  snapshot.result = Result::kConflict;
  snapshot.revision = 0xfedcba9876543210ull;
  snapshot.path = L"C:\\設定\\AltSnap.ini";
  snapshot.values[0] = {1, true, L"custom"};
  bytes = Encode(snapshot, 73);
  CHECK(Decode(bytes, 73, restored));
  CHECK(restored.path == snapshot.path &&
        restored.values[0].raw == snapshot.values[0].raw &&
        restored.revision == snapshot.revision);
  CHECK(!Decode(bytes, 74, restored));
  auto oversized = bytes;
  oversized.resize(kMaxReply + 1);
  CHECK(!Decode(oversized, 73, restored));
  bytes[28] = 255;
  CHECK(!Decode(bytes, 73, restored));
  Snapshot largest;
  largest.path.assign(32767, L'p');
  for (auto& value : largest.values) value.raw.assign(4095, L'x');
  bytes = Encode(largest, 73);
  CHECK(bytes.size() <= kMaxReply && Decode(bytes, 73, restored));
  wchar_t temp[MAX_PATH], filename[MAX_PATH];
  CHECK(GetTempPathW(MAX_PATH, temp));
  CHECK(GetTempFileNameW(temp, L"ast", 0, filename));
  const std::wstring path = std::wstring(filename) + L"-設定.ini";
  HANDLE unicode_file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_NEW, 0, nullptr);
  CHECK(unicode_file != INVALID_HANDLE_VALUE);
  const std::wstring unicode_text = L"\xfeff[Unknown]\r\nUnicode=設定\r\n";
  DWORD written = 0;
  CHECK(WriteFile(unicode_file, unicode_text.data(),
                  static_cast<DWORD>(unicode_text.size() * sizeof(wchar_t)),
                  &written, nullptr));
  CloseHandle(unicode_file);
  Repository repository(path);
  auto initial = repository.Read();
  CHECK(initial.result == Result::kOk && !initial.values[0].present &&
        !initial.values[0].effective);
  CHECK(WritePrivateProfileStringW(L"Unknown", L"Keep", L"unchanged",
                                   path.c_str()));
  CHECK(repository.Write(initial.revision, true) == Result::kConflict);
  initial = repository.Read();
  CHECK(repository.Write(initial.revision, true) == Result::kOk);
  auto current = repository.Read();
  CHECK(current.values[0].present && current.values[0].raw == L"1" &&
        current.values[0].effective);
  CHECK(repository.Write(initial.revision, false) == Result::kConflict);
  wchar_t value[64];
  GetPrivateProfileStringW(L"Unknown", L"Keep", L"", value, 64, path.c_str());
  CHECK(std::wstring(value) == L"unchanged");
  GetPrivateProfileStringW(L"Unknown", L"Unicode", L"", value, 64,
                           path.c_str());
  CHECK(std::wstring(value) == L"設定");
  for (const auto [text, expected] : {std::pair{L"256", false},
                                      {L"257", true},
                                      {L"-256", false},
                                      {L"0x1", false},
                                      {L"2", true}}) {
    CHECK(WritePrivateProfileStringW(L"General", L"AutoFocus", text,
                                     path.c_str()));
    CHECK(repository.Read().values[0].effective ==
          static_cast<uint32_t>(expected));
  }
  CHECK(
      WritePrivateProfileStringW(L"General", L"AutoFocus", L"", path.c_str()));
  current = repository.Read();
  CHECK(current.values[0].present && current.values[0].raw.empty() &&
        !current.values[0].effective);
  CHECK(WritePrivateProfileStringW(L"General", L"AutoFocus", L"custom",
                                   path.c_str()));
  current = repository.Read();
  CHECK(current.values[0].present && current.values[0].raw == L"custom");
  CHECK(WritePrivateProfileStringW(L"General", L"AutoFocus", nullptr,
                                   path.c_str()));
  current = repository.Read();
  CHECK(!current.values[0].present && !current.values[0].effective);
  // Deny writes without relying on administrator/read-only attribute behavior.
  HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(locked != INVALID_HANDLE_VALUE);
  CHECK(repository.Write(current.revision, true) == Result::kWriteFailed);
  CloseHandle(locked);
  struct FakeRuntime final : Runtime {
    bool enabled = true;
    bool stop_ok = true, start_ok = true;
    int stops = 0, starts = 0;
    bool Enabled() const override { return enabled; }
    bool Stop() override {
      ++stops;
      if (stop_ok) enabled = false;
      return stop_ok;
    }
    bool Start() override {
      ++starts;
      enabled = start_ok;
      return start_ok;
    }
  } runtime;
  current = repository.Read();
  CHECK(ApplyAutoFocus(repository, current.revision, true, runtime) ==
        Result::kOk);
  CHECK(runtime.stops == 1 && runtime.starts == 1 && runtime.enabled);
  CHECK(ApplyAutoFocus(repository, current.revision, false, runtime) ==
        Result::kConflict);
  CHECK(runtime.stops == 1 && runtime.starts == 1);
  runtime.enabled = false;
  CHECK(ApplyAutoFocus(repository, repository.Read().revision, false,
                       runtime) == Result::kOk);
  CHECK(runtime.stops == 1 && runtime.starts == 1 && !runtime.enabled);
  runtime.enabled = true;
  runtime.stop_ok = false;
  CHECK(ApplyAutoFocus(repository, repository.Read().revision, true, runtime) ==
        Result::kActivationFailed);
  CHECK(runtime.stops == 2 && runtime.starts == 1);
  CHECK(repository.Read().values[0].effective);
  runtime.stop_ok = true;
  runtime.start_ok = false;
  CHECK(ApplyAutoFocus(repository, repository.Read().revision, false,
                       runtime) == Result::kActivationFailed);
  CHECK(runtime.stops == 3 && runtime.starts == 2 && !runtime.enabled);
  CHECK(!repository.Read().values[0].effective);
  const Snapshot legacy_draft = repository.Read();
  CHECK(WritePrivateProfileStringW(L"Unknown", L"Keep", L"other legacy page",
                                   path.c_str()));
  CHECK(repository.WriteIfAutoFocusUnchanged(legacy_draft, true) ==
        Result::kOk);
  CHECK(repository.WriteIfAutoFocusUnchanged(legacy_draft, false) ==
        Result::kConflict);

  // Missing keys use engine defaults; an unrelated Apply never materializes
  // them.
  current = repository.Read();
  CHECK(current.values[Index(Option::kAero)].effective == 1);
  CHECK(current.values[Index(Option::kStickyResize)].effective == 0);
  CHECK(!current.values[Index(Option::kStickyResize)].present);
  Draft draft;
  draft.Load(current.values);
  CHECK(!draft.Changes().changed);
  draft.Set(Option::kAero, 0);
  draft.Set(Option::kAero, 1);
  CHECK(!draft.Changes().changed);
  CHECK(WritePrivateProfileStringW(L"General", L"SmartAero", L"261",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"General", L"StickyResize", L"259",
                                   path.c_str()));
  CHECK(
      WritePrivateProfileStringW(L"General", L"AutoSnap", L"99", path.c_str()));
  current = repository.Read();
  CHECK(current.values[Index(Option::kStickyResize)].effective == 1);
  CHECK(current.values[Index(Option::kAutoSnap)].effective == 99);
  draft.Load(current.values);
  CHECK(draft.Changes().Valid() && !draft.Changes().changed);
  draft.Set(Option::kSmartSizing, 0);
  draft.Set(Option::kSmartCandidates, 1);
  draft.Set(Option::kStickyResize, 2);
  draft.Set(Option::kAero, 0);
  const auto batch = draft.Changes();
  CHECK(batch.Valid());
  runtime.enabled = true;
  runtime.start_ok = true;
  const int stops = runtime.stops, starts = runtime.starts;
  CHECK(ApplyPatch(repository, current.revision, batch, runtime) ==
        Result::kOk);
  CHECK(runtime.stops == stops + 1 && runtime.starts == starts + 1);
  current = repository.Read();
  CHECK(current.values[Index(Option::kSmartSizing)].raw == L"262");
  CHECK(current.values[Index(Option::kKeepSnapped)].effective == 1);
  CHECK(current.values[Index(Option::kStickyResize)].raw == L"258");
  CHECK(current.values[Index(Option::kAutoSnap)].raw == L"99");
  CHECK(ApplyPatch(repository, current.revision, {}, runtime) == Result::kOk);
  CHECK(runtime.stops == stops + 1 && runtime.starts == starts + 1);
  Patch invalid = SingleChange(Option::kAutoSnap, 4);
  CHECK(repository.Write(current.revision, invalid) == Result::kInvalid);
  CHECK(repository.Read().revision == current.revision);
  invalid.changed = 1u << kOptionCount;
  CHECK(!invalid.Valid());
  // Complete model and protocol round trip, including all raw/presence fields.
  bytes = Encode(current, 123);
  CHECK(Decode(bytes, 123, restored));
  for (size_t i = 0; i < kOptionCount; ++i) {
    CHECK(restored.values[i].raw == current.values[i].raw);
    CHECK(restored.values[i].present == current.values[i].present);
    CHECK(restored.values[i].effective == current.values[i].effective);
  }
  for (size_t length = 0; length < bytes.size(); ++length)
    CHECK(!Decode(std::span(bytes.data(), length), 123, restored));
  request.patch = batch;
  bytes = Encode(request);
  CHECK(Decode(bytes, decoded) && decoded.patch.changed == batch.changed);
  bytes[4] = 1;
  CHECK(!Decode(bytes, decoded));  // Old peers must fail closed.
  // Advanced page: typed widths, signed editing, cross-section writes, and
  // both shared bit fields. Parsing for edits is stricter than reading old
  // INIs.
  CHECK(ParseNumber(Option::kSnapGap, L"-128") == uint32_t(-128));
  CHECK(ParseNumber(Option::kSnapGap, L"127") == 127u);
  CHECK(!ParseNumber(Option::kSnapGap, L"-129"));
  CHECK(!ParseNumber(Option::kSnapGap, L"128"));
  CHECK(ParseNumber(Option::kAeroMaxSpeed, L"65535") == 65535u);
  for (const auto text :
       {L"65536", L"-1", L"", L"1.5", L"12px", L" 1", L"+1", L"999999999999"})
    CHECK(!ParseNumber(Option::kAeroMaxSpeed, text));
  CHECK(!ParseNumber(Option::kCenterFraction, L"101"));
  CHECK(ParseNumber(Option::kSidesFraction, L"255") == 255u);
  CHECK(WritePrivateProfileStringW(L"General", L"AeroTopMaximizes",
                                   L"untouched", path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Advanced", L"AeroTopMaximizes", L"257",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"General", L"MMMaximize", L"258",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Advanced", L"AeroMaxSpeed", L"65534",
                                   path.c_str()));
  CHECK(
      WritePrivateProfileStringW(L"Advanced", L"SnapGap", L"-7", path.c_str()));
  CHECK(WritePrivateProfileStringW(L"General", L"CenterFraction", L"custom",
                                   path.c_str()));
  current = repository.Read();
  CHECK(current.values[Index(Option::kAeroSpeedTau)].effective == 64);
  CHECK(WritePrivateProfileStringW(L"Advanced", L"MultipleInstances", L"256",
                                   path.c_str()));
  CHECK(repository.Read().values[Index(Option::kMultipleInstances)].effective ==
        1);
  current = repository.Read();
  CHECK(current.values[Index(Option::kMoveTrans)].effective == 255);
  CHECK(current.values[Index(Option::kSidesFraction)].effective == 255);
  CHECK(current.values[Index(Option::kFullScreen)].effective == 1);
  CHECK(current.values[Index(Option::kAeroMaxSpeed)].effective == 65534);
  CHECK(SignedNumber(current.values[Index(Option::kSnapGap)].effective) == -7);
  draft.Load(current.values);
  draft.Set(Option::kAeroTopMaximizes, 0);
  draft.Set(Option::kAeroDoubleClickShift, 1);
  draft.Set(Option::kMaximizeOnClick, 1);
  draft.Set(Option::kRestoreOnClick, 0);
  draft.Set(Option::kFancyZone, 1);
  draft.Set(Option::kSnapGap, uint32_t(-128));
  draft.Set(Option::kAeroMaxSpeed, 1024);
  runtime.enabled = true;
  const int advanced_stops = runtime.stops;
  CHECK(ApplyPatch(repository, current.revision, draft.Changes(), runtime) ==
        Result::kOk);
  CHECK(runtime.stops == advanced_stops + 1);
  current = repository.Read();
  CHECK(current.values[Index(Option::kAeroTopMaximizes)].raw == L"258");
  CHECK(current.values[Index(Option::kMaximizeOnClick)].raw == L"257");
  CHECK(current.values[Index(Option::kFancyZone)].raw == L"1");
  CHECK(current.values[Index(Option::kSnapGap)].raw == L"-128");
  CHECK(current.values[Index(Option::kCenterFraction)].raw == L"custom");
  GetPrivateProfileStringW(L"General", L"AeroTopMaximizes", L"", value, 64,
                           path.c_str());
  CHECK(std::wstring(value) == L"untouched");
  CHECK(repository.Write(current.revision,
                         SingleChange(Option::kSnapGap, uint32_t(-129))) ==
        Result::kInvalid);
  CHECK(repository.Write(current.revision,
                         SingleChange(Option::kAeroMaxSpeed, 65536)) ==
        Result::kInvalid);
  CHECK(repository.Read().revision == current.revision);
  CHECK(repository.WriteIfUnchanged(
            initial, SingleChange(Option::kFancyZone, 0)) == Result::kConflict);
  bytes = Encode(current, 321);
  CHECK(Decode(bytes, 321, restored));
  CHECK(SignedNumber(restored.values[Index(Option::kSnapGap)].effective) ==
        -128);
  CHECK(restored.values[Index(Option::kAeroMaxSpeed)].effective == 1024);
  // Mouse inventory and engine defaults: missing is Nothing, not template Move.
  CHECK(kBindings.size() == 41);
  CHECK(std::wstring(kBindings[38].key) == L"MB5R");
  CHECK(std::wstring(kBindings[31].key) == L"MB5M");
  for (size_t i = 0; i < kBindings.size(); ++i) {
    for (size_t j = 0; j < i; ++j)
      CHECK(std::wstring(kBindings[i].key) != kBindings[j].key);
    CHECK(!repository.Read().values.text[i].present);
    CHECK(repository.Read().values.text[i].effective == L"Nothing");
  }
  CHECK(!repository.Read().values.text[kModKey].present);
  CHECK(BuildAction(L"SideSnap_R_12", 1, L"-32768") == L"SideSnap_L_-32768");
  CHECK(BuildAction(L"Volume", 5, L"50") == L"Volume_S_50");
  CHECK(BuildAction(L"Custom", 0, L"12") == L"Custom_0_12");
  CHECK(BuildAction(L"Custom_R_12", 0, L"") == L"Custom");
  CHECK(!BuildAction(L"", 0, L""));
  CHECK(!BuildAction(L"Move", 7, L""));
  for (const wchar_t* bad : {L"32768", L"-32769", L"1.5", L"-", L" 1", L"+1"})
    CHECK(!BuildAction(L"Move", 0, bad));
  CHECK(ValidModifier(L"") && ValidModifier(L"a2 A3") &&
        ValidModifier(L"01 FF"));
  for (const wchar_t* bad : {L"0", L"100", L"GG", L"Ctrl", L"A2,A3"})
    CHECK(!ValidModifier(bad));
  CHECK(!ValidTextOption(kModKey, L"Ctrl"));
  CHECK(ValidTextOption(kHotclicks, L"AA custom FE"));
  CHECK(ValidText(L"") && ValidText(L"Unknown_設定_12"));
  CHECK(!ValidText(L"Move\nResize") && !ValidText(L" Move") &&
        !ValidText(L"Move "));
  CHECK(!ValidText(std::wstring(L"Move\0Resize", 11)));
  CHECK(!ValidText(std::wstring(4096, L'x')));
  CHECK(WritePrivateProfileStringW(L"Input", L"Hotclicks", L"AA 04 05 06 FE",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Input", L"ScrollUp", L"CustomWheel_U_9",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Input", L"LMBTB", L"Menu", path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Input", L"TTBActions", L"260",
                                   path.c_str()));
  initial = repository.Read();
  Draft mouse, stale_mouse;
  mouse.Load(initial.values);
  stale_mouse.Load(initial.values);
  CHECK(!mouse.Changes().Any());
  mouse.SetText(0, L"Unknown_設定_-42");
  mouse.SetText(0, L"Nothing");
  CHECK(!mouse.Changes().Any());
  mouse.SetText(0, L"Unknown_設定_-42");
  mouse.SetText(38, L"SideSnap_L_123");
  mouse.SetText(9, L"Resize_0_8");
  mouse.SetText(kModKey, L"");
  mouse.Set(Option::kTitlebarWithoutAlt, 0);
  CHECK(mouse.GetText(9) == L"Resize_0_8");
  mouse.SetText(kModKey, L"A2 A3");
  auto hotclicks = altsnap::RemoveKeyFromList(mouse.GetText(kHotclicks), 5);
  CHECK(hotclicks == L"AA 04 06 FE");
  mouse.SetText(kHotclicks, hotclicks);
  mouse.Set(Option::kTitlebarWithoutAlt, 1);
  mouse.Set(Option::kTitlebarWithAlt, 1);
  mouse.Set(Option::kLongClickMove, 1);
  Request mouse_request{18, Operation::kApply, initial.revision,
                        mouse.Changes()};
  bytes = Encode(mouse_request);
  CHECK(Decode(bytes, decoded));
  CHECK(decoded.patch.text_changed == mouse_request.patch.text_changed);
  CHECK(decoded.patch.text[0] == L"Unknown_設定_-42");
  for (size_t length = 0; length < bytes.size(); ++length)
    CHECK(!Decode(std::span(bytes.data(), length), decoded));
  auto bad_text_mask = bytes;
  bad_text_mask[32 + 4 * kOptionCount + 7] = 128;
  CHECK(!Decode(bad_text_mask, decoded));
  CHECK(repository.Write(initial.revision, mouse.Changes()) == Result::kOk);
  current = repository.Read();
  CHECK(current.values.text[0].raw == L"Unknown_設定_-42");
  CHECK(current.values.text[38].raw == L"SideSnap_L_123");
  CHECK(!current.values.text[31].present);
  CHECK(current.values[Index(Option::kTitlebarWithoutAlt)].raw == L"263");
  GetPrivateProfileStringW(L"Input", L"ScrollUp", L"", value, 64, path.c_str());
  CHECK(std::wstring(value) == L"CustomWheel_U_9");
  GetPrivateProfileStringW(L"Input", L"LMBTB", L"", value, 64, path.c_str());
  CHECK(std::wstring(value) == L"Menu");
  stale_mouse.SetText(0, L"Close");
  CHECK(repository.Write(initial.revision, stale_mouse.Changes()) ==
        Result::kConflict);
  CHECK(repository.WriteIfUnchanged(initial, stale_mouse.Changes()) ==
        Result::kConflict);
  CHECK(stale_mouse.GetText(0) == L"Close");
  mouse.Load(current.values);
  mouse.SetText(1, L"Move\n[Other]");
  CHECK(!mouse.Changes().Valid());
  CHECK(repository.Write(current.revision, mouse.Changes()) ==
        Result::kInvalid);
  CHECK(repository.Read().revision == current.revision);
  // Untouched raw values, including quoted/custom/empty values, survive other
  // edits.
  CHECK(WritePrivateProfileStringW(L"Input", L"MMB", L"\"Custom\"",
                                   path.c_str()));
  CHECK(WritePrivateProfileStringW(L"Input", L"RMB", L"", path.c_str()));
  current = repository.Read();
  mouse.Load(current.values);
  CHECK(current.values.text[1].raw == L"\"Custom\"");
  CHECK(current.values.text[2].present &&
        current.values.text[2].effective.empty());
  mouse.SetText(3, L"Menu");
  CHECK(mouse.Changes().Valid());
  const int mouse_starts = runtime.starts, mouse_stops = runtime.stops;
  runtime.enabled = true;
  runtime.start_ok = true;
  CHECK(ApplyPatch(repository, current.revision, mouse.Changes(), runtime) ==
        Result::kOk);
  CHECK(runtime.starts == mouse_starts + 1 && runtime.stops == mouse_stops + 1);
  current = repository.Read();
  CHECK(current.values.text[1].raw == L"\"Custom\"");
  mouse.Load(current.values);
  mouse.SetText(3, L"Close");
  runtime.enabled = false;
  CHECK(ApplyPatch(repository, current.revision, mouse.Changes(), runtime) ==
        Result::kOk);
  CHECK(!runtime.enabled && runtime.starts == mouse_starts + 1);
  current = repository.Read();
  CHECK(Decode(Encode(current, 555), 555, restored));
  CHECK(restored.values.text[kHotclicks].raw == hotclicks);
  CHECK(restored.values.text[1].raw == L"\"Custom\"");
  // Every text binding uses its own allowlisted key; all contexts can apply
  // together.
  mouse.Load(current.values);
  for (size_t i = 0; i < kBindings.size(); ++i)
    mouse.SetText(i, L"Menu_0_" + std::to_wstring(i));
  CHECK(repository.Write(current.revision, mouse.Changes()) == Result::kOk);
  current = repository.Read();
  for (size_t i = 0; i < kBindings.size(); ++i)
    CHECK(current.values.text[i].raw == L"Menu_0_" + std::to_wstring(i));
  Snapshot maximum;
  maximum.path.assign(32767, L'p');
  for (auto& v : maximum.values) v.raw.assign(4095, L'x');
  for (auto& v : maximum.values.text) {
    v.raw.assign(4095, L'x');
    v.effective = v.raw;
  }
  bytes = Encode(maximum, 777);
  CHECK(bytes.size() <= kMaxReply && Decode(bytes, 777, restored));
  Request maximum_request{1, Operation::kApply};
  maximum_request.patch.text_changed = (uint64_t{1} << kTextCount) - 1;
  for (auto& text : maximum_request.patch.text) text.assign(4095, L'x');
  maximum_request.patch.text[kModKey] = L"A2 A3";
  bytes = Encode(maximum_request);
  CHECK(bytes.size() <= kMaxRequest && Decode(bytes, decoded));
  // Every option can be written and read through its own declared section/key.
  for (size_t i = 0; i < kOptionCount; ++i) {
    const auto option = static_cast<Option>(i);
    CHECK(repository.Write(repository.Read().revision,
                           SingleChange(option, 0)) == Result::kOk);
    CHECK(repository.Read().values[i].effective == 0);
  }
  CHECK(DeleteFileW(path.c_str()));
  CHECK(DeleteFileW(filename));
  CHECK(repository.Read().result == Result::kReadFailed);
  std::puts("Settings protocol and repository tests passed");
}
