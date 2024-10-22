/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// This file contains interfaces used for creating the VideoCaptureModule
// and DeviceInfo.

#pragma once
#include "VideoCapture/VideoCapture.h"
#include <memory>
namespace hrtc {

class VideoCaptureOptions;

class VideoCaptureFactory {
 public:
  // Create a video capture module object
  // id - unique identifier of this video capture module object.
  // deviceUniqueIdUTF8 - name of the device.
  //                      Available names can be found by using GetDeviceName
  static std::shared_ptr<VideoCaptureModule> Create(
      const char* deviceUniqueIdUTF8);
  static std::shared_ptr<VideoCaptureModule> Create(
      VideoCaptureOptions* options,
      const char* deviceUniqueIdUTF8);

  static VideoCaptureModule::DeviceInfo* CreateDeviceInfo();
  static VideoCaptureModule::DeviceInfo* CreateDeviceInfo(
      VideoCaptureOptions* options);

 private:
     ~VideoCaptureFactory()=default;
};

}  // namespace hrtc

