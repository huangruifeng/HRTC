#include "CameraCapture/windows/mf/video_capture_mf.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include "CameraCapture/windows/mf/source_reader_callback_mf.h"
#include <thread>

#define kWaitDuration 8.0

namespace hrtc {
namespace videocapturemodule {

VideoCaptureMF::VideoCaptureMF()
    : _mfSource(NULL),
    _mfSourceReader(NULL),
    _readerSinkCallback(NULL),
    _mfDesc(NULL),
    _isRunning(FALSE) ,
    _canCapture(FALSE),
    _lastRestartTimestamp(0) {}

VideoCaptureMF::~VideoCaptureMF() {
  _isRunning = false;
  SafeRelease(&_mfSourceReader);
  if (_mfSource) 
  {
    _mfSource->Shutdown();
  }
  SafeRelease(&_mfDesc);
  SafeRelease(&_mfSource);
  SafeRelease(&_readerSinkCallback);
}

int32_t VideoCaptureMF::Init(const std::string& deviceId,
                             const std::string& deviceName) {
  if (VideoDeviceBase::Init(deviceId, deviceName) != 0)
    return -1;
  SetDeviceInfo(&_mfInfo);
  if (_mfInfo.Init() != 0)
    return -1;
  return 0;
}

int VideoCaptureMF::StartCapture(const VideoCapability& capability) {
  if (_isRunning) {
    StopCapture();
  }
  std::lock_guard<std::mutex> cs(_startStopCs);
  if(startSourceReader()!=0)
      return -1;
  //if (capability != requestedCapability_)
  //note: always update capability,this's because the older setting won't work 
  //if cam was occupied by other program that time 
  {
    if (SetCameraOutput(capability) != 0)
      return -1;
  }

  HRESULT hr= RequestNextFrame();
  _isRunning = SUCCEEDED(hr);
  if (FAILED(hr))
  {
      //CAPTURE_DEVICE_EXCEPTION(hr, "StartCapture failed")
      //LOG(LS_ERROR) << "RequestNextFrame failed hr=0x" << std::hex << hr << std::dec << ",error:" << GetLastError();
  }
  return hr;
}

int VideoCaptureMF::StopCapture() {
  std::lock_guard<std::mutex> cs(_startStopCs);
  stopSourceReader();
  //todo call the real stop event.(note: calling stop method from MediaSource is not recommended if SourceReader is used: 
  //https://docs.microsoft.com/zh-cn/windows/desktop/api/mfreadwrite/nf-mfreadwrite-mfcreatesourcereaderfrommediasource
  _isRunning = FALSE;
  return 0;
}

bool VideoCaptureMF::IsCapturing() {
  if (_readerSinkCallback && _readerSinkCallback->currentStatus() == -1) {
      time_t start, stop;
      start = clock();
      double duration = 0;
      while (!_readerSinkCallback->hasCallback() && duration < kWaitDuration)
      {
          stop = clock();
          duration = (stop - start) / CLOCKS_PER_SEC;
      }
      //LOG(LS_INFO) << "check started complete duration:" << duration << "s" << ",has callback : "<<(_readerSinkCallback->hasCallback()?"true":"false");
  }
  std::lock_guard<std::mutex> cs(_startStopCs);

  if (_readerSinkCallback != NULL) {
    _canCapture = SUCCEEDED(_readerSinkCallback->currentStatus());
  } else {
      _canCapture = false;
  }
  return _isRunning && _canCapture;
}

int32_t VideoCaptureMF::SetCameraOutput(
    const VideoCapability& requestedCapability) {
  // Get the best matching capability
  VideoCapability capability;
  int32_t capabilityIndex;

  // Store the new requested size
  requestedCapability_ = requestedCapability;
  // Match the requested capability with the supported.
  auto capabilities = GetCapabilities();
  if ((capabilityIndex = FindBestCapability(capabilities, requestedCapability_,
                                            capability)) < 0) {
    //CAPTURE_DEVICE_EXCEPTION(UINT64_MAX, "Failed on GetBestMatchedCapability() during SetCameraOutput()")
    return -1;
  }

  _runningCapability = capability;

  // Reduce the frame rate if possible.
  if (capability.maxFPS > requestedCapability.maxFPS) {
    capability.maxFPS = requestedCapability.maxFPS;
  } else if (capability.maxFPS <= 0) {
    capability.maxFPS = 30;
  }

  // Convert it to the windows capability index since they are not nexessary
  // the same
  VideoCapabilityWindowsMf windowsCapability;
  if (_mfInfo.GetWindowsCapability(capabilityIndex, windowsCapability) != 0) {
    //CAPTURE_DEVICE_EXCEPTION(UINT64_MAX, "Failed on GetWindowsCapability() during SetCameraOutput()")
    return -1;
  }

  IMFStreamDescriptor* mfStreamDesc = NULL;
  IMFMediaTypeHandler* mfHandler = NULL;
  IMFMediaType* mfType = NULL;
  BOOL selected = NULL;
  SafeRelease(&_mfDesc);

  HRESULT hr = _mfSource->CreatePresentationDescriptor(&_mfDesc);
  if (FAILED(hr)) {
    //CAPTURE_DEVICE_EXCEPTION(hr, "Failed to create presentation descriptor during SetCameraOutput()")
    //LOG(LS_INFO) << "Failed to create presentation descriptor" << hr << ":"
    //             << GetLastError();
    goto done;
  }

  hr = _mfDesc->GetStreamDescriptorByIndex(0, &selected, &mfStreamDesc);
  if (FAILED(hr)) {
    //CAPTURE_DEVICE_EXCEPTION(hr, "Failed on GetStreamDescriptorByIndex() during SetCameraOutput()")
    //LOG(LS_INFO) << "Failed to GetStreamDescriptorByIndex" << hr << ":"
    //             << GetLastError();
    goto done;
  }
  if (!selected) {
    _mfDesc->SelectStream(0);
  }

  hr = mfStreamDesc->GetMediaTypeHandler(&mfHandler);
  if (FAILED(hr)) {
    //CAPTURE_DEVICE_EXCEPTION(hr, "Failed on GetMediaTypeHandler() during SetCameraOutput()")
    //LOG(LS_INFO) << "Failed to GetMediaTypeHandler" << hr << ":"
    //             << GetLastError();
    goto done;
  }

  hr = mfHandler->GetMediaTypeByIndex(windowsCapability.capabilityIndex,
                                      &mfType);
  if (FAILED(hr)) {
    //CAPTURE_DEVICE_EXCEPTION(hr, "Failed on GetMediaTypeByIndex() during SetCameraOutput()")
    //LOG(LS_INFO) << "Failed to GetMediaTypeByIndex" << hr << ":"
    //             << GetLastError();
    goto done;
  }

  // try set framerate
  PROPVARIANT var;
  if (SUCCEEDED(mfType->GetItem(MF_MT_FRAME_RATE_RANGE_MAX, &var))) {
    mfType->SetItem(MF_MT_FRAME_RATE, var);
    PropVariantClear(&var);
  }
  
  // hr = mfHandler->SetCurrentMediaType(mfType);
  hr = _mfSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                            NULL, mfType);
  if (FAILED(hr)) {
    //CAPTURE_DEVICE_EXCEPTION(hr, "Failed on SetCurrentMediaType() during SetCameraOutput()")
    //LOG(LS_ERROR) << "Failed to set currentMediaType, error:0x" << std::hex << hr << std::dec;
  }
done:
  SafeRelease(&mfStreamDesc);
  SafeRelease(&mfHandler);
  SafeRelease(&mfType);
  return SUCCEEDED(hr) ? 0 : -1;
}

std::mutex* VideoCaptureMF::GetStartStopCs() {
  return &_startStopCs;
}

const bool& VideoCaptureMF::IsRunning() const {
  return _isRunning;
}

const VideoCapability& VideoCaptureMF::RunningCapability() const {
  return _runningCapability;
}

HRESULT VideoCaptureMF::RequestNextFrame() const {
  return _mfSourceReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                     NULL, NULL, NULL, NULL);
}

int32_t VideoCaptureMF::PushIncomingFrame(
    uint8_t* videoFrame,
    size_t videoFrameLength,
    const VideoCapability& frameInfo,
    int64_t captureTime) const {
  const_cast<VideoCaptureMF*>(this)->DeliverFrame(
      videoFrame, videoFrameLength, frameInfo, captureTime);
  return 0;
}

const VideoCapability VideoCaptureMF::RequestCapabilityUpdate() {
  if (_mfSourceReader) {
    IMFMediaType* type = nullptr;
    HRESULT hr = _mfSourceReader->GetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
    if (SUCCEEDED(hr)) {
      UINT32 width, height;
      if (SUCCEEDED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height))) {
        _runningCapability.width = width;
        _runningCapability.height = height;
        //we assume the type did not change; todo: query media type and update
      }
    } else {
      //LOG(LS_ERROR) << "Failed to get current media type, err: " << hr;
    }
  }
  return _runningCapability;
}

int32_t VideoCaptureMF::stopSourceReader()
{
    //LOG(LS_INFO) << "stop source reader:" << _mfSource;
    SafeRelease(&_mfSourceReader);
    if (_mfSource) {
      _mfSource->Shutdown();
    }
    SafeRelease(&_mfSource);
    return 0;
}

int32_t VideoCaptureMF::startSourceReader()
{
    if ( _mfSource != nullptr )
        return 0;

    _mfSource = _mfInfo.GetDeviceMediaSource(deviceId());

    if ( !_mfSource ) {
        // CAPTURE_DEVICE_EXCEPTION(UINT64_MAX, "Failed to create capture source during startSourceReader()")
        // LOG(LS_INFO) << "Failed to create capture source.";
        return -1;
    }

    IMFAttributes* attributes = NULL;
    HRESULT hr = MFCreateAttributes(&attributes, 2);
  
    if ( FAILED(hr) ) {
        //CAPTURE_DEVICE_EXCEPTION(hr, "Failed on MFCreateAttributes() during startSourceReader()")
        return -1;
    }
    attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS,
                          TRUE);  // don't care if it fails 
    if (!_readerSinkCallback) {
      _readerSinkCallback = new SourceReaderCallbackMF(this);
    }
    hr = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,
                                _readerSinkCallback);
    if ( FAILED(hr) ) {
        // CAPTURE_DEVICE_EXCEPTION(hr, "Failed to set source reader callback attribute during startSourceReader()")
        // LOG(LS_ERROR) << "Failed to set source reader callback attribute, error:"
        //     << hr;
        SafeRelease(&attributes);
        return -1;
    }

    hr = MFCreateSourceReaderFromMediaSource(_mfSource, attributes,
                                             &_mfSourceReader);

    if ( FAILED(hr) ) {
        // CAPTURE_DEVICE_EXCEPTION(hr, "Failed to create capture source reader during startSourceReader()")
        // LOG(LS_INFO) << "Failed to create capture source reader";
        SafeRelease(&attributes);
        return -1;
    }
    return 0;
}

void VideoCaptureMF::OnReadSampleFailed(HRESULT hr) {
    switch (hr)
    {
    case MF_E_INVALIDREQUEST:
    break;
    case 0x80070491:
        //LOG(LS_WARNING) << "mf read sample return error_no_match; now try to stop "
        //    "and restart the hardware ";
        StopCapture();
        StartCapture(_runningCapability);
        return;
        break;
    case MF_E_HW_MFT_FAILED_START_STREAMING:
        //NotifyRuntimeErr(CaptureRuntimeError::kCameraError);
        return;
        break;
    default:
        break;
    }
  RequestNextFrame();
}

}  // namespace videocapturemodule
}  // namespace hrtc
