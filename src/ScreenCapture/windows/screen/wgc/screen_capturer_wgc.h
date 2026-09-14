#pragma once

#include <windows.h>
#include <d3d11.h>
#include <roapi.h>
#include <wrl/client.h>

#include <Windows.Graphics.h>
#include <Windows.Graphics.DirectX.h>
#include <Windows.Graphics.DirectX.Direct3D11.interop.h>
#include <windows.foundation.h>
#include <windows.graphics.capture.h>

#include <atomic>
#include <mutex>

#include "ScreenCapture/windows/screen/screen_capture_base.h"

namespace hrtc {
namespace videocapturemodule {

// WGC (Windows Graphics Capture) 屏幕采集 (Win10 1903+):
// 同时支持显示器与窗口源, 通过 Direct3D11CaptureFramePool 以 push 模式回调帧,
// 不支持或初始化失败时由上层组合器回退到 DXGI / GDI
class ScreenCapturerWGC : public ScreenCaptureBase {
 public:
  ScreenCapturerWGC();
  ~ScreenCapturerWGC() override;

 protected:
  int StartInternal() override;
  int StopInternal() override;

 private:
  using CaptureFramePool =
      ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePool;
  using FrameArrivedHandler =
      ABI::Windows::Foundation::ITypedEventHandler<
          ABI::Windows::Graphics::Capture::Direct3D11CaptureFramePool*,
          IInspectable*>;

  // 创建 D3D11 设备并包装为 WinRT IDirect3DDevice, 失败返回 false
  bool InitDirect3DDevice();
  // 帧到达回调(自由线程池 MTA 线程), 拷贝 BGRA 数据并分发
  void OnFrameArrived(CaptureFramePool* sender);
  // 启动过程中任一步失败时清理并返回 -1
  int CleanupFail();

  bool roInitialized_ = false;
  bool framePoolRegistered_ = false;

  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<
      ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>
      winrtDevice_;
  Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>
      item_;
  Microsoft::WRL::ComPtr<CaptureFramePool> framePool_;
  Microsoft::WRL::ComPtr<
      ABI::Windows::Graphics::Capture::IGraphicsCaptureSession>
      session_;
  Microsoft::WRL::ComPtr<FrameArrivedHandler> handler_;
  ABI::Windows::Foundation::EventRegistrationToken token_ = {};

  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
  UINT stagingWidth_ = 0;
  UINT stagingHeight_ = 0;
  std::mutex frameMutex_;
};

}  // namespace videocapturemodule
}  // namespace hrtc
