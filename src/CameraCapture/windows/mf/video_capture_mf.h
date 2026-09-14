#pragma once

#include "CameraCapture/VideoDeviceBase.h"
#include "CameraCapture/windows/mf/device_info_mf.h"
#include "CameraCapture/windows/mf/source_reader_control_mf.h"

#include <mfreadwrite.h>

namespace hrtc {
namespace videocapturemodule {

class SourceReaderCallbackMF;

// Windows Media Foundation 采集实现
class VideoCaptureMF : public VideoDeviceBase, private ISourceReaderControlMF {
 public:
  explicit VideoCaptureMF();

  int32_t Init(const std::string& deviceId, const std::string& deviceName);

  // VideoDevice 接口实现
  int StartCapture(const VideoCapability& capability) override;
  int StopCapture() override;
  bool IsCapturing() override;

  virtual ~VideoCaptureMF();

 protected:
  // ISourceReaderControlMF
  std::mutex* GetStartStopCs() override;
  const bool& IsRunning() const override;
  const VideoCapability& RunningCapability() const override;
  HRESULT RequestNextFrame() const override;
  int32_t PushIncomingFrame(uint8_t* videoFrame,
                            size_t videoFrameLength,
                            const VideoCapability& frameInfo,
                            int64_t captureTime = 0) const override;

  const VideoCapability RequestCapabilityUpdate() override;
  virtual void OnReadSampleFailed(HRESULT hr) override;

  int32_t stopSourceReader();
  int32_t startSourceReader();
  int32_t SetCameraOutput(const VideoCapability& requestedCapability);

  DeviceInfoMF _mfInfo;
  IMFMediaSource* _mfSource;
  IMFSourceReader* _mfSourceReader;
  IMFPresentationDescriptor* _mfDesc;
  VideoCapability _runningCapability;
  SourceReaderCallbackMF* _readerSinkCallback;

  std::mutex _startStopCs;
  bool _isRunning;
  bool _canCapture;

  int64_t _lastRestartTimestamp;
};

}  // namespace videocapturemodule
}  // namespace hrtc
