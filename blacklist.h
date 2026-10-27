// Window/process blacklists (the [Blacklist] section of the ini file).

#ifndef ALTSNAP_BLACKLIST_H_
#define ALTSNAP_BLACKLIST_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ini_section.h"

namespace altsnap {

// A list of window patterns parsed from a comma-separated ini value such as
//   "notepad.exe, *|Chrome_WidgetWin_1, calc.exe:Calculator|CalcFrame"
//
// Each item is one of
//   exename.exe              any window of that program
//   title|class              windows with that title and class
//   exename.exe:title|class  both of the above
//   exename.exe:title        same as above with any class
// and a `*` (or an omitted part) matches anything. Titles and classes may also
// start or end with `*` (see lstrcmp_star()).
//
// If the first item is `*|*` the list is a whitelist: every window that is not
// listed counts as blacklisted.
class Blacklist {
 public:
  Blacklist() = default;

  // Upstream: readblacklist
  explicit Blacklist(std::wstring_view spec);

  [[nodiscard]] bool empty() const { return items_.empty(); }

  // Whether a window with these names is blacklisted by this list.
  // Upstream: blacklisted_from_names
  [[nodiscard]] bool Contains(const wchar_t* exename, const wchar_t* title,
                              const wchar_t* class_name) const;

 private:
  struct Item {
    std::optional<std::wstring> exename;     // nullopt: any program
    std::optional<std::wstring> title;       // nullopt: any title
    std::optional<std::wstring> class_name;  // nullopt: any class

    [[nodiscard]] bool MatchesAnything() const {
      return !exename && !title && !class_name;
    }
    [[nodiscard]] bool Matches(const wchar_t* exe, const wchar_t* window_title,
                               const wchar_t* window_class) const;
    [[nodiscard]] static Item Parse(std::wstring_view text);
  };

  std::vector<Item> items_;
};

// All the lists of the [Blacklist] section.
struct Blacklists {
  Blacklist Processes;   // Programs ignored entirely
  Blacklist Windows;     // Windows ignored entirely
  Blacklist Snaplist;    // Windows other windows do not snap to
  Blacklist MDIs;        // Windows that are not MDI parents
  Blacklist Pause;       // Windows that pause AltSnap while focused
  Blacklist MMBLower;    // Windows excluded from middle-click lowering
  Blacklist Scroll;      // Windows excluded from scroll actions
  Blacklist IScroll;     // Windows excluded from inactive scrolling
  Blacklist AResize;     // Windows always resizable
  Blacklist SSizeMove;   // Windows excluded from WM_*SIZEMOVE handling
  Blacklist NCHittest;   // Windows excluded from non-client hit testing
  Blacklist Bottommost;  // Windows kept at the bottom

  [[nodiscard]] static Blacklists Read(const IniSection& section);
};

}  // namespace altsnap

#endif  // ALTSNAP_BLACKLIST_H_
