#include "blacklist.h"

#include <windows.h>

#include "strutil.h"

namespace {

// A lone `*` means "anything", which is stored as an absent value.
std::optional<std::wstring> PatternOrAny(std::wstring_view text) {
  if (text == L"*") return std::nullopt;
  return std::wstring(text);
}

const wchar_t* OrNull(const std::optional<std::wstring>& value) {
  return value ? value->c_str() : nullptr;
}

}  // namespace

Blacklist::Item Blacklist::Item::Parse(std::wstring_view text) {
  std::wstring_view exename, title, class_name;
  bool has_exename = false, has_title = false, has_class = false;

  const size_t bar = text.find(L'|');
  if (bar != std::wstring_view::npos) {
    // [exename:]title|class. A ':' after the '|' belongs to the class, since
    // file names cannot contain '|'.
    class_name = text.substr(bar + 1);
    has_class = true;
    const std::wstring_view head = text.substr(0, bar);
    const size_t colon = head.find(L':');
    if (colon != std::wstring_view::npos) {
      exename = head.substr(0, colon);
      has_exename = true;
      title = head.substr(colon + 1);
    } else {
      title = head;
    }
    has_title = true;
  } else if (const size_t colon = text.find(L':');
             colon != std::wstring_view::npos) {
    exename = text.substr(0, colon);  // exename:title
    has_exename = true;
    title = text.substr(colon + 1);
    has_title = true;
  } else {
    exename = text;  // exename only
    has_exename = true;
  }

  Item item;
  if (has_exename) item.exename = PatternOrAny(exename);
  if (has_title) item.title = PatternOrAny(title);
  if (has_class) item.class_name = PatternOrAny(class_name);
  return item;
}

bool Blacklist::Item::Matches(const wchar_t* exe, const wchar_t* window_title,
                              const wchar_t* window_class) const {
  return !lstrcmp_star(window_class, OrNull(class_name)) &&
         !lstrcmp_star(window_title, OrNull(title)) &&
         (!exename || !lstrcmpi(exe, exename->c_str()));
}

Blacklist::Blacklist(std::wstring_view spec) {
  size_t pos = 0;
  while (pos <= spec.size()) {
    const size_t comma = spec.find(L',', pos);
    const std::wstring_view text = spec.substr(
        pos, comma == std::wstring_view::npos ? comma : comma - pos);
    if (!text.empty()) items_.push_back(Item::Parse(text));
    if (comma == std::wstring_view::npos) break;
    pos = comma + 1;
    while (pos < spec.size() && spec[pos] == L' ') ++pos;  // "a, b"
  }
}

bool Blacklist::Contains(const wchar_t* exename, const wchar_t* title,
                         const wchar_t* class_name) const {
  if (items_.empty()) return false;

  // A first item that matches anything turns the list into a whitelist.
  const bool whitelist = items_.front().MatchesAnything();
  for (size_t i = whitelist ? 1 : 0; i < items_.size(); ++i) {
    if (items_[i].Matches(exename, title, class_name)) return !whitelist;
  }
  return whitelist;
}

Blacklists Blacklists::Read(const IniSection& section) {
  Blacklists lists;
  const struct {
    const char* key;
    Blacklist Blacklists::*list;
  } kLists[] = {
      {"Processes", &Blacklists::Processes},
      {"Windows", &Blacklists::Windows},
      {"Snaplist", &Blacklists::Snaplist},
      {"MDIs", &Blacklists::MDIs},
      {"Pause", &Blacklists::Pause},
      {"MMBLower", &Blacklists::MMBLower},
      {"Scroll", &Blacklists::Scroll},
      {"IScroll", &Blacklists::IScroll},
      {"AResize", &Blacklists::AResize},
      {"SSizeMove", &Blacklists::SSizeMove},
      {"NCHittest", &Blacklists::NCHittest},
      {"Bottommost", &Blacklists::Bottommost},
  };
  for (const auto& [key, list] : kLists) {
    const wchar_t* spec = section.GetString(key, nullptr);
    if (spec && *spec) lists.*list = Blacklist(spec);
  }
  return lists;
}
