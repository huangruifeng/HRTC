#include "CameraCapture/VideoDeviceBase.h"

namespace hrtc {
namespace videocapturemodule {

VideoDeviceBase::VideoDeviceBase() {}

VideoDeviceBase::~VideoDeviceBase() {}

int32_t VideoDeviceBase::Init(const std::string& deviceId,
                              const std::string& deviceName) {
  if (deviceId.empty() || deviceId.size() > kVideoCaptureUniqueNameLength)
    return -1;
  deviceId_ = deviceId;
  deviceName_ = deviceName;
  return 0;
}

std::vector<VideoCapability> VideoDeviceBase::GetCapabilities() {
  std::vector<VideoCapability> capabilities;
  if (!deviceInfo_)
    return capabilities;

  const int32_t count = deviceInfo_->NumberOfCapabilities(deviceId_);
  for (int32_t i = 0; i < count; ++i) {
    VideoCapability capability;
    if (deviceInfo_->GetCapability(deviceId_, i, capability) == 0)
      capabilities.push_back(capability);
  }
  return capabilities;
}

void VideoDeviceBase::SetVideoSink(IVideoSink* sink) {
  std::lock_guard<std::mutex> lock(apiLock_);
  sink_ = sink;
}

void VideoDeviceBase::DeliverFrame(uint8_t* videoFrame,
                                   size_t videoFrameLength,
                                   const VideoCapability& frameInfo,
                                   int64_t captureTime) {
  std::lock_guard<std::mutex> lock(apiLock_);
  if (!sink_)
    return;

  VideoFrame frame(videoFrame, videoFrameLength, frameInfo.width,
                   frameInfo.height, frameInfo.format, rotateFrame_,
                   captureTime);
  sink_->OnVideoFrame(frame);
}
}  // namespace videocapturemodule
}  // namespace hrtc
