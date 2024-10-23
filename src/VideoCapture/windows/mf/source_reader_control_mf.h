#pragma once
#include <mutex>

namespace hrtc {
namespace videocapturemodule {

class ISourceReaderControlMF {
 public:
  virtual ~ISourceReaderControlMF() = default;
  virtual std::mutex* GetStartStopCs() = 0;
  virtual const bool& IsRunning() const = 0;
  virtual const VideoCaptureCapability& RunningCapability() const = 0;
  virtual HRESULT RequestNextFrame() const = 0;
  virtual int32_t PushIncomingFrame(uint8_t* videoFrame,
                                    size_t videoFrameLength,
                                    const VideoCaptureCapability& frameInfo,
                                    int64_t captureTime = 0) const = 0;

  virtual void OnReadSampleFailed(HRESULT hr) = 0;
  virtual const VideoCaptureCapability RequestCapabilityUpdate() = 0;
};

}  // namespace videocapturemodule
}  // namespace webrtc

