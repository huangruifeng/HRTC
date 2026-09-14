#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "ScreenCapture/windows/screen/screen_capture_base.h"

namespace hrtc {
namespace videocapturemodule {

// GDI 屏幕采集: BitBlt(屏幕) / PrintWindow(窗口), 兼容所有 Windows 版本
class ScreenCapturerGDI : public ScreenCaptureBase {
 public:
  ScreenCapturerGDI();
  ~ScreenCapturerGDI() override;

 protected:
  int StartInternal() override;
  int StopInternal() override;

 private:
  void CaptureLoop();
  // 抓取一帧 BGRA 数据, 返回是否成功
  bool CaptureFrame(std::vector<uint8_t>& buffer, int& width, int& height);

  std::thread thread_;
  std::atomic<bool> stop_{false};
  int targetWidth_ = 0;
  int targetHeight_ = 0;
  void* window_ = nullptr;  // HWND, 窗口源有效
};

}  // namespace videocapturemodule
}  // namespace hrtc
