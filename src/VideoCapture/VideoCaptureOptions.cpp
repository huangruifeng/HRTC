/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "VideoCapture/VideoCaptureOptions.h"


namespace hrtc {

VideoCaptureOptions::VideoCaptureOptions() {}
VideoCaptureOptions::VideoCaptureOptions(const VideoCaptureOptions& options) =
    default;
VideoCaptureOptions::VideoCaptureOptions(VideoCaptureOptions&& options) =
    default;
VideoCaptureOptions::~VideoCaptureOptions() {}

VideoCaptureOptions& VideoCaptureOptions::operator=(
    const VideoCaptureOptions& options) = default;
VideoCaptureOptions& VideoCaptureOptions::operator=(
    VideoCaptureOptions&& options) = default;

void VideoCaptureOptions::Init(Callback* callback) {
#if defined(HRTC_LINUX)
  if (!allow_v4l2_)
    callback->OnInitialized(Status::UNAVAILABLE);
  else
#endif
    callback->OnInitialized(Status::SUCCESS);
}


}  // namespace hrtc
