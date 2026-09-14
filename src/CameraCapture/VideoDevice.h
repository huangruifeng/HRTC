#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "VideoCommon/VideoCaptureDefines.h"
#include "VideoCommon/VideoFrame.h"
#include "VideoCommon/VideoRotation.h"

namespace hrtc {

// 摄像头设备
class VideoDevice {
 public:
  virtual ~VideoDevice() = default;

  // 设备唯一标识
  virtual const std::string& deviceId() const = 0;
  // 设备友好名称
  virtual const std::string& deviceName() const = 0;
  // 获取设备支持的采集能力列表
  virtual std::vector<VideoCapability> GetCapabilities() = 0;
  // 按指定能力开始采集
  virtual int StartCapture(const VideoCapability& capability) = 0;
  // 停止采集
  virtual int StopCapture() = 0;
  // 是否正在采集
  virtual bool IsCapturing() = 0;
  // 设置视频帧回调, 传 nullptr 清除
  virtual void SetVideoSink(IVideoSink* sink) = 0;
};

// 从能力列表中选出与请求最匹配的能力
int FindBestCapability(const std::vector<VideoCapability>& capabilities,
                       const VideoCapability& requested,
                       VideoCapability& result);

}  // namespace hrtc
