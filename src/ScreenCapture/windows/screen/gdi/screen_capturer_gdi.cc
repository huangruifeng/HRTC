#include "ScreenCapture/windows/screen/gdi/screen_capturer_gdi.h"

#include <windows.h>

#include <chrono>
#include <cstring>

#include "ScreenCapture/windows/screen/screen_source_list_win.h"

namespace hrtc {
namespace videocapturemodule {

ScreenCapturerGDI::ScreenCapturerGDI() {}

ScreenCapturerGDI::~ScreenCapturerGDI() {
  StopCapture();
}

int ScreenCapturerGDI::StartInternal() {
  window_ = nullptr;
  if (source_.type == ScreenSourceType::kWindow) {
    void* handle = nullptr;
    if (!ParseScreenSourceId(source_.sourceId, &handle))
      return -1;
    HWND hwnd = reinterpret_cast<HWND>(handle);
    if (!IsWindow(hwnd))
      return -1;
    window_ = hwnd;
  }

  targetWidth_ = capability_.width > 0 ? capability_.width : source_.width;
  targetHeight_ = capability_.height > 0 ? capability_.height : source_.height;
  if (targetWidth_ <= 0 || targetHeight_ <= 0)
    return -1;

  stop_ = false;
  thread_ = std::thread(&ScreenCapturerGDI::CaptureLoop, this);
  return 0;
}

int ScreenCapturerGDI::StopInternal() {
  stop_ = true;
  if (thread_.joinable())
    thread_.join();
  stop_ = false;
  return 0;
}

void ScreenCapturerGDI::CaptureLoop() {
  const int fps = capability_.maxFPS > 0 ? capability_.maxFPS : 15;
  const auto frameInterval = std::chrono::milliseconds(1000 / fps);

  while (!stop_) {
    const auto frameStart = std::chrono::steady_clock::now();

    std::vector<uint8_t> buffer;
    int width = 0;
    int height = 0;
    if (CaptureFrame(buffer, width, height)) {
      DeliverFrame(buffer.data(), buffer.size(), width, height,
                   VideoPixelFormat::kBGRA, GetTickCount64());
    }

    const auto elapsed = std::chrono::steady_clock::now() - frameStart;
    if (!stop_ && elapsed < frameInterval)
      std::this_thread::sleep_for(frameInterval - elapsed);
  }
}

bool ScreenCapturerGDI::CaptureFrame(std::vector<uint8_t>& buffer,
                                     int& width,
                                     int& height) {
  HDC screenDc = GetDC(nullptr);
  if (!screenDc)
    return false;

  HDC memDc = CreateCompatibleDC(screenDc);
  if (!memDc) {
    ReleaseDC(nullptr, screenDc);
    return false;
  }

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = targetWidth_;
  bmi.bmiHeader.biHeight = -targetHeight_;  // 自顶向下
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP bitmap =
      CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bitmap) {
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return false;
  }

  HGDIOBJ oldObject = SelectObject(memDc, bitmap);

  bool success = false;
  if (window_) {
    HWND hwnd = reinterpret_cast<HWND>(window_);
    if (IsWindow(hwnd)) {
      // 优先 PrintWindow(Win8.1+ 支持 PW_RENDERFULLCONTENT), 失败回退窗口 DC
      if (!PrintWindow(hwnd, memDc, PW_RENDERFULLCONTENT)) {
        HDC windowDc = GetWindowDC(hwnd);
        if (windowDc) {
          StretchBlt(memDc, 0, 0, targetWidth_, targetHeight_, windowDc, 0, 0,
                     source_.width, source_.height, SRCCOPY | CAPTUREBLT);
          ReleaseDC(hwnd, windowDc);
        }
      }
      success = true;
    }
  } else {
    // 整屏采集
    BitBlt(memDc, 0, 0, targetWidth_, targetHeight_, screenDc, 0, 0,
           SRCCOPY | CAPTUREBLT);
    success = true;
  }

  GdiFlush();

  if (success) {
    const size_t stride = static_cast<size_t>(targetWidth_) * 4;
    buffer.resize(stride * static_cast<size_t>(targetHeight_));
    memcpy(buffer.data(), bits, buffer.size());
    width = targetWidth_;
    height = targetHeight_;
  }

  SelectObject(memDc, oldObject);
  DeleteObject(bitmap);
  DeleteDC(memDc);
  ReleaseDC(nullptr, screenDc);
  return success;
}

}  // namespace videocapturemodule
}  // namespace hrtc
