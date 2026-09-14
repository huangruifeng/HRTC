#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "ScreenCapture/windows/screen/screen_capture_base.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct IDXGIOutputDuplication;
enum DXGI_MODE_ROTATION;

namespace hrtc {
namespace videocapturemodule {

// DXGI Desktop Duplication 屏幕采集 (Win8+), 仅支持显示器源
class ScreenCapturerDXGI : public ScreenCaptureBase {
 public:
  ScreenCapturerDXGI();
  ~ScreenCapturerDXGI() override;

 protected:
  int StartInternal() override;
  int StopInternal() override;

 private:
  void CaptureLoop();
  // 初始化 duplication, 失败返回 false
  bool InitDuplication(void* monitor);
  void ReleaseDuplication();
  // 获取一帧 BGRA 数据, 返回是否成功
  bool AcquireFrame(std::vector<uint8_t>& buffer, int& width, int& height);

  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> invalidated_{false};

  ID3D11Device* device_ = nullptr;
  ID3D11DeviceContext* context_ = nullptr;
  IDXGIOutputDuplication* duplication_ = nullptr;
  ID3D11Texture2D* staging_ = nullptr;
  int rotation_ = 0;  // DXGI_MODE_ROTATION
};

}  // namespace videocapturemodule
}  // namespace hrtc
