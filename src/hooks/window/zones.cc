/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * (C) Raymond GILLIBERT                                                 *
 * Functions to handle Snap Layouts under AltSnap                        *
 * General Public License Version 3 or later (Free Software Foundation)  *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "hooks/window/zones.h"

#include "common/ini_section.h"
#include "common/text_format.h"
#include "hooks/hooks_internal.h"
#include "hooks/window/snap.h"

namespace altsnap::hooks {

std::span<RECT> GetZones(size_t layout) {
  if (layout >= Zones.size()) return {};
  return Zones[layout];
}

std::array<std::vector<RECT>, MAX_LAYOUTS> Zones;
static DWORD Grids[MAX_LAYOUTS] = {};

// Upstream: freezones
void FreeZones() {
  for (auto& layout : Zones) layout = {};  // Also releases the memory.
}

// Reads the rectangle of zone `index` of a layout from the ini file.
// Upstream: ReadRectFromini
static std::optional<RECT> ReadZoneRect(const IniSection& section,
                                        unsigned layout, unsigned index) {
  if (index > MAX_ZONES) return std::nullopt;
  const wchar_t* text =
      section.GetString(ZoneKey(layout, index).c_str(), nullptr);
  if (!text || !text[0]) return std::nullopt;
  return ParseRect(text);
}
static void ReadZonesFromLayout(const IniSection& section, unsigned laynum) {
  Zones[laynum].clear();
  while (auto zone = ReadZoneRect(section, laynum, Zones[laynum].size())) {
    Zones[laynum].push_back(*zone);
  }
}
// Load all zones from ini file
void ReadZones(const IniSection& section) {
  for (size_t i = 0; i < std::size(Zones); i++) ReadZonesFromLayout(section, i);
}

// Generate a grid if asked
static void GenerateGridZones(unsigned layout, unsigned short Nx,
                              unsigned short Ny) {
  // Enumerate monitors
  Session().monitors.clear();
  EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsProc, 0);
  Zones[layout].clear();
  Zones[layout].reserve(Session().monitors.size() * Nx * Ny);

  // Loop on all monitors
  for (auto& monitor : Session().monitors) {
    const RECT* mon = &monitor;
    for (size_t i = 0; i < Nx; i++) {    // Horizontal
      for (size_t j = 0; j < Ny; j++) {  // Vertical
        RECT& elem = Zones[layout].emplace_back();
        elem.left = mon->left + ((i) * (mon->right - mon->left)) / Nx;
        elem.top = mon->top + ((j) * (mon->bottom - mon->top)) / Ny;
        elem.right = mon->left + ((i + 1) * (mon->right - mon->left)) / Nx;
        elem.bottom = mon->top + ((j + 1) * (mon->bottom - mon->top)) / Ny;
      }
    }
  }
}
void ReadGrids(const IniSection& section) {
  for (size_t i = 0; i < std::size(Grids); i++) {
    // The first layout uses GridNx/GridNy, the next ones GridNx1/GridNy1, ...
    const std::string suffix = i == 0 ? "" : std::to_string(i);
    Grids[i] = 0;
    const unsigned short GridNx =
        section.GetInt(("GridNx" + suffix).c_str(), 0);
    const unsigned short GridNy =
        section.GetInt(("GridNy" + suffix).c_str(), 0);
    if (GridNx && GridNy && GridNx * GridNy <= MAX_ZONES) {
      Grids[i] = GridNx | GridNy << 16;  // Store grid dimentions
      GenerateGridZones(i, GridNx, GridNy);
    }
  }
}
// Recalculate the zones from the Grid info.
// Needed in case resolution changes.
static void RecalculateZonesFromGrids() {
  for (size_t i = 0; i < std::size(Grids); i++) {
    unsigned short GridNx = LOWORD(Grids[i]);
    unsigned short GridNy = HIWORD(Grids[i]);
    if (GridNx && GridNy) GenerateGridZones(i, GridNx, GridNy);
  }
}

[[nodiscard]] static unsigned long PtDist2(POINT pt, POINT Zpt) {
  long dx = (pt.x - Zpt.x) >> 1;
  long dy = (pt.y - Zpt.y) >> 1;

  return dx * dx + dy * dy;
}

[[nodiscard]] static unsigned long ClacPtRectDist(const POINT pt,
                                                  const RECT* zone) {
  POINT Zpt = {(zone->left + zone->right) / 2, (zone->top + zone->bottom) / 2};
  return PtDist2(pt, Zpt);
}

static unsigned GetNearestZoneDist(POINT pt, unsigned long* dist_) {
  RECT* const lZones = Zones[CurrentLayout()].data();
  size_t numzones = Zones[CurrentLayout()].size();
  if (!lZones) return 0;
  unsigned long dist = 0xffffffff;
  unsigned idx = 0;
  for (size_t i = 0; i < numzones; i++) {
    unsigned long dst = ClacPtRectDist(pt, &lZones[i]);
    if (dst < dist) {
      dist = dst;
      idx = i;
    }
  }
  *dist_ = dist;
  return idx;
}

static unsigned GetZoneNearestFromPoint(POINT pt, RECT* urc, int extend) {
  RECT* const lZones = Zones[CurrentLayout()].data();
  size_t numzones = Zones[CurrentLayout()].size();
  if (!lZones) return 0;
  unsigned i = 0, ret = 0;
  SetRectEmpty(urc);
  int iz = conf.InterZone;
  iz = (iz * iz) << 1;
  unsigned long mindist = 0;

  i = GetNearestZoneDist(pt, &mindist);
  ret = mindist != 0xffffffff;
  if (!ret) return 0;
  CopyRect(urc, &lZones[i]);
  mindist += iz;  // Add iz tolerance.
  for (i = 0; i < numzones; i++) {
    BOOL inrect = mindist > ClacPtRectDist(pt, &lZones[i]);
    if ((Session().input.ctrl || extend) && !inrect)
      inrect = mindist > ClacPtRectDist(extend ? Session().input.shiftpt
                                               : Session().input.ctrlpt,
                                        &lZones[i]);

    if (inrect) {
      UnionRect(urc, urc, &lZones[i]);
      ret++;
    }
  }
  return ret;
}

static unsigned GetZoneContainingPoint(POINT pt, RECT* urc, int extend) {
  RECT* const lZones = Zones[CurrentLayout()].data();
  size_t numzones = Zones[CurrentLayout()].size();
  if (!lZones) return 0;
  unsigned i = 0, ret = 0;
  SetRectEmpty(urc);
  int iz = conf.InterZone;
  for (i = 0; i < numzones; i++) {
    if (iz) InflateRect(&lZones[i], iz, iz);

    int inrect = 0;
    inrect = PtInRect(&lZones[i], pt);
    if ((Session().input.ctrl || extend) && !inrect)
      inrect = PtInRect(&lZones[i], extend ? Session().input.shiftpt
                                           : Session().input.ctrlpt);

    if (iz) InflateRect(&lZones[i], -iz, -iz);
    if (inrect) {
      UnionRect(urc, urc, &lZones[i]);
      ret++;
    }
  }
  return ret;
}
static unsigned GetZoneFromPoint(POINT pt, RECT* urc, int extend) {
  switch (conf.ZSnapMode) {
    case 0:
      return GetZoneContainingPoint(pt, urc, extend);
    case 1:
      return GetZoneNearestFromPoint(pt, urc, extend);
  }
  // Invalid Snap mode!
  return 0;
}

void ActionToggleSnapToZoneMode() {
  if (conf.UseZones & 1 && Session().input.action.ac == AC_MOVE) {
    Session().input.usezones = !Session().input.usezones;
    MouseMove(Session().input.prevpt);
  }
}
void MoveSnapToZone(POINT pt, int* posx, int* posy, int* width, int* height) {
  if (!(conf.UseZones & 1) || Session().input.mdiclient ||
      !Session().input.resizable)  // Zones disabled
    return;

  POINT& oldpt = Session().zone_point;
  if (Session().input.Speed > conf.AeroMaxSpeed)
    pt = oldpt;  // do not move the window if speed is too high.
  else
    oldpt = pt;

  if (conf.UseZones & 8 &&
      Session().input.snap != conf.AutoSnap)  // Zones toggled by other click
    return;

  if (!Session().input.usezones) {
    if (!Session().input.forcelayoutdisplay)
      ShowSnapLayoutPreview(ZONES_PREV_HIDE);
    return;
  }

  ShowSnapLayoutPreview(ZONES_PREV_SHOW);
  RECT rc, bd;
  unsigned ret = GetZoneFromPoint(pt, &rc, conf.UseZones & 4);
  if (!ret) return;  // Outside of a rect

  Session().last_window.end = 0;
  FixDWMRect(Session().input.hwnd, &bd);
  InflateRectBorder(&rc, &bd);

  SetRestoreData(Session().input.hwnd, Session().input.origin.width,
                 Session().input.origin.height, SNAPPED | SNZONE);
  *posx = rc.left;
  *posy = rc.top;
  *width = rc.right - rc.left;
  *height = rc.bottom - rc.top;
}

[[nodiscard]] static int IsPtInCone(POINT pt, POINT op, UCHAR direction) {
  const long dx = pt.x - op.x, dy = pt.y - op.y;
  switch (direction) {
    case 0:  // LEFT
      return dx <= 0 && (dy <= -dx && dy > dx);

    case 1:  // TOP
      return dy <= 0 && (dx <= -dy && dx > dy);

    case 2:  // RIGHT
      return dx > 0 && (dy <= dx && dy > -dx);

    case 3:  // BOTTOM
      return dy > 0 && (dx <= dy && dx > -dy);
  }
  return 0;
}
static unsigned GetNearestZoneFromPointInDirection(const RECT* rc, RECT* out,
                                                   UCHAR direction) {
  unsigned idx = INVALID_ZONE_IDX;
  RECT* const lZones = Zones[CurrentLayout()].data();
  size_t nZones = Zones[CurrentLayout()].size();
  if (!lZones) return 0;

  POINT opt = {(rc->left + rc->right) / 2, (rc->top + rc->bottom) / 2};

  unsigned long dist = 0xffffffff;
  for (size_t i = 0; i < nZones; i++) {
    const RECT* zrc = &lZones[i];
    const POINT pt = {(zrc->left + zrc->right) / 2,
                      (zrc->top + zrc->bottom) / 2};
    unsigned long dst = PtDist2(pt, opt);
    if (!EqualRect(rc, zrc) && dst < dist && IsPtInCone(pt, opt, direction)) {
      dist = dst;
      idx = i;
    }
  }

  if (idx != INVALID_ZONE_IDX) {
    CopyRect(out, &lZones[idx]);
    return 1;
  }

  return 0;
}

//
// static void SnapToRect(HWND hwnd, RECT *fr)
//{
//    RECT bd;
//    LastWin.end = 0;
//    FixDWMRect(hwnd, &bd);
//    InflateRectBorder(fr, &bd);
//
//    SetRestoreData(hwnd, state.origin.width, state.origin.height,
//    SNAPPED|SNZONE); MoveWindowAsync(hwnd, fr->left, fr->top,
//    CLAMPW(fr->right-fr->left), CLAMPH(fr->bottom-fr->top));
//}

void MoveWindowToTouchingZone(HWND hwnd, UCHAR direction, UCHAR extend) {
  if (!(conf.UseZones & 1) || Session().input.mdiclient ||
      !Session().input.resizable)  // Zones disabled
    return;
  SetOriginFromRestoreData(hwnd, k_action_move);
  // 1st get current window position.
  RECT rc;
  GetWindowRectL(hwnd, &rc);
  POINT pt = {0, 0};

  int offset = abs(conf.InterZone) + 16;

  if (direction == 0) {       // LEFT
    pt.x = rc.left - offset;  // Mid Left segment
    pt.y = (rc.top + rc.bottom) / 2;
  } else if (direction == 1) {        // TOP
    pt.x = (rc.right + rc.left) / 2;  // Mid Top segment
    pt.y = rc.top - offset;
  } else if (direction == 2) {  // RIGHT
    pt.x = rc.right + offset;   // Mid Right segment
    pt.y = (rc.top + rc.bottom) / 2;
  } else if (direction == 3) {        // BOTTOM
    pt.x = (rc.right + rc.left) / 2;  // Mid Bottom segment
    pt.y = rc.bottom + offset;
  } else {
    return;  // Unknown direction.
  }
  if (IsZoomed(hwnd) && extend) return;

  // Clamp point inside monitor
  MONITORINFO mi;
  GetMonitorInfoFromWin(hwnd, &mi);
  ClampPointInRect(&mi.rcWork, &pt);

  RECT zrc;
  unsigned ret = 0;
  switch (conf.ZSnapMode) {
    case 0:
      ret = GetZoneContainingPoint(pt, &zrc, 0);
      break;
    case 1:
      ret = GetNearestZoneFromPointInDirection(&rc, &zrc, direction);
      break;
  }
  if (!ret) return;  // Outside of a rect

  RECT fr;                      // final rect...
  if (extend) {                 // extend the zone instead.
    UnionRect(&fr, &rc, &zrc);  // Union of original and new rect
  } else {
    CopyRect(&fr, &zrc);
  }
  Session().last_window.end = 0;
  RECT bd;
  FixDWMRect(hwnd, &bd);
  InflateRectBorder(&fr, &bd);

  SetRestoreData(hwnd, Session().input.origin.width,
                 Session().input.origin.height, SNAPPED | SNZONE);
  MoveWindowAsync(hwnd, fr.left, fr.top, ClampWidth(fr.right - fr.left),
                  ClampHeight(fr.bottom - fr.top));
}
// Sets urc as the union of all the rcs array or RECT, of length N
static void UnionMultiRect(RECT* urc, const RECT* rcs, unsigned N) {
  CopyRect(urc, &rcs[0]);
  while (--N) {
    rcs++;
    urc->left = std::min(urc->left, rcs->left);
    urc->top = std::min(urc->top, rcs->top);
    urc->right = std::max(urc->right, rcs->right);
    urc->bottom = std::max(urc->bottom, rcs->bottom);
  }
}
[[nodiscard]] DWORD GetLayoutRez(int laynum) {
  laynum = std::clamp<int>(laynum, 0, std::size(Zones) - 1);
  if (conf.UseZones & 2) {
    // In Grid mode we return the grid dimentions!
    return Grids[laynum];
  }
  size_t nz = Zones[laynum].size();
  const RECT* zone = Zones[laynum].data();
  if (!zone || !nz) return 0;
  RECT urc;
  UnionMultiRect(&urc, zone, nz);
  return (urc.right - urc.left) | (urc.bottom - urc.top) << 16;
}
// Calculate the dimentions of union rectangle that contains all
// the work area of every monitors.
static DWORD GetFullMonitorsRez() {
  // Enumerate monitors
  EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsProc, 0);
  if (Session().monitors.empty()) return 0;  // ERROR! do nothing
  RECT urc;
  UnionMultiRect(&urc, Session().monitors.data(),
                 static_cast<unsigned>(Session().monitors.size()));

  // Return dimentions width:height
  return (urc.right - urc.left) | (urc.bottom - urc.top) << 16;
}

// Return a layout number if its dimentions exactly fit
// the display monitor.
int GetBestLayoutFromMonitors() {
  if (!(conf.UseZones & 1)) return -1;
  if (conf.UseZones & 2) {
    // Grid mode! Do not change Grid, just recalculate them all...
    RecalculateZonesFromGrids();
    return -1;  // DONE!
  }
  DWORD curRZ = GetLayoutRez(CurrentLayout());

  // If the current rez is 0:0 we should not change it.
  // Because it can be used as a dummy mode, plus a user
  // might have selected it in advance to configure it
  // later for the new display settings, so selecting
  // an other one would bother him.
  if (!curRZ) return -1;

  // Check if the current Layout is good for the current monitor
  DWORD monRZ = 0;
  if (Session().monitors.size() <= 1) {
    RECT mrc;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &mrc, 0);
    monRZ = (mrc.right - mrc.left) | (mrc.bottom - mrc.top) << 16;
    // In Single monitor mode we use SPI_GETWORKAREA.
    // Because EnumDisplayMonitor often gives bad values.
    // I am unsure on how to properly handle multiple monitors...
    if (monRZ == curRZ) {
      return -1;  // Current layout is good!
    }
  } else if ((monRZ = GetFullMonitorsRez()) == curRZ) {
    // GetFullMonitorsRez gives us the union of all monitors.rcWork
    return -1;  // Current layout is good!
  }

  for (size_t i = 0; i < std::size(Zones); i++) {
    DWORD rez = GetLayoutRez(i);
    if (rez == monRZ) return i;
  }
  // No perfect match found!
  return -1;
}

static HWND g_zphwnd = nullptr;
static COLORREF g_ZPrevBDCol = RGB(0, 0, 0);
static LRESULT CALLBACK SnapLayoutWinProc(HWND hwnd, UINT msg, WPARAM wp,
                                          LPARAM lp) {
  static HPEN pen = nullptr;

  switch (msg) {
    case WM_CREATE: {
      pen = CreatePen(PS_INSIDEFRAME, 4, g_ZPrevBDCol);
    } break;

    case WM_PAINT: {
      // Create Font
      //        struct NEWNONCLIENTMETRICSAW ncm;
      //        int dpi = GetDpiForWindow(hwnd);
      //        GetNonClientMetricsDpi(&ncm, dpi);
      //        ncm.lfCaptionFont.lfHeight = 64;
      //        HFONT font = CreateFontIndirect(&ncm.lfCaptionFont);

      PAINTSTRUCT ps;
      POINT opt = {0, 0};
      ScreenToClient(hwnd, &opt);
      BeginPaint(hwnd, &ps);
      HPEN oldpen = static_cast<HPEN>(SelectObject(ps.hdc, pen));
      //        HFONT oldfont = (HFONT)SelectObject(ps.hdc, font);
      // PaintDesktop(ps.hdc);
      auto oldBrush = static_cast<HBRUSH>(
          SelectObject(ps.hdc, GetStockObject(HOLLOW_BRUSH)));
      for (auto& i : Zones[CurrentLayout()]) {
        RECT* rc = &i;
        if (!AreRectsTouchingT(rc, &ps.rcPaint, 0)) {
          //                wchar_t buf[16]; SIZE sz;
          //                const wchar_t *num = Int2lStr(buf, i);
          //                int txtlen = lstrlen(num);
          //                GetTextExtentPoint32(ps.hdc, num, txtlen, &sz);
          //                int txtX = (rc->left + rc->right - sz.cx) / 2 +
          //                opt.x; int txtY = (rc->top + rc->bottom - sz.cy) / 2
          //                + opt.y; TextOut(ps.hdc, txtX, txtY, num, txtlen);
          Rectangle(ps.hdc, rc->left + opt.x, rc->top + opt.y,
                    rc->right + opt.x, rc->bottom + opt.y);
        }
      }
      //        SelectObject(ps.hdc, oldfont);  // Restore old font
      SelectObject(ps.hdc, oldBrush);  // Restore old pen
      SelectObject(ps.hdc, oldpen);    // Restore old brush

      EndPaint(hwnd, &ps);

      //        DeleteObject(font);
    }
      return 0;

    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_LBUTTONUP:
      ShowWindow(hwnd, SW_HIDE);
      break;

    case WM_DESTROY: {
      DeleteObject(pen);
    } break;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

void ShowSnapLayoutPreview(unsigned char yep) {
  if (g_zphwnd) {  // Window was created
    if (!yep) {
      ShowWindow(g_zphwnd, SW_HIDE);
    } else if (!IsWindowVisible(g_zphwnd)) {
      ShowWindow(g_zphwnd, SW_SHOW);
      SetWindowLevel(g_zphwnd,
                     Session().input.hwnd);  // Behind currently moving hwn
    }
  }
}

static void ZonesPrevResetRegion() {
  if (conf.ZonesPrevwOpacity != 0) return;
  POINT opt = {0, 0};
  RECT wrc;
  GetWindowRect(g_zphwnd, &wrc);
  ScreenToClient(g_zphwnd, &opt);
  HRGN hregion = CreateRectRgn(0, 0, 0, 0);
  for (auto& i : Zones[CurrentLayout()]) {
    RECT rc;
    CopyRect(&rc, &i);
    OffsetRect(&rc, opt.x, opt.y);
    HRGN tmpr =
        CreateRectRgn(rc.left, rc.top, rc.right, rc.top + 4);  // top  ^^^^
    CombineRgn(hregion, hregion, tmpr, RGN_OR);
    DeleteObject(tmpr);
    tmpr = CreateRectRgn(rc.left, rc.bottom - 4, rc.right,
                         rc.bottom);  //     ____ bottom
    CombineRgn(hregion, hregion, tmpr, RGN_OR);
    DeleteObject(tmpr);
    tmpr =
        CreateRectRgn(rc.left, rc.top, rc.left + 4, rc.bottom);  //   left  |...
    CombineRgn(hregion, hregion, tmpr, RGN_OR);
    DeleteObject(tmpr);
    tmpr = CreateRectRgn(rc.right - 4, rc.top, rc.right,
                         rc.bottom);  //       ...| right
    CombineRgn(hregion, hregion, tmpr, RGN_OR);
    DeleteObject(tmpr);
  }
  SetWindowRgn(g_zphwnd, hregion, FALSE);
}

bool SnapLayoutPreviewCreateDestroy(const IniSection* section) {
  if (!conf.ShowZonesPrevw) return true;
  if (section) {
    const COLORREF bgcol = ReadColor(*section, "ZonesPrevwBGCol", L"FF FF FF",
                                     ColorChannels::kRgb);
    g_ZPrevBDCol = ReadColor(*section, "ZonesPrevwBDCol", L"00 00 00",
                             ColorChannels::kRgb);
    const UCHAR opacity = conf.ZonesPrevwOpacity;
    const UCHAR usetrans = opacity != 0 && opacity != 255;
    const WNDPROC wproc = opacity == 0 ? DefWindowProc : SnapLayoutWinProc;
    const HBRUSH wbrush =
        opacity == 0 ? CreateSolidBrush(g_ZPrevBDCol) : CreateSolidBrush(bgcol);
    const WNDCLASS wnd = {
        0,       wproc,   0,      0,       hinstDLL,
        nullptr, nullptr, wbrush, nullptr, APP_NAME L"-ZonesPreview"};
    if (!wbrush || !RegisterClass(&wnd)) {
      if (wbrush) DeleteObject(wbrush);
      return false;
    }

    int left = 0, top = 0, width = 0, height = 0;
    if (GetSystemMetrics(SM_CMONITORS) >= 1) {
      left = GetSystemMetrics(SM_XVIRTUALSCREEN);
      top = GetSystemMetrics(SM_YVIRTUALSCREEN);
      width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
      height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    } else {  // NT4...
      width = GetSystemMetrics(SM_CXFULLSCREEN) + 256;
      height = GetSystemMetrics(SM_CYFULLSCREEN) + 256;
    }
    g_zphwnd = CreateWindowEx(
        usetrans ? WS_EX_TOOLWINDOW | WS_EX_LAYERED : WS_EX_TOOLWINDOW,
        wnd.lpszClassName, nullptr, WS_POPUP, left, top, width, height,
        g_mainhwnd, nullptr, hinstDLL, nullptr);
    if (!g_zphwnd) return false;
    SetWindowLevel(g_zphwnd, Session().input.hwnd);  // Behind
    if (usetrans &&
        !SetLayeredWindowAttributes(g_zphwnd, 0, opacity, LWA_ALPHA))
      return false;

    // Set Window Region to only show outlines if needed...
    ZonesPrevResetRegion();

  } else {
    if (g_zphwnd) DestroyWindow(g_zphwnd);
    g_zphwnd = nullptr;
    UnregisterClass(APP_NAME L"-ZonesPreview", hinstDLL);
  }
  return true;
}

// The active zone layout. The ini file only provides the initial value.
static unsigned g_layout_number = 0;
unsigned CurrentLayout() {
  return g_layout_number;
}
void InitCurrentLayout(unsigned layout) {
  g_layout_number = layout;
}

void SetLayoutNumber(WPARAM number) {
  int was_visible = IsWindowVisible(g_zphwnd);
  ShowSnapLayoutPreview(0);
  g_layout_number = static_cast<unsigned>(std::min<WPARAM>(number, 9));
  ZonesPrevResetRegion();  // re-calculate
  ShowSnapLayoutPreview(was_visible);

  if (!was_visible && conf.ShowZonesOnChange) {
    Session().input.forcelayoutdisplay = 1;
    ShowSnapLayoutPreview(1);
    UINT id =
        SetTimer(g_mainhwnd, HIDELAYOUT_TIMER, conf.ShowZonesOnChange * 100,
                 static_cast<TIMERPROC>(TimerWindowProc));
    Session().input.forcelayoutdisplay = !!id;
  }
}

}  // namespace altsnap::hooks
