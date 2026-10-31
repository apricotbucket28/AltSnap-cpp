#ifndef ALTSNAP_HOOKS_WORKER_COMMAND_H_
#define ALTSNAP_HOOKS_WORKER_COMMAND_H_

#include <windows.h>

#include <cstdint>
#include <variant>

namespace altsnap::hooks {

enum button {
  BT_NONE = 0,
  BT_LMB = 0x02,
  BT_RMB = 0x03,
  BT_MMB = 0x04,
  BT_MB4 = 0x05,
  BT_MB5 = 0x06,
  BT_MB6 = 0x07,
  BT_MB7 = 0x08,
  BT_MB8 = 0x09,
  BT_MB9 = 0x0A,
  BT_MB10 = 0x0B,
  BT_MB11 = 0x0C,
  BT_MB12 = 0x0D,
  BT_MB13 = 0x0E,
  BT_MB14 = 0x0F,
  BT_MB15 = 0x10,
  BT_MB16 = 0x11,
  BT_MB17 = 0x12,
  BT_MB18 = 0x13,
  BT_MB19 = 0x14,
  BT_MB20 = 0x15,
  BT_WHEELD = 0x16,
  BT_WHEELU = 0x17,
  BT_HWHEELD = 0x18,
  BT_HWHEELU = 0x19
};
enum resizeX { RZ_XNONE = 0, RZ_LEFT = 1, RZ_RIGHT = 2, RZ_XCENTER = 3 };
enum resizeY { RZ_YNONE = 0, RZ_TOP = 1, RZ_BOTTOM = 2, RZ_YCENTER = 3 };
enum buttonstate { STATE_NONE, STATE_DOWN, STATE_UP };

enum { BT_PROBE = (1 << 16) };

struct resizeXY {
  BYTE x;
  BYTE y;
};
using resizexy_t = resizeXY;

struct windowRR {
  HWND hwnd;
  int x;
  int y;
  int width;
  int height;
  UINT odpi;
  resizexy_t resize;
  UCHAR end;
  UCHAR start;
  UCHAR moveonly;
  UCHAR maximize;
  UCHAR snap;
  // UCHAR resizing_now;
};
using windowRR_t = windowRR;

// Closed, owning command vocabulary. No caller-owned pointers or closures cross
// the queue. HWNDs identify foreign windows and are revalidated on execution.
struct MouseMoveCommand {
  POINT point{};
  uint64_t gesture = 0;
};
struct FinishMovementCommand {
  uint64_t gesture = 0;
};
struct MoveWindowCommand {
  windowRR request{};
};
struct ClickCommand {
  button value = BT_NONE;
};
struct AltCtrlCommand {};
struct MoveResizeComboCommand {
  uint64_t gesture = 0;
};
struct KillWindowCommand {
  HWND window = nullptr;
  bool shift = false;
};
using WorkerCommand =
    std::variant<MouseMoveCommand, FinishMovementCommand, MoveWindowCommand,
                 ClickCommand, AltCtrlCommand, MoveResizeComboCommand,
                 KillWindowCommand>;

inline bool IsMouseMove(const WorkerCommand& command) {
  return std::holds_alternative<MouseMoveCommand>(command);
}
inline bool CanCoalesce(const WorkerCommand& previous,
                        const WorkerCommand& next) {
  const auto* left = std::get_if<MouseMoveCommand>(&previous);
  const auto* right = std::get_if<MouseMoveCommand>(&next);
  return left && right && left->gesture == right->gesture;
}
inline bool IsDroppable(const WorkerCommand& command) {
  return std::holds_alternative<MoveWindowCommand>(command);
}

}  // namespace altsnap::hooks

#endif  // ALTSNAP_HOOKS_WORKER_COMMAND_H_
