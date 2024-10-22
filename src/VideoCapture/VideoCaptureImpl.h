/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#pragma once

/*
 * video_capture_impl.h
 */

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <mutex>
#include "VideoCapture/VideoCapture.h"
#include "Base/VideoBase/VideoCaptureConfig.h"

namespace hrtc {

class VideoCaptureOptions;

namespace videocapturemodule {
// Class definitions
class VideoCaptureImpl : public VideoCaptureModule {
 public:
  /*
   *   Create a video capture module object
   *
   *   id              - unique identifier of this video capture module object
   *   deviceUniqueIdUTF8 -  name of the device. Available names can be found by
   * using GetDeviceName
   */
  static std::shared_ptr<VideoCaptureModule> Create(
      const char* deviceUniqueIdUTF8);
  static std::shared_ptr<VideoCaptureModule> Create(
      VideoCaptureOptions* options,
      const char* deviceUniqueIdUTF8);

  static DeviceInfo* CreateDeviceInfo();
  static DeviceInfo* CreateDeviceInfo(VideoCaptureOptions* options);

  // Helpers for converting between (integral) degrees and
  // VideoRotation values.  Return 0 on success.
  static int32_t RotationFromDegrees(int degrees, VideoRotation* rotation);
  static int32_t RotationInDegrees(VideoRotation rotation, int* degrees);

  // Call backs
  virtual void RegisterCaptureDataCallback(
      RawVideoSinkInterface* dataCallback) override;
  void DeRegisterCaptureDataCallback() override;

  int32_t SetCaptureRotation(VideoRotation rotation) override;
  bool SetApplyRotation(bool enable) override;
  bool GetApplyRotation() override;

  const char* CurrentDeviceName() const override;

  // `capture_time` must be specified in NTP time format in milliseconds.
  int32_t IncomingFrame(uint8_t* videoFrame,
                        size_t videoFrameLength,
                        const VideoCaptureCapability& frameInfo,
                        int64_t captureTime = 0);

  // Platform dependent
  int32_t StartCapture(const VideoCaptureCapability& capability) override;
  int32_t StopCapture() override;
  bool CaptureStarted() override;
  int32_t CaptureSettings(VideoCaptureCapability& /*settings*/) override;

 protected:
  VideoCaptureImpl();
  ~VideoCaptureImpl() override;

  // Calls to the public API must happen on a single thread.
  // RaceChecker for members that can be accessed on the API thread while
  // capture is not happening, and on a callback thread otherwise.
  // current Device unique name;
  char* _deviceUniqueId ;
  std::mutex api_lock_;
  // Should be set by platform dependent code in StartCapture.
  VideoCaptureCapability _requestedCapability;

 private:
  void UpdateFrameCount();
  uint32_t CalculateFrameRate(int64_t now_ns);
  //int32_t DeliverCapturedFrame(VideoFrame& captureFrame);
  void DeliverRawFrame(uint8_t* videoFrame,
                       size_t videoFrameLength,
                       const VideoCaptureCapability& frameInfo,
                       int64_t captureTime);

  // last time the module process function was called.
  int64_t _lastProcessTimeNanos;
  // last time the frame rate callback function was called.
  int64_t _lastFrameRateCallbackTimeNanos ;

  RawVideoSinkInterface* _rawDataCallBack ;

  int64_t _lastProcessFrameTimeNanos;
  // timestamp for local captured frames
  int64_t _incomingFrameTimesNanos[kFrameRateCountHistorySize] ;
  // Set if the frame should be rotated by the capture module.
  VideoRotation _rotateFrame;

  // Indicate whether rotation should be applied before delivered externally.
  bool apply_rotation_ ;
};
}  // namespace videocapturemodule
}  // namespace hrtc