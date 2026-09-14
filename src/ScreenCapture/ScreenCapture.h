#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "VideoCommon/VideoCaptureDefines.h"
#include "VideoCommon/VideoFrame.h"

namespace hrtc {

// 屏幕采集源类型
enum class ScreenSourceType {
  kUnknown = 0,
  kScreen,  // 显示器
  kWindow,  // 窗口
};

// 屏幕采集源(显示器/窗口)
struct ScreenSource {
  ScreenSourceType type = ScreenSourceType::kUnknown;
  std::string sourceId;    // 唯一标识: "screen:<hex handle>" / "window:<hex handle>"
  std::string sourceName;  // 显示器设备名 / 窗口标题
  int width = 0;
  int height = 0;
  uint64_t windowId = 0;  // 平台句柄(Windows: HWND), 跨平台保持 uint64_t
};

// 屏幕采集器
class ScreenCapturer {
 public:
  virtual ~ScreenCapturer() = default;

  // 当前采集源信息
  virtual const ScreenSource& source() const = 0;
  // 开始采集, 失败返回负值
  virtual int StartCapture(const ScreenSource& source,
                           const VideoCapability& capability) = 0;
  // 停止采集
  virtual int StopCapture() = 0;
  // 是否正在采集
  virtual bool IsCapturing() = 0;
  // 设置视频帧回调, 传 nullptr 清除
  virtual void SetVideoSink(IVideoSink* sink) = 0;
};

// 屏幕采集管理器: 枚举采集源并创建采集器
class ScreenCaptureManager {
 public:
  ScreenCaptureManager() = default;
  ~ScreenCaptureManager() = default;

  // 枚举所有可用采集源: 显示器 + 可见窗口
  std::vector<ScreenSource> EnumerateSources();
  // 创建采集器, 失败返回 nullptr
  std::shared_ptr<ScreenCapturer> CreateCapturer();
};

}  // namespace hrtc
