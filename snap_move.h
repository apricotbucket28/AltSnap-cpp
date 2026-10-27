// Interface of snap_move.cc (internal to hooks.dll).
#ifndef ALTSNAP_SNAP_MOVE_H_
#define ALTSNAP_SNAP_MOVE_H_

#include "hooks_internal.h"

namespace altsnap::hooks {

void MoveSnap(int* _posx, int* _posy, int wndwidth, int wndheight, UCHAR pth);
void ResizeSnap(int* posx, int* posy, int* wndwidth, int* wndheight, UCHAR pthx,
                UCHAR pthy);
int AeroMoveSnap(POINT pt, int* posx, int* posy, int* wndwidth, int* wndheight);
void AeroResizeSnap(POINT pt, int* posx, int* posy, int* wndwidth,
                    int* wndheight);
void SetEdgeToClosestSide(const RECT* wnd, const POINT pt);
void SetEdgeAndOffset(const RECT* wnd, const POINT pt);
void SnapToCorner(HWND hwnd, resizexy_t resize, UCHAR flags);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_SNAP_MOVE_H_
