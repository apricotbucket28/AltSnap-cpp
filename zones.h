#ifndef ALTSNAP_ZONES_H_
#define ALTSNAP_ZONES_H_

#include "hooks_internal.h"
#include "ini_section.h"

namespace altsnap::hooks {

enum { MAX_ZONES = 2048, MAX_LAYOUTS = 10 };
enum { INVALID_ZONE_IDX = 0xffffffffU };
enum { ZONES_PREV_HIDE = 0, ZONES_PREV_SHOW = 1 };

void ActionToggleSnapToZoneMode();
int GetBestLayoutFromMonitors();
[[nodiscard]] DWORD GetLayoutRez(int laynum);
void MoveSnapToZone(POINT pt, int* posx, int* posy, int* width, int* height);
void MoveWindowToTouchingZone(HWND hwnd, UCHAR direction, UCHAR extend);
void ReadGrids(const IniSection& section);
void ReadZones(const IniSection& section);
void SetLayoutNumber(WPARAM number);
void ShowSnapLayoutPreview(unsigned char yep);
void SnapLayoutPreviewCreateDestroy(const IniSection* section);
extern std::array<std::vector<RECT>, MAX_LAYOUTS> Zones;
void freezones();

}  // namespace altsnap::hooks

#endif  // ALTSNAP_ZONES_H_
