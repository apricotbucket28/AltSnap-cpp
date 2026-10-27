// Interface of volume.cc (internal to hooks.dll).
#ifndef ALTSNAP_VOLUME_H_
#define ALTSNAP_VOLUME_H_

#include "hooks/hooks_internal.h"

namespace altsnap::hooks {

short ScaleDeltaAndAccum(short delta, short tar_delta);
void ActionVolume(int delta);

}  // namespace altsnap::hooks

#endif  // ALTSNAP_VOLUME_H_
