#pragma once

#include <memory>
#include <string>
#include <vector>

#include "CameraCapture/VideoDevice.h"

namespace hrtc {

// 视频设备管理器: 枚举摄像头并创建采集设备
class VideoDeviceManager {
 public:
  VideoDeviceManager() = default;
  ~VideoDeviceManager() = default;

  // 枚举当前系统所有摄像头
  std::vector<std::shared_ptr<VideoDevice>> EnumerateDevices();
  // 按设备唯一标识创建采集设备, 失败返回 nullptr
  std::shared_ptr<VideoDevice> CreateDevice(const std::string& deviceId);
};

}  // namespace hrtc
