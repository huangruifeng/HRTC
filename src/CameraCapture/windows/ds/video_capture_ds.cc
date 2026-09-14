/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "CameraCapture/windows/ds/video_capture_ds.h"

#include <dvdmedia.h>  // VIDEOINFOHEADER2

#include "CameraCapture/windows/ds/help_functions_ds.h"
#include "CameraCapture/windows/ds/sink_filter_ds.h"
#include "Base/base.h"

namespace hrtc {
namespace videocapturemodule {
VideoCaptureDS::VideoCaptureDS()
    : _captureFilter(NULL),
      _graphBuilder(NULL),
      _mediaControl(NULL),
      _inputSendPin(NULL),
      _outputCapturePin(NULL),
      _dvFilter(NULL),
      _inputDvPin(NULL),
      _outputDvPin(NULL) {}

VideoCaptureDS::~VideoCaptureDS() {
  if (_mediaControl) {
    _mediaControl->Stop();
  }
  if (_graphBuilder) {
    if (sink_filter_)
      _graphBuilder->RemoveFilter(sink_filter_.get());
    if (_captureFilter)
      _graphBuilder->RemoveFilter(_captureFilter);
    if (_dvFilter)
      _graphBuilder->RemoveFilter(_dvFilter);
  }
  RELEASE_AND_CLEAR(_inputSendPin);
  RELEASE_AND_CLEAR(_outputCapturePin);

  RELEASE_AND_CLEAR(_captureFilter);  // release the capture device
  RELEASE_AND_CLEAR(_dvFilter);

  RELEASE_AND_CLEAR(_mediaControl);

  RELEASE_AND_CLEAR(_inputDvPin);
  RELEASE_AND_CLEAR(_outputDvPin);

  RELEASE_AND_CLEAR(_graphBuilder);
}

int32_t VideoCaptureDS::Init(const std::string& deviceId,
                           const std::string& deviceName) {
  if (VideoDeviceBase::Init(deviceId, deviceName) != 0)
    return -1;
  SetDeviceInfo(&_dsInfo);

  if (_dsInfo.Init() != 0)
    return -1;

  _captureFilter = _dsInfo.GetDeviceFilter(deviceId.c_str());
  if (!_captureFilter) {
    LOG_INFO("ds","Failed to create capture filter.")
    return -1;
  }

  // Get the interface for DirectShow's GraphBuilder
  HRESULT hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
                                IID_IGraphBuilder, (void**)&_graphBuilder);
  if (FAILED(hr)) {
    LOG_INFO("ds","Failed to create graph builder.")
    return -1;
  }

  hr = _graphBuilder->QueryInterface(IID_IMediaControl, (void**)&_mediaControl);
  if (FAILED(hr)) {
    LOG_INFO("ds", "Failed to create media control builder.")
    return -1;
  }
  hr = _graphBuilder->AddFilter(_captureFilter, CAPTURE_FILTER_NAME);
  if (FAILED(hr)) {
    LOG_INFO("ds","Failed to add the capture device to the graph.")
    return -1;
  }

  _outputCapturePin = GetOutputPin(_captureFilter, PIN_CATEGORY_CAPTURE);
  if (!_outputCapturePin) {
    LOG_INFO("ds","Failed to get output capture pin")
    return -1;
  }

  // Create the sink filte used for receiving Captured frames.
  sink_filter_ = new ComRefCount<CaptureSinkFilter>(this);

  hr = _graphBuilder->AddFilter(sink_filter_.get(), SINK_FILTER_NAME);
  if (FAILED(hr)) {
    LOG_INFO("ds","Failed to add the send filter to the graph.")
    return -1;
  }

  _inputSendPin = GetInputPin(sink_filter_.get());
  if (!_inputSendPin) {
    LOG_INFO("ds","Failed to get input send pin")
    return -1;
  }

  if (SetCameraOutput(requestedCapability_) != 0) {
    return -1;
  }
  LOG_INFO("ds","Capture device '" << deviceId()
                   << "' initialized.")
  return 0;
}

int VideoCaptureDS::StartCapture(const VideoCapability& capability) {

  if (capability != requestedCapability_) {
    DisconnectGraph();

    if (SetCameraOutput(capability) != 0) {
      return -1;
    }
  }
  HRESULT hr = _mediaControl->Pause();
  if (FAILED(hr)) {
    LOG_INFO("ds","Failed to Pause the Capture device. Is it already occupied? " << hr)
    return -1;
  }
  hr = _mediaControl->Run();
  if (FAILED(hr)) {
    LOG_INFO("ds","Failed to start the Capture device.")
    return -1;
  }
  return 0;
}

int VideoCaptureDS::StopCapture() {

  HRESULT hr = _mediaControl->StopWhenReady();
  if (FAILED(hr)) {
    LOG_INFO("ds", "Failed to stop the capture graph. " << hr)
    return -1;
  }
  return 0;
}

bool VideoCaptureDS::IsCapturing() {

  OAFilterState state = 0;
  HRESULT hr = _mediaControl->GetState(1000, &state);
  if (hr != S_OK && hr != VFW_S_CANT_CUE) {
    LOG_INFO("ds","Failed to get the CaptureStarted status");
  }
  LOG_INFO("ds",""CaptureStarted " << state")
  return state == State_Running;
}

int32_t VideoCaptureDS::SetCameraOutput(
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
    return -1;
  }
  // Reduce the frame rate if possible.
  if (capability.maxFPS > requestedCapability.maxFPS) {
    capability.maxFPS = requestedCapability.maxFPS;
  } else if (capability.maxFPS <= 0) {
    capability.maxFPS = 30;
  }

  // Convert it to the windows capability index since they are not nexessary
  // the same
  VideoCapabilityWindows windowsCapability;
  if (_dsInfo.GetWindowsCapability(capabilityIndex, windowsCapability) != 0) {
    return -1;
  }

  IAMStreamConfig* streamConfig = NULL;
  AM_MEDIA_TYPE* pmt = NULL;
  VIDEO_STREAM_CONFIG_CAPS caps;

  HRESULT hr = _outputCapturePin->QueryInterface(IID_IAMStreamConfig,
                                                 (void**)&streamConfig);
  if (hr) {
    LOG_INFO("ds","Can't get the Capture format settings.")
    return -1;
  }

  // Get the windows capability from the capture device
  bool isDVCamera = false;
  hr = streamConfig->GetStreamCaps(windowsCapability.directShowCapabilityIndex,
                                   &pmt, reinterpret_cast<BYTE*>(&caps));
  if (hr == S_OK) {
    if (pmt->formattype == FORMAT_VideoInfo2) {
      VIDEOINFOHEADER2* h = reinterpret_cast<VIDEOINFOHEADER2*>(pmt->pbFormat);
      if (capability.maxFPS > 0 && windowsCapability.supportFrameRateControl) {
        h->AvgTimePerFrame = REFERENCE_TIME(10000000.0 / capability.maxFPS);
      }
    } else {
      VIDEOINFOHEADER* h = reinterpret_cast<VIDEOINFOHEADER*>(pmt->pbFormat);
      if (capability.maxFPS > 0 && windowsCapability.supportFrameRateControl) {
        h->AvgTimePerFrame = REFERENCE_TIME(10000000.0 / capability.maxFPS);
      }
    }

    // Set the sink filter to request this capability
    sink_filter_->SetRequestedCapability(capability);
    // Order the capture device to use this capability
    hr += streamConfig->SetFormat(pmt);

    // Check if this is a DV camera and we need to add MS DV Filter
    if (pmt->subtype == MEDIASUBTYPE_dvsl ||
        pmt->subtype == MEDIASUBTYPE_dvsd ||
        pmt->subtype == MEDIASUBTYPE_dvhd) {
      isDVCamera = true;  // This is a DV camera. Use MS DV filter
    }

    FreeMediaType(pmt);
    pmt = NULL;
  }
  RELEASE_AND_CLEAR(streamConfig);

  if (FAILED(hr)) {
    LOG_INFO("ds","Failed to set capture device output format")
    return -1;
  }

  if (isDVCamera) {
    hr = ConnectDVCamera();
  } else {
    hr = _graphBuilder->ConnectDirect(_outputCapturePin, _inputSendPin, NULL);
  }
  if (hr != S_OK) {
    LOG_INFO("ds","Failed to connect the Capture graph " << hr)
    return -1;
  }
  return 0;
}

int32_t VideoCaptureDS::DisconnectGraph() {

  HRESULT hr = _mediaControl->Stop();
  hr += _graphBuilder->Disconnect(_outputCapturePin);
  hr += _graphBuilder->Disconnect(_inputSendPin);

  // if the DV camera filter exist
  if (_dvFilter) {
    _graphBuilder->Disconnect(_inputDvPin);
    _graphBuilder->Disconnect(_outputDvPin);
  }
  if (hr != S_OK) {
    LOG_INFO("ds","Failed to Stop the Capture device for reconfiguration " << hr)
    return -1;
  }
  return 0;
}

HRESULT VideoCaptureDS::ConnectDVCamera() {

  HRESULT hr = S_OK;

  if (!_dvFilter) {
    hr = CoCreateInstance(CLSID_DVVideoCodec, NULL, CLSCTX_INPROC,
                          IID_IBaseFilter, (void**)&_dvFilter);
    if (hr != S_OK) {
      LOG_INFO("ds","Failed to create the dv decoder: " << hr)
      return hr;
    }
    hr = _graphBuilder->AddFilter(_dvFilter, L"VideoDecoderDV");
    if (hr != S_OK) {
      LOG_INFO("ds","Failed to add the dv decoder to the graph: " << hr)
      return hr;
    }
    _inputDvPin = GetInputPin(_dvFilter);
    if (_inputDvPin == NULL) {
      LOG_INFO("ds","Failed to get input pin from DV decoder")
      return -1;
    }
    _outputDvPin = GetOutputPin(_dvFilter, GUID_NULL);
    if (_outputDvPin == NULL) {
      LOG_INFO("ds","Failed to get output pin from DV decoder")
      return -1;
    }
  }
  hr = _graphBuilder->ConnectDirect(_outputCapturePin, _inputDvPin, NULL);
  if (hr != S_OK) {
    LOG_INFO("ds", "Failed to connect capture device to the dv devoder: "
                     << hr)
    return hr;
  }

  hr = _graphBuilder->ConnectDirect(_outputDvPin, _inputSendPin, NULL);
  if (hr != S_OK) {
    if (hr == HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES)) {
      LOG_INFO("ds","Failed to connect the capture device, busy");
    } else {
      LOG_INFO("ds","Failed to connect capture device to the send graph: "
                       << hr)
    }
  }
  return hr;
}
}  // namespace videocapturemodule
}  // namespace hrtc
