#pragma once

#include "VideoCommon/VideoRotation.h"
#include <cstdint>
#include <Headers/MediaInfo.h>

namespace hrtc {

// 设备标识/名称最大长度
enum { kVideoCaptureUniqueNameLength = 1024 };  // 设备唯一标识最大长度
enum { kVideoCaptureDeviceNameLength = 256 };   // 设备名称最大长度
enum { kVideoCaptureProductIdLength = 128 };    // 产品 ID 最大长度

// 视频像素格式
enum class VideoPixelFormat {
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

// 按像素格式与宽高计算一帧缓冲大小, 未知/压缩格式返回 0
size_t CalcBufferSize(VideoPixelFormat type, int width, int height);

// 摄像头采集能力
struct VideoCapability {
  int32_t width;
  int32_t height;
  int32_t maxFPS;
  VideoPixelFormat format;
  bool interlaced;

  VideoCapability() {
    width = 0;
    height = 0;
    maxFPS = 0;
    format = VideoPixelFormat::kUnknown;
    interlaced = false;
  }

  // 转换为媒体层视频格式 (供 libyuv 等使用)
  int GetVideoFormat() const {
    switch (format) {
      case VideoPixelFormat::kUnknown:
        return VideoFormat::Unknown;
      case VideoPixelFormat::kI420:
        return VideoFormat::I420;
      case VideoPixelFormat::kIYUV:  // same as VideoPixelFormat::kYV12
      case VideoPixelFormat::kYV12:
        return VideoFormat::IYUV;
      case VideoPixelFormat::kABGR:
        return VideoFormat::ABGR;
      case VideoPixelFormat::kYUY2:
        return VideoFormat::YUY2;
      case VideoPixelFormat::kMJPEG:
        return VideoFormat::MJPEG;
      case VideoPixelFormat::kNV12:
        return VideoFormat::NV12;
      case VideoPixelFormat::kARGB:
        return VideoFormat::ARGB;
      default:
        break;
    }
    return VideoFormat::Unknown;
  }

  bool operator!=(const VideoCapability& other) const {
    if (width != other.width)
      return true;
    if (height != other.height)
      return true;
    if (maxFPS != other.maxFPS)
      return true;
    if (format != other.format)
      return true;
    if (interlaced != other.interlaced)
      return true;
    return false;
  }
  bool operator==(const VideoCapability& other) const {
    return !operator!=(other);
  }
};

}  // namespace hrtc
