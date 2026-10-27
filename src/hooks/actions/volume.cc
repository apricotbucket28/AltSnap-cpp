// volume (split out of hooks.cc).

#include "hooks/actions/volume.h"

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "common/ini_section.h"
#include "hooks/blacklist.h"
#include "hooks/hooks_internal.h"
#include "hooks/send_input.h"
#include "hooks/window/snap.h"
#include "hooks/window/zones.h"

namespace altsnap::hooks {

/////////////////////////////////////////////////////////////////////////////
// Helper function with a stupid static accululator for
// The wheel in case we have a fine wheel that is unable to
// trigger a single step.
short ScaleDeltaAndAccum(short delta, short tar_delta) {
  short step =
      (static_cast<int>(tar_delta) * static_cast<int>(delta)) / WHEEL_DELTA;
  // Only accumulate
  if (step == 0) {  // step is too small, we need to accumulate delta
    static short delta_acc = 0;
    delta_acc += delta;
    // Recalculate the step.
    step = (tar_delta * delta_acc) / WHEEL_DELTA;
    if (step == 0) return 0;
    // Reset the value (simpler)
    delta_acc = 0;
  }
  return step;
}
namespace {

// Initializes COM on the current thread for the lifetime of the object.
class ScopedComApartment {
 public:
  ScopedComApartment()
      : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
  ~ScopedComApartment() {
    if (SUCCEEDED(hr_)) CoUninitialize();  // Only balance successful calls.
  }
  ScopedComApartment(const ScopedComApartment&) = delete;
  ScopedComApartment& operator=(const ScopedComApartment&) = delete;

 private:
  HRESULT hr_;
};

}  // namespace

// Uses the IAudioEndpointVolume COM interface to read the current volume step,
// the maximum step and the mute state of the default render device.
static HRESULT GetCurrentVolumeMute(UINT* current_volume, UINT* max_volume,
                                    BOOL* muted) {
  using Microsoft::WRL::ComPtr;
  const ScopedComApartment com;  // Needed for IAudioEndpointVolume.

  ComPtr<IMMDeviceEnumerator> enumerator;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
  if (FAILED(hr)) return hr;

  ComPtr<IMMDevice> device;
  hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
  if (FAILED(hr)) return hr;

  ComPtr<IAudioEndpointVolume> endpoint;
  hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void**>(endpoint.GetAddressOf()));
  if (FAILED(hr)) return hr;

  hr = endpoint->GetVolumeStepInfo(current_volume, max_volume);
  if (FAILED(hr)) return hr;
  hr = endpoint->GetMute(muted);

  LOG("%ld==GetVolumeStepInfo() -> %u (0 - %u) Muted=%d", hr, *current_volume,
      *max_volume, *muted);
  return hr;
}
// Under Vista this will change the main volume with ole interface,
void ActionVolume(int delta) {
  int num = (state.shift) ? 5 : 1;
  num = ScaleDeltaAndAccum(delta, num);
  num = abs(num);
  if (!num) return;

  UINT curentvol = 0, maxvol = 0;
  BOOL muted = FALSE;
  if (S_OK == GetCurrentVolumeMute(&curentvol, &maxvol, &muted) && curentvol) {
    if (delta < 0 && static_cast<UINT>(num) >= curentvol) {
      // We would Set the volume to Zero
      // We prefer to mute instead,
      // because otherwise it resets volume balance.
      if (!muted) Send_KEY(VK_VOLUME_MUTE);  // Mute
      return;
    }
    if (delta > 0) {
      if (muted) {
        Send_KEY(VK_VOLUME_MUTE);  // UnMute
        return;
      }
    }
  }
  while (num--) Send_KEY(delta > 0 ? VK_VOLUME_UP : VK_VOLUME_DOWN);
}

}  // namespace altsnap::hooks
