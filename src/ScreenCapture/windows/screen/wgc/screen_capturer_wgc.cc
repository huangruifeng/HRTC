#include "ScreenCapture/windows/screen/wgc/screen_capturer_wgc.h"

#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

#include <cstring>
#include <vector>

#include "ScreenCapture/windows/screen/screen_source_list_win.h"

namespace hrtc {
namespace videocapturemodule {

namespace {

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

namespace abi_capture = ABI::Windows::Graphics::Capture;
namespace abi_d3d = ABI::Windows::Graphics::DirectX::Direct3D11;
namespace abi_foundation = ABI::Windows::Foundation;

// 获取 WinRT 激活工厂
template <typename T>
HRESULT GetFactory(const wchar_t* runtimeClassId, T** factory) {
  return RoGetActivationFactory(HStringReference(runtimeClassId).Get(),
                                IID_PPV_ARGS(factory));
}

// 通过 IClosable 释放 WinRT 对象持有的原生资源
template <typename T>
void CloseWinRtObject(ComPtr<T>& object) {
  if (!object)
    return;
  ComPtr<abi_foundation::IClosable> closable;
  if (SUCCEEDED(object.As(&closable)) && closable)
    closable->Close();
  object.Reset();
}

}  // namespace

ScreenCapturerWGC::ScreenCapturerWGC() {}

ScreenCapturerWGC::~ScreenCapturerWGC() {
  StopCapture();
}

int ScreenCapturerWGC::StartInternal() {
  void* handle = nullptr;
  if (!ParseScreenSourceId(source_.sourceId, &handle))
    return -1;

  // 初始化 WinRT 公寓(多线程), 与 StopInternal 中的 RoUninitialize 成对
  const HRESULT roHr = RoInitialize(RO_INIT_MULTITHREADED);
  roInitialized_ = SUCCEEDED(roHr);  // S_OK 或 S_FALSE(本线程已初始化)
  if (FAILED(roHr) && roHr != RPC_E_CHANGED_MODE)
    return CleanupFail();

  if (!InitDirect3DDevice())
    return CleanupFail();

  // 通过互操作接口按显示器/窗口句柄创建采集项 (Win10 1903+)
  ComPtr<IGraphicsCaptureItemInterop> interop;
  if (FAILED(GetFactory(L"Windows.Graphics.Capture.GraphicsCaptureItem",
                        interop.GetAddressOf())))
    return CleanupFail();

  HRESULT hr;
  if (source_.type == ScreenSourceType::kWindow) {
    hr = interop->CreateForWindow(reinterpret_cast<HWND>(handle),
                                  IID_PPV_ARGS(&item_));
  } else {
    hr = interop->CreateForMonitor(reinterpret_cast<HMONITOR>(handle),
                                   IID_PPV_ARGS(&item_));
  }
  if (FAILED(hr) || !item_)
    return CleanupFail();

  // 采集尺寸: 优先取采集项实际尺寸, 失败回退到源尺寸
  ABI::Windows::Graphics::SizeInt32 size = {};
  if (FAILED(item_->get_Size(&size)) || size.Width <= 0 || size.Height <= 0) {
    size.Width = source_.width > 0 ? source_.width : 1;
    size.Height = source_.height > 0 ? source_.height : 1;
  }

  // 创建自由线程帧池(回调在线程池 MTA 线程, 无需 CoreDispatcher)
  ComPtr<abi_capture::IDirect3D11CaptureFramePoolStatics2> poolStatics;
  if (FAILED(GetFactory(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool",
                        poolStatics.GetAddressOf())))
    return CleanupFail();
  hr = poolStatics->CreateFreeThreaded(
      winrtDevice_.Get(),
      ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized,
      2, size, &framePool_);
  if (FAILED(hr) || !framePool_)
    return CleanupFail();

  // 注册帧到达回调
  handler_ = Microsoft::WRL::Callback<FrameArrivedHandler>(
                 [this](CaptureFramePool* sender,
                        IInspectable*) -> HRESULT {
                   OnFrameArrived(sender);
                   return S_OK;
                 })
                 .Get();
  if (!handler_)
    return CleanupFail();
  hr = framePool_->add_FrameArrived(handler_.Get(), &token_);
  if (FAILED(hr))
    return CleanupFail();
  framePoolRegistered_ = true;

  // 创建并启动采集会话(由帧池创建会话对象)
  hr = framePool_->CreateCaptureSession(item_.Get(), &session_);
  if (FAILED(hr) || !session_)
    return CleanupFail();

  // 可选: 关闭光标捕获 / 黄色边框 (接口在 Win10 2004+ 可用, 不支持时忽略)
  ComPtr<abi_capture::IGraphicsCaptureSession2> session2;
  if (SUCCEEDED(session_.As(&session2)) && session2)
    session2->put_IsCursorCaptureEnabled(false);
  ComPtr<abi_capture::IGraphicsCaptureSession3> session3;
  if (SUCCEEDED(session_.As(&session3)) && session3)
    session3->put_IsBorderRequired(false);

  hr = session_->StartCapture();
  if (FAILED(hr))
    return CleanupFail();

  return 0;
}

int ScreenCapturerWGC::StopInternal() {
  if (framePool_ && framePoolRegistered_) {
    framePool_->remove_FrameArrived(token_);
    framePoolRegistered_ = false;
  }
  handler_.Reset();

  CloseWinRtObject(session_);
  CloseWinRtObject(framePool_);
  CloseWinRtObject(item_);

  std::lock_guard<std::mutex> lock(frameMutex_);
  staging_.Reset();
  stagingWidth_ = 0;
  stagingHeight_ = 0;
  winrtDevice_.Reset();
  context_.Reset();
  device_.Reset();

  if (roInitialized_) {
    RoUninitialize();
    roInitialized_ = false;
  }
  return 0;
}

bool ScreenCapturerWGC::InitDirect3DDevice() {
  D3D_FEATURE_LEVEL featureLevel;
  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device_,
      &featureLevel, &context_);
  if (FAILED(hr)) {
    // 无可用硬件设备时回退 WARP 软件驱动
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                           D3D11_SDK_VERSION, &device_, &featureLevel,
                           &context_);
    if (FAILED(hr))
      return false;
  }

  ComPtr<IDXGIDevice> dxgiDevice;
  if (FAILED(device_.As(&dxgiDevice)) || !dxgiDevice)
    return false;

  ComPtr<IInspectable> inspectable;
  hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), &inspectable);
  if (FAILED(hr) || !inspectable)
    return false;

  hr = inspectable.As(&winrtDevice_);
  return SUCCEEDED(hr) && winrtDevice_;
}

void ScreenCapturerWGC::OnFrameArrived(CaptureFramePool* sender) {
  if (!sender || !capturing_)
    return;

  ComPtr<abi_capture::IDirect3D11CaptureFrame> frame;
  if (FAILED(sender->TryGetNextFrame(&frame)) || !frame)
    return;

  ComPtr<abi_d3d::IDirect3DSurface> surface;
  if (SUCCEEDED(frame->get_Surface(&surface)) && surface) {
    ComPtr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>
        access;
    if (SUCCEEDED(surface.As(&access)) && access) {
      ComPtr<ID3D11Texture2D> texture;
      if (SUCCEEDED(access->GetInterface(IID_PPV_ARGS(&texture))) && texture) {
        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);

        std::lock_guard<std::mutex> lock(frameMutex_);
        if (device_ && context_) {
          // 尺寸变化时重建 staging 纹理
          if (!staging_ || stagingWidth_ != desc.Width ||
              stagingHeight_ != desc.Height) {
            staging_.Reset();
            D3D11_TEXTURE2D_DESC stagingDesc = {};
            stagingDesc.Width = desc.Width;
            stagingDesc.Height = desc.Height;
            stagingDesc.MipLevels = 1;
            stagingDesc.ArraySize = 1;
            stagingDesc.Format = desc.Format;
            stagingDesc.SampleDesc.Count = 1;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (SUCCEEDED(
                    device_->CreateTexture2D(&stagingDesc, nullptr, &staging_))) {
              stagingWidth_ = desc.Width;
              stagingHeight_ = desc.Height;
            } else {
              staging_.Reset();
            }
          }

          if (staging_) {
            context_->CopyResource(staging_.Get(), texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0,
                                        &mapped))) {
              const size_t rowBytes = static_cast<size_t>(desc.Width) * 4;
              std::vector<uint8_t> buffer(rowBytes * desc.Height);
              const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
              for (UINT row = 0; row < desc.Height; ++row) {
                memcpy(buffer.data() + row * rowBytes, src + row * mapped.RowPitch,
                       rowBytes);
              }
              context_->Unmap(staging_.Get(), 0);

              DeliverFrame(buffer.data(), buffer.size(),
                           static_cast<int>(desc.Width),
                           static_cast<int>(desc.Height),
                           VideoPixelFormat::kBGRA, GetTickCount64());
            }
          }
        }
      }
    }
  }

  // 归还帧到底层缓冲池, 避免池耗尽
  CloseWinRtObject(frame);
  CloseWinRtObject(surface);
}

int ScreenCapturerWGC::CleanupFail() {
  StopInternal();
  return -1;
}

}  // namespace videocapturemodule
}  // namespace hrtc
