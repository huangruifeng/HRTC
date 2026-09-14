#include "ScreenCapture/windows/screen/screen_capture_base.h"

namespace hrtc {
namespace videocapturemodule {

ScreenCaptureBase::ScreenCaptureBase() {}

// 注意: 不在基类析构中调 StopCapture, 纯虚 StopInternal 在子类析构后才不可用;
// 停止采集由子类析构负责
ScreenCaptureBase::~ScreenCaptureBase() {}

void ScreenCaptureBase::SetVideoSink(IVideoSink* sink) {
  std::lock_guard<std::mutex> lock(apiLock_);
  sink_ = sink;
}

int ScreenCaptureBase::StartCapture(const ScreenSource& source,
                                    const VideoCapability& capability) {
  if (capturing_)
    StopCapture();

  source_ = source;
  capability_ = capability;
  if (capability_.maxFPS <= 0)
    capability_.maxFPS = 15;
  if (StartInternal() != 0)
    return -1;

  capturing_ = true;
  return 0;
}

int ScreenCaptureBase::StopCapture() {
  if (!capturing_)
    return 0;
  capturing_ = false;
  return StopInternal();
}

void ScreenCaptureBase::DeliverFrame(const uint8_t* data,
                                     size_t size,
                                     int width,
                                     int height,
                                     VideoPixelFormat format,
                                     int64_t timestampMs) {
  std::lock_guard<std::mutex> lock(apiLock_);
  if (!sink_)
    return;

  VideoFrame frame(data, size, width, height, format,
                   VideoRotation::kVideoRotation_0, timestampMs);
  sink_->OnVideoFrame(frame);
}

}  // namespace videocapturemodule
}  // namespace hrtc
