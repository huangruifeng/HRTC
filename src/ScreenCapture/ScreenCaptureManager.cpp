#include "ScreenCapture/ScreenCapture.h"

namespace hrtc {

#if defined(HRTC_WINDOWS)
namespace videocapturemodule {
// Windows 平台实现, 见 windows/screen/screen_capture_windows.cc
std::vector<ScreenSource> EnumerateScreenSources();
std::shared_ptr<ScreenCapturer> CreateScreenCapturer();
}  // namespace videocapturemodule
#endif

std::vector<ScreenSource> ScreenCaptureManager::EnumerateSources() {
#if defined(HRTC_WINDOWS)
  return videocapturemodule::EnumerateScreenSources();
#else
  return {};
#endif
}

std::shared_ptr<ScreenCapturer> ScreenCaptureManager::CreateCapturer() {
#if defined(HRTC_WINDOWS)
  return videocapturemodule::CreateScreenCapturer();
#else
  return nullptr;
#endif
}

}  // namespace hrtc
