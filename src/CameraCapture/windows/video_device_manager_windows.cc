#include "CameraCapture/VideoDeviceBase.h"
#include "CameraCapture/windows/ds/device_info_ds.h"
#include "CameraCapture/windows/ds/video_capture_ds.h"
#ifdef ENABLE_MEDIA_FOUNDATION
#include "CameraCapture/windows/mf/device_info_mf.h"
#include "CameraCapture/windows/mf/video_capture_mf.h"
#endif
#include <memory>
#include "VersionHelpers.h"

namespace hrtc {
namespace videocapturemodule {

bool gSupportMediaFoundation = true;

namespace {

// 创建设备信息后端: 默认 DirectShow (更稳定), DS 不可用时回退 Media Foundation
std::unique_ptr<IDeviceInfo> CreateDeviceInfo() {
  std::unique_ptr<DeviceInfoDS> dsInfo(DeviceInfoDS::Create());
  if (dsInfo) {
    return std::unique_ptr<IDeviceInfo>(dsInfo.release());
  }
#ifdef ENABLE_MEDIA_FOUNDATION
  if (IsWindows10OrGreater()) {
    std::unique_ptr<DeviceInfoMF> mfInfo(DeviceInfoMF::Create());
    if (mfInfo) {
      gSupportMediaFoundation = true;
      return std::unique_ptr<IDeviceInfo>(mfInfo.release());
    }
    gSupportMediaFoundation = false;
  }
#endif
  return nullptr;
}

// 创建平台采集实现
std::shared_ptr<VideoDevice> CreatePlatformCapture(const std::string& deviceId,
                                                   const std::string& deviceName) {
  auto capture = std::make_shared<VideoCaptureDS>();
  if (capture->Init(deviceId, deviceName) == 0)
    return capture;
#ifdef ENABLE_MEDIA_FOUNDATION
  if (IsWindows10OrGreater() && gSupportMediaFoundation) {
    auto mfCapture = std::make_shared<VideoCaptureMF>();
    if (mfCapture->Init(deviceId, deviceName) == 0)
      return mfCapture;
  }
#endif
  return nullptr;
}

// 按设备唯一标识查找友好名称, 找不到时返回标识本身
std::string FindDeviceName(IDeviceInfo* deviceInfo,
                           const std::string& deviceId) {
  const uint32_t count = deviceInfo->NumberOfDevices();
  for (uint32_t i = 0; i < count; ++i) {
    std::string name, uniqueId;
    if (deviceInfo->GetDeviceName(i, name, uniqueId) == 0 &&
        uniqueId == deviceId) {
      return name;
    }
  }
  return deviceId;
}

}  // namespace

std::vector<std::shared_ptr<VideoDevice>> EnumerateDevices() {
  std::vector<std::shared_ptr<VideoDevice>> devices;
  auto deviceInfo = CreateDeviceInfo();
  if (!deviceInfo)
    return devices;

  const uint32_t count = deviceInfo->NumberOfDevices();
  for (uint32_t i = 0; i < count; ++i) {
    std::string name, uniqueId;
    if (deviceInfo->GetDeviceName(i, name, uniqueId) != 0)
      continue;
    auto device = CreatePlatformCapture(uniqueId, name);
    if (device)
      devices.push_back(device);
  }
  return devices;
}

std::shared_ptr<VideoDevice> CreateDevice(const std::string& deviceId) {
  if (deviceId.empty())
    return nullptr;

  auto deviceInfo = CreateDeviceInfo();
  const std::string name =
      deviceInfo ? FindDeviceName(deviceInfo.get(), deviceId) : deviceId;
  return CreatePlatformCapture(deviceId, name);
}

}  // namespace videocapturemodule
}  // namespace hrtc
