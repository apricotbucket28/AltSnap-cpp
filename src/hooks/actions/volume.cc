// volume (split out of hooks.cc).

#include "hooks/actions/volume.h"

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <cstdlib>
#include <limits>

#include "hooks/hooks_internal.h"
#include "hooks/send_input.h"

namespace altsnap::hooks {

// Scales mouse wheel deltas into steps, accumulating small movements until
// they are sufficient to produce a step on high-resolution mouse wheels.
// Upstream: ScaleDeltaAndAccum
int ScaleWheelDelta(int wheel_delta, int step_scale) {
  int steps = (step_scale * wheel_delta) / WHEEL_DELTA;

  if (steps == 0) {
    static int accumulated_delta = 0;
    accumulated_delta += wheel_delta;

    steps = (step_scale * accumulated_delta) / WHEEL_DELTA;
    if (steps == 0) {
      return 0;
    }

    accumulated_delta = 0;
  }

  return steps;
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

void ActionVolume(int delta) {
  const int scaled_delta = ScaleWheelDelta(delta, state.shift ? 5 : 1);
  if (scaled_delta == std::numeric_limits<int>::min()) {
    return;
  }

  const int steps = std::abs(scaled_delta);
  if (steps == 0) {
    return;
  }

  UINT current_volume = 0;
  UINT max_volume = 0;
  BOOL muted = FALSE;

  if (GetCurrentVolumeMute(&current_volume, &max_volume, &muted) == S_OK &&
      current_volume != 0) {
    if (delta < 0 && static_cast<UINT>(steps) >= current_volume) {
      // Prefer muting over setting the volume to zero, because setting it to
      // zero resets the volume balance.
      if (!muted) {
        SendKey(VK_VOLUME_MUTE);
      }
      return;
    }

    if (delta > 0 && muted) {
      // Unmute without also increasing the volume.
      SendKey(VK_VOLUME_MUTE);
      return;
    }
  }

  const auto volume_key = delta > 0 ? VK_VOLUME_UP : VK_VOLUME_DOWN;
  for (int i = 0; i < steps; ++i) {
    SendKey(volume_key);
  }
}

}  // namespace altsnap::hooks
