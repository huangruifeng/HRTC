#pragma once
#include <mutex>

#include "VideoCommon/VideoCaptureDefines.h"

namespace hrtc {
namespace videocapturemodule {

class ISourceReaderControlMF {
 public:
  virtual ~ISourceReaderControlMF() = default;
  virtual std::mutex* GetStartStopCs() = 0;
  virtual const bool& IsRunning() const = 0;
  virtual const VideoCapability& RunningCapability() const = 0;
  virtual HRESULT RequestNextFrame() const = 0;
  virtual int32_t PushIncomingFrame(uint8_t* videoFrame,
                                    size_t videoFrameLength,
                                    const VideoCapability& frameInfo,
                                    int64_t captureTime = 0) const = 0;

  virtual void OnReadSampleFailed(HRESULT hr) = 0;
  virtual const VideoCapability RequestCapabilityUpdate() = 0;
};

}  // namespace videocapturemodule
}  // namespace hrtc
