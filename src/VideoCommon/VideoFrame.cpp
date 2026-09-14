#include "VideoCommon/VideoFrame.h"

namespace hrtc {

VideoFrame::VideoFrame(const uint8_t* data,
                       size_t size,
                       int width,
                       int height,
                       VideoPixelFormat format,
                       VideoRotation rotation,
                       int64_t timestampMs)
    : data_(data),
      size_(size),
      width_(width),
      height_(height),
      format_(format),
      rotation_(rotation),
      timestampMs_(timestampMs) {}

}  // namespace hrtc
