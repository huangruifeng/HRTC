#pragma once

#include <atomic>
#include <mutex>

#include "ScreenCapture/ScreenCapture.h"

namespace hrtc {
namespace videocapturemodule {

// 屏幕采集公共基类: 管理源信息/帧回调/采集状态,
// 子类实现 StartInternal/StopInternal 采集逻辑
class ScreenCaptureBase : public ScreenCapturer {
 public:
  ScreenCaptureBase();
  ~ScreenCaptureBase() override;

  const ScreenSource& source() const override { return source_; }
  bool IsCapturing() override { return capturing_; }
  void SetVideoSink(IVideoSink* sink) override;

  int StartCapture(const ScreenSource& source,
                   const VideoCapability& capability) override;
  int StopCapture() override;

 protected:
  // 采集线程产生帧后调用, 分发到已注册的 sink
  void DeliverFrame(const uint8_t* data,
                    size_t size,
                    int width,
                    int height,
                    VideoPixelFormat format,
                    int64_t timestampMs = 0);

  ScreenSource source_;
  VideoCapability capability_;
  std::atomic<bool> capturing_{false};

 private:
  virtual int StartInternal() = 0;
  virtual int StopInternal() = 0;

  std::mutex apiLock_;
  IVideoSink* sink_ = nullptr;
};

}  // namespace videocapturemodule
}  // namespace hrtc
