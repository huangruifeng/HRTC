/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "VideoCapture/windows/ds/video_capture_ds.h"
#ifdef ENABLE_MEDIA_FOUNDATION
#include "VideoCapture/windows/mf/video_capture_mf.h"
#endif
#include <memory>
#include "VersionHelpers.h"
namespace hrtc {
namespace videocapturemodule {
bool is_support_media_foundation = true;
// static
VideoCaptureModule::DeviceInfo* VideoCaptureImpl::CreateDeviceInfo() {
  // TODO(tommi): Use the Media Foundation version on Vista and up.

#ifdef ENABLE_MEDIA_FOUNDATION
  if (IsWindows10OrGreater()) {
    DeviceInfoMF* mfInfo = DeviceInfoMF::Create();
    if (mfInfo) {
      is_support_media_foundation = true;
      return mfInfo;
    } else {
      is_support_media_foundation = false;
    }
  }
#endif

  return DeviceInfoDS::Create();
}

std::shared_ptr<VideoCaptureModule> VideoCaptureImpl::Create(
    const char* device_id) {
  if (device_id == nullptr)
    return nullptr;

  // TODO(tommi): Use Media Foundation implementation for Vista and up.
#ifdef ENABLE_MEDIA_FOUNDATION
  if(IsWindows10OrGreater() && is_support_media_foundation){
    auto capture = std::make_shared<VideoCaptureMF>();
    if (capture->Init(device_id) != 0) {
      return nullptr;
    }
    return capture;
  }
#endif

  auto capture = std::make_shared<VideoCaptureDS>();
  if (capture->Init(device_id) != 0) {
    return nullptr;
  }
  return capture;
}

}  // namespace videocapturemodule
}  // namespace webrtc
