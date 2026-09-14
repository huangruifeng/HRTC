#include <VideoCommon/VideoCaptureDefines.h>
using namespace hrtc;

size_t hrtc::CalcBufferSize(VideoPixelFormat type, int width, int height) {
  switch (type) {
    case VideoPixelFormat::kI420:
    case VideoPixelFormat::kIYUV:
    case VideoPixelFormat::kYV12:
    case VideoPixelFormat::kNV12: {
      int half_width = (width + 1) >> 1;
      int half_height = (height + 1) >> 1;
      return width * height + half_width * half_height * 2;
    }
    case VideoPixelFormat::kRGB565:
    case VideoPixelFormat::kYUY2:
    case VideoPixelFormat::kUYVY:
      return width * height * 2;
    case VideoPixelFormat::kRGB24:
    case VideoPixelFormat::kBGR24:
      return width * height * 3;
    case VideoPixelFormat::kBGRA:
    case VideoPixelFormat::kARGB:
    case VideoPixelFormat::kABGR:
      return width * height * 4;
    case VideoPixelFormat::kMJPEG:
    case VideoPixelFormat::kUnknown:
      break;
  }
  return 0;
}
