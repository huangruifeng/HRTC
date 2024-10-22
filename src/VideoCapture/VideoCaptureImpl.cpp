/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "VideoCapture/VideoCaptureImpl.h"
#include "Base/TimeUtils.h"
#include <stdlib.h>
#include <string.h>

//#include "third_party/libyuv/include/libyuv.h"

namespace hrtc {
namespace videocapturemodule {

const char* VideoCaptureImpl::CurrentDeviceName() const {
  return _deviceUniqueId;
}

// static
int32_t VideoCaptureImpl::RotationFromDegrees(int degrees,
                                              VideoRotation* rotation) {
  switch (degrees) {
    case 0:
      *rotation = kVideoRotation_0;
      return 0;
    case 90:
      *rotation = kVideoRotation_90;
      return 0;
    case 180:
      *rotation = kVideoRotation_180;
      return 0;
    case 270:
      *rotation = kVideoRotation_270;
      return 0;
    default:
      return -1;
      ;
  }
}

// static
int32_t VideoCaptureImpl::RotationInDegrees(VideoRotation rotation,
                                            int* degrees) {
  switch (rotation) {
    case kVideoRotation_0:
      *degrees = 0;
      return 0;
    case kVideoRotation_90:
      *degrees = 90;
      return 0;
    case kVideoRotation_180:
      *degrees = 180;
      return 0;
    case kVideoRotation_270:
      *degrees = 270;
      return 0;
  }
  return -1;
}

VideoCaptureImpl::VideoCaptureImpl()
    : _deviceUniqueId(NULL),
      _requestedCapability(),
      _lastProcessTimeNanos(hrtc::TimeNanos()),
      _lastFrameRateCallbackTimeNanos(hrtc::TimeNanos()),
      _rawDataCallBack(NULL),
      _lastProcessFrameTimeNanos(hrtc::TimeNanos()),
      _rotateFrame(kVideoRotation_0),
      apply_rotation_(false) {
  _requestedCapability.width = kDefaultWidth;
  _requestedCapability.height = kDefaultHeight;
  _requestedCapability.maxFPS = 30;
  _requestedCapability.videoType = VideoType::kI420;
  memset(_incomingFrameTimesNanos, 0, sizeof(_incomingFrameTimesNanos));
}

VideoCaptureImpl::~VideoCaptureImpl() {
  DeRegisterCaptureDataCallback();
  if (_deviceUniqueId)
    delete[] _deviceUniqueId;
}


void VideoCaptureImpl::RegisterCaptureDataCallback(
    RawVideoSinkInterface* dataCallBack) {
  std::lock_guard<std::mutex> lock(api_lock_);
  _rawDataCallBack = dataCallBack;
}

void VideoCaptureImpl::DeRegisterCaptureDataCallback() {
  std::lock_guard<std::mutex> lock(api_lock_);
  _rawDataCallBack = NULL;
}

void VideoCaptureImpl::DeliverRawFrame(uint8_t* videoFrame,
                                       size_t videoFrameLength,
                                       const VideoCaptureCapability& frameInfo,
                                       int64_t captureTime) {

  UpdateFrameCount();
  _rawDataCallBack->OnRawFrame(videoFrame, videoFrameLength, frameInfo,
                               _rotateFrame, captureTime);
}

int32_t VideoCaptureImpl::IncomingFrame(uint8_t* videoFrame,
                                        size_t videoFrameLength,
                                        const VideoCaptureCapability& frameInfo,
                                        int64_t captureTime /*=0*/) {
  std::lock_guard<std::mutex> lock(api_lock_);

  const int32_t width = frameInfo.width;
  const int32_t height = frameInfo.height;

  //TRACE_EVENT1("webrtc", "VC::IncomingFrame", "capture_time", captureTime);

  if (_rawDataCallBack) {
    DeliverRawFrame(videoFrame, videoFrameLength, frameInfo, captureTime);
    return 0;
  }

  return 0;
}

int32_t VideoCaptureImpl::StartCapture(
    const VideoCaptureCapability& capability) {
  _requestedCapability = capability;
  return -1;
}

int32_t VideoCaptureImpl::StopCapture() {
  return -1;
}

bool VideoCaptureImpl::CaptureStarted() {
  return false;
}

int32_t VideoCaptureImpl::CaptureSettings(
    VideoCaptureCapability& /*settings*/) {
  return -1;
}

int32_t VideoCaptureImpl::SetCaptureRotation(VideoRotation rotation) {
  std::lock_guard<std::mutex> lock(api_lock_);
  _rotateFrame = rotation;
  return 0;
}

bool VideoCaptureImpl::SetApplyRotation(bool enable) {
  std::lock_guard<std::mutex> lock(api_lock_);
  apply_rotation_ = enable;
  return true;
}

bool VideoCaptureImpl::GetApplyRotation() {
  std::lock_guard<std::mutex> lock(api_lock_);
  return apply_rotation_;
}

void VideoCaptureImpl::UpdateFrameCount() {

  if (_incomingFrameTimesNanos[0] / hrtc::kNumNanosecsPerMicrosec == 0) {
    // first no shift
  } else {
    // shift
    for (int i = (kFrameRateCountHistorySize - 2); i >= 0; --i) {
      _incomingFrameTimesNanos[i + 1] = _incomingFrameTimesNanos[i];
    }
  }
  _incomingFrameTimesNanos[0] = hrtc::TimeNanos();
}

uint32_t VideoCaptureImpl::CalculateFrameRate(int64_t now_ns) {
  int32_t num = 0;
  int32_t nrOfFrames = 0;
  for (num = 1; num < (kFrameRateCountHistorySize - 1); ++num) {
    if (_incomingFrameTimesNanos[num] <= 0 ||
        (now_ns - _incomingFrameTimesNanos[num]) /
                hrtc::kNumNanosecsPerMillisec >
            kFrameRateHistoryWindowMs) {  // don't use data older than 2sec
      break;
    } else {
      nrOfFrames++;
    }
  }
  if (num > 1) {
    int64_t diff = (now_ns - _incomingFrameTimesNanos[num - 1]) /
                   hrtc::kNumNanosecsPerMillisec;
    if (diff > 0) {
      return uint32_t((nrOfFrames * 1000.0f / diff) + 0.5f);
    }
  }

  return nrOfFrames;
}
}  // namespace videocapturemodule
}  // namespace hrtc
