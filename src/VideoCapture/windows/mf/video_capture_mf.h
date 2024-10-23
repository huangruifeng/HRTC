#pragma once

#include "VideoCapture/VideoCaptureImpl.h"
#include "VideoCapture/windows/mf/device_info_mf.h"
#include "VideoCapture/windows/mf/source_reader_control_mf.h"

#include <mfreadwrite.h>

namespace hrtc {
namespace videocapturemodule {

class SourceReaderCallbackMF;

// VideoCapture implementation that uses the Media Foundation API on Windows.
// This will replace the DirectShow based implementation on Vista and higher.
// TODO(tommi): Finish implementing and switch out the DS in the factory method
// for supported platforms.
class VideoCaptureMF : public VideoCaptureImpl, private ISourceReaderControlMF {
 public:
  explicit VideoCaptureMF();

  int32_t Init(const char* device_id);

  // Overrides from VideoCaptureImpl.
  virtual int32_t StartCapture(const VideoCaptureCapability& capability);
  virtual int32_t StopCapture();
  virtual bool CaptureStarted();
  virtual int32_t CaptureSettings(VideoCaptureCapability& settings);  // NOLINT
  int32_t SetCameraOutput(const VideoCaptureCapability& requestedCapability);


  virtual ~VideoCaptureMF();
protected:
  // ISourceReaderControlMF
  std::mutex* GetStartStopCs() override;
  const bool& IsRunning() const override;
  const VideoCaptureCapability& RunningCapability() const override;
  HRESULT RequestNextFrame() const override;
  int32_t PushIncomingFrame(uint8_t* videoFrame,
                                  size_t videoFrameLength,
                                  const VideoCaptureCapability& frameInfo,
                                  int64_t captureTime = 0) const override;

  const VideoCaptureCapability RequestCapabilityUpdate() override;

  // Inherited via ISourceReaderControlMF
  virtual void OnReadSampleFailed(HRESULT hr) override;

  int32_t stopSourceReader();
  int32_t startSourceReader();

  DeviceInfoMF _mfInfo;
  IMFMediaSource* _mfSource;
  IMFSourceReader* _mfSourceReader;
  IMFPresentationDescriptor* _mfDesc;
  VideoCaptureCapability _runningCapability; 
  SourceReaderCallbackMF* _readerSinkCallback;

  std::mutex _startStopCs;
  bool _isRunning;
  bool _canCapture;

  int64_t _lastRestartTimestamp;
};

}  // namespace videocapturemodule
}  // namespace webrtc

  