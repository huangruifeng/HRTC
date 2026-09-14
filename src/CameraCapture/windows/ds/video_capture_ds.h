#ifndef MODULES_VIDEO_CAPTURE_MAIN_SOURCE_WINDOWS_VIDEO_CAPTURE_DS_H_
#define MODULES_VIDEO_CAPTURE_MAIN_SOURCE_WINDOWS_VIDEO_CAPTURE_DS_H_

#include "Base/HrtcRefPtr.h"
#include "CameraCapture/VideoDeviceBase.h"
#include "CameraCapture/windows/ds/device_info_ds.h"

#define CAPTURE_FILTER_NAME L"VideoCaptureFilter"
#define SINK_FILTER_NAME L"SinkFilter"

namespace hrtc {
namespace videocapturemodule {
// Forward declaraion
class CaptureSinkFilter;

// Windows DirectShow 采集实现
class VideoCaptureDS : public VideoDeviceBase {
 public:
  VideoCaptureDS();

  virtual int32_t Init(const std::string& deviceId, const std::string& deviceName);

  /*************************************************************************
   *
   *   Start/Stop
   *
   *************************************************************************/
  int StartCapture(const VideoCapability& capability) override;
  int StopCapture() override;

  bool IsCapturing() override;

  ~VideoCaptureDS() override;
 protected:

  // Help functions

  int32_t SetCameraOutput(const VideoCapability& requestedCapability);
  int32_t DisconnectGraph();
  HRESULT ConnectDVCamera();

  DeviceInfoDS _dsInfo ;

  IBaseFilter* _captureFilter ;
  IGraphBuilder* _graphBuilder ;
  IMediaControl* _mediaControl ;
  hrtc::rtc_refptr<CaptureSinkFilter> sink_filter_;
  IPin* _inputSendPin ;
  IPin* _outputCapturePin ;

  // Microsoft DV interface (external DV cameras)
  IBaseFilter* _dvFilter ;
  IPin* _inputDvPin ;
  IPin* _outputDvPin ;
};
}  // namespace videocapturemodule
}  // namespace hrtc
#endif  // MODULES_VIDEO_CAPTURE_MAIN_SOURCE_WINDOWS_VIDEO_CAPTURE_DS_H_
