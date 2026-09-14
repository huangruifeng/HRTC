#pragma once

#include <cstdint>

#include "VideoCommon/VideoCaptureDefines.h"
#include "VideoCommon/VideoRotation.h"

namespace hrtc {

// 视频帧: 采集产生的原始帧数据, 数据仅在回调返回前有效
class VideoFrame {
 public:
  VideoFrame() = default;
  VideoFrame(const uint8_t* data,
             size_t size,
             int width,
             int height,
             VideoPixelFormat format,
             VideoRotation rotation,
             int64_t timestampMs);

  const uint8_t* data() const { return data_; }
  size_t size() const { return size_; }
  int width() const { return width_; }
  int height() const { return height_; }
  VideoPixelFormat format() const { return format_; }
  VideoRotation rotation() const { return rotation_; }
  // 采集时间戳(毫秒)
  int64_t timestampMs() const { return timestampMs_; }

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  int width_ = 0;
  int height_ = 0;
  VideoPixelFormat format_ = VideoPixelFormat::kUnknown;
  VideoRotation rotation_ = VideoRotation::kVideoRotation_0;
  int64_t timestampMs_ = 0;
};

// 视频帧数据回调接口
class IVideoSink {
 public:
  virtual ~IVideoSink() = default;
  // 帧数据仅在回调返回前有效, 需要保留时请在回调内拷贝
  virtual void OnVideoFrame(const VideoFrame& frame) = 0;
};

}  // namespace hrtc
