#ifndef ALTSNAP_SNAP_H_
#define ALTSNAP_SNAP_H_

#include "hooks_internal.h"

namespace altsnap::hooks {

// Snapping state flags stored per window (see SetRestoreFlag()).
enum SnapFlag : unsigned {
  SNAPPED = 1,
  ROLLED = 2,
  SNLEFT = 1 << 2,
  SNRIGHT = 1 << 3,
  SNTOP = 1 << 4,
  SNBOTTOM = 1 << 5,
  SNMAXH = 1 << 6,
  SNMAXW = 1 << 7,
  SNCLEAR = 1 << 8,  // Cleared at the start of a movement.
  TORESIZE = 1 << 9,
  SNTHENROLLED = 1 << 10,
  SNZONE = 1 << 11,
  SNMIN = 1 << 12,
  SNTOPLEFT = SNTOP | SNLEFT,
  SNTOPRIGHT = SNTOP | SNRIGHT,
  SNBOTTOMLEFT = SNBOTTOM | SNLEFT,
  SNBOTTOMRIGHT = SNBOTTOM | SNRIGHT,
  SNAPPEDSIDE = SNTOPLEFT | SNBOTTOMRIGHT,
};

// True if only the named single side is set (no corner).
[[nodiscard]] constexpr bool PureLeft(unsigned flag) {
  return (flag & SNLEFT) && !(flag & (SNTOP | SNBOTTOM));
}
[[nodiscard]] constexpr bool PureRight(unsigned flag) {
  return (flag & SNRIGHT) && !(flag & (SNTOP | SNBOTTOM));
}
[[nodiscard]] constexpr bool PureTop(unsigned flag) {
  return (flag & SNTOP) && !(flag & (SNLEFT | SNRIGHT));
}
[[nodiscard]] constexpr bool PureBottom(unsigned flag) {
  return (flag & SNBOTTOM) && !(flag & (SNLEFT | SNRIGHT));
}

LONG_PTR ClearBorderlessFlag(HWND hwnd);
void ClearRestoreData(HWND hwnd);
LONG_PTR GetBorderlessFlag(HWND hwnd);
unsigned GetRestoreData(HWND hwnd, int* width, int* height);
unsigned GetRestoreFlag(HWND hwnd);
int IsAnySnapped(HWND hwnd);
void ResetDB();
void SetBorderlessFlag(HWND hwnd, LONG_PTR flag);
void SetRestoreData(HWND hwnd, int width, int height, unsigned restore);
void SetRestoreFlag(HWND hwnd, unsigned flag);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_SNAP_H_
