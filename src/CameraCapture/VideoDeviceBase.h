#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "CameraCapture/VideoDevice.h"

namespace hrtc {
namespace videocapturemodule {

class CaptureSinkFilter;

// 设备枚举与能力查询内部接口 (平台实现: DeviceInfoMF / DeviceInfoDS)
class IDeviceInfo {
 public:
  virtual ~IDeviceInfo() = default;

  // 设备数量
  virtual uint32_t NumberOfDevices() = 0;
  // 获取指定设备的友好名称与唯一标识
  virtual int32_t GetDeviceName(uint32_t deviceNumber,
                                std::string& deviceName,
                                std::string& deviceUniqueId) = 0;
  // 获取设备能力数量
  virtual int32_t NumberOfCapabilities(const std::string& deviceUniqueId) = 0;
  // 获取指定能力
  virtual int32_t GetCapability(const std::string& deviceUniqueId,
                                uint32_t deviceCapabilityNumber,
                                VideoCapability& capability) = 0;
};

// 采集设备公共基类: 负责设备标识/能力查询/帧回调分发,
// 平台实现只需关注采集本身的启动与停止
class VideoDeviceBase : public VideoDevice {
 public:
  friend class CaptureSinkFilter;
  VideoDeviceBase();
  ~VideoDeviceBase() override;

  // ---- VideoDevice 接口公共实现 ----
  const std::string& deviceId() const override { return deviceId_; }
  const std::string& deviceName() const override { return deviceName_; }
  std::vector<VideoCapability> GetCapabilities() override;
  void SetVideoSink(IVideoSink* sink) override;

 protected:
  // 平台实现初始化时调用, 设置设备标识与名称
  int32_t Init(const std::string& deviceId, const std::string& deviceName);
  // 平台采集线程产生帧后调用, 分发到已注册的 sink
  void DeliverFrame(uint8_t* videoFrame,
                    size_t videoFrameLength,
                    const VideoCapability& frameInfo,
                    int64_t captureTime = 0);
  // 设置设备信息后端(用于能力查询, 由平台实现持有)
  void SetDeviceInfo(IDeviceInfo* deviceInfo) { deviceInfo_ = deviceInfo; }

  // 当前请求的采集能力
  VideoCapability requestedCapability_;

 private:
  std::mutex apiLock_;
  IDeviceInfo* deviceInfo_ = nullptr;
  IVideoSink* sink_ = nullptr;
  VideoRotation rotateFrame_ = VideoRotation::kVideoRotation_0;

  std::string deviceId_;
  std::string deviceName_;
};
}  // namespace videocapturemodule
}  // namespace hrtc
