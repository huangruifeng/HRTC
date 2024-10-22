#include <Base/VideoBase/VideoCaptureDefines.h>
using namespace hrtc;

size_t CalcBufferSize(VideoType type, int width, int height) {
  switch (type) {
    case VideoType::kI420:
    case VideoType::kIYUV:
    case VideoType::kYV12:
    case VideoType::kNV12: {
      int half_width = (width + 1) >> 1;
      int half_height = (height + 1) >> 1;
      return width * height + half_width * half_height * 2;
    }
    case VideoType::kRGB565:
    case VideoType::kYUY2:
    case VideoType::kUYVY:
      return width * height * 2;
    case VideoType::kRGB24:
    case VideoType::kBGR24:
      return width * height * 3;
    case VideoType::kBGRA:
    case VideoType::kARGB:
    case VideoType::kABGR:
      return width * height * 4;
    case VideoType::kMJPEG:
    case VideoType::kUnknown:
      break;
  }
  return 0;
}