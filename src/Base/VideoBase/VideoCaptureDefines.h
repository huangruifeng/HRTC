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

#include"Base/VideoBase/VideoRotation.h"
#include <cstdint>
namespace hrtc {

enum {
  kVideoCaptureUniqueNameLength = 1024
};  // Max unique capture device name lenght
enum { kVideoCaptureDeviceNameLength = 256 };  // Max capture device name lenght
enum { kVideoCaptureProductIdLength = 128 };   // Max product id length

enum class VideoType {
  kUnknown = 0,
  kI420,
  kIYUV,
  kRGB24,
  kBGR24,
  kARGB,
  kABGR,
  kRGB565,
  kYUY2,
  kYV12,
  kUYVY,
  kMJPEG,
  kBGRA,
  kNV12,
};

size_t CalcBufferSize(VideoType type, int width, int height);

struct VideoCaptureCapability {
  int32_t width;
  int32_t height;
  int32_t maxFPS;
  VideoType videoType;
  bool interlaced;

  VideoCaptureCapability() {
    width = 0;
    height = 0;
    maxFPS = 0;
    videoType = VideoType::kUnknown;
    interlaced = false;
  }
  bool operator!=(const VideoCaptureCapability& other) const {
    if (width != other.width)
      return true;
    if (height != other.height)
      return true;
    if (maxFPS != other.maxFPS)
      return true;
    if (videoType != other.videoType)
      return true;
    if (interlaced != other.interlaced)
      return true;
    return false;
  }
  bool operator==(const VideoCaptureCapability& other) const {
    return !operator!=(other);
  }
};

}  // namespace hrtc