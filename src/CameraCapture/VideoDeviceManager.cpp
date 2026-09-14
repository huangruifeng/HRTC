#include "CameraCapture/VideoDeviceManager.h"

#include <algorithm>
#include <cstdint>

#include "VideoCommon/VideoCaptureDefines.h"

namespace hrtc {

#if defined(HRTC_WINDOWS)
namespace videocapturemodule {
// Windows 平台实现, 见 windows/video_device_manager_windows.cc
std::vector<std::shared_ptr<VideoDevice>> EnumerateDevices();
std::shared_ptr<VideoDevice> CreateDevice(const std::string& deviceId);
}  // namespace videocapturemodule
#endif

// 从能力列表中选出与请求最匹配的能力:
// 优先宽高不小于请求且最接近, 其次帧率, 格式不匹配时惩罚
int FindBestCapability(const std::vector<VideoCapability>& capabilities,
                       const VideoCapability& requested,
                       VideoCapability& result) {
  if (capabilities.empty())
    return -1;

  int bestIndex = 0;
  int64_t bestScore = INT64_MAX;
  for (size_t i = 0; i < capabilities.size(); ++i) {
    const VideoCapability& capability = capabilities[i];
    int64_t score = 0;
    // 宽高不足请求时重罚
    score += std::max<int64_t>(0, requested.width - capability.width) * 8;
    score += std::max<int64_t>(0, requested.height - capability.height) * 8;
    // 超出请求越多, 代价越高
    score += std::max<int64_t>(0, capability.width - requested.width);
    score += std::max<int64_t>(0, capability.height - requested.height);
    // 帧率不足时惩罚
    score += std::max<int64_t>(0, requested.maxFPS - capability.maxFPS) * 4;
    // 指定格式时不匹配惩罚
    if (requested.format != VideoPixelFormat::kUnknown &&
        capability.format != requested.format) {
      score += 1000;
    }
    if (score < bestScore) {
      bestScore = score;
      bestIndex = static_cast<int>(i);
    }
  }
  result = capabilities[bestIndex];
  return bestIndex;
}

std::vector<std::shared_ptr<VideoDevice>> VideoDeviceManager::EnumerateDevices() {
#if defined(HRTC_WINDOWS)
  return videocapturemodule::EnumerateDevices();
#else
  return {};
#endif
}

std::shared_ptr<VideoDevice> VideoDeviceManager::CreateDevice(
    const std::string& deviceId) {
#if defined(HRTC_WINDOWS)
  return videocapturemodule::CreateDevice(deviceId);
#else
  return nullptr;
#endif
}

}  // namespace hrtc
