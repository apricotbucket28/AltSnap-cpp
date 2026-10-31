#ifndef ALTSNAP_ZONES_H_
#define ALTSNAP_ZONES_H_

#include "common/ini_section.h"
#include "hooks/hooks_internal.h"

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
[[nodiscard]] unsigned CurrentLayout();
void InitCurrentLayout(unsigned layout);
void ShowSnapLayoutPreview(unsigned char yep);
bool SnapLayoutPreviewCreateDestroy(const IniSection* section);
extern std::array<std::vector<RECT>, MAX_LAYOUTS> Zones;
// Borrowed until the layouts are modified or freed. Invalid indices are empty.
[[nodiscard]] std::span<RECT> GetZones(size_t layout);
void FreeZones();

}  // namespace altsnap::hooks

#endif  // ALTSNAP_ZONES_H_
