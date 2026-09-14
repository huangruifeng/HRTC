#include "ScreenCapture/windows/screen/dxgi/screen_capturer_dxgi.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <chrono>
#include <cstring>

#include "ScreenCapture/windows/screen/screen_source_list_win.h"

namespace hrtc {
namespace videocapturemodule {

namespace {

template <typename T>
void SafeRelease(T** pp) {
  if (*pp) {
    (*pp)->Release();
    *pp = nullptr;
  }
}

}  // namespace

ScreenCapturerDXGI::ScreenCapturerDXGI() {}

ScreenCapturerDXGI::~ScreenCapturerDXGI() {
  StopCapture();
  ReleaseDuplication();
}

int ScreenCapturerDXGI::StartInternal() {
  void* handle = nullptr;
  if (!ParseScreenSourceId(source_.sourceId, &handle))
    return -1;
  if (!InitDuplication(handle))
    return -1;

  stop_ = false;
  invalidated_ = false;
  thread_ = std::thread(&ScreenCapturerDXGI::CaptureLoop, this);
  return 0;
}

int ScreenCapturerDXGI::StopInternal() {
  stop_ = true;
  if (thread_.joinable())
    thread_.join();
  stop_ = false;
  return 0;
}

bool ScreenCapturerDXGI::InitDuplication(void* monitor) {
  ReleaseDuplication();

  // 创建 D3D11 设备
  D3D_FEATURE_LEVEL featureLevel;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                 nullptr, 0, D3D11_SDK_VERSION, &device_,
                                 &featureLevel, &context_);
  if (FAILED(hr))
    return false;

  // 遍历输出, 匹配目标显示器
  IDXGIDevice* dxgiDevice = nullptr;
  IDXGIAdapter* adapter = nullptr;
  device_->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void**>(&dxgiDevice));
  if (!dxgiDevice || FAILED(dxgiDevice->GetAdapter(&adapter)) || !adapter) {
    SafeRelease(&adapter);
    SafeRelease(&dxgiDevice);
    ReleaseDuplication();
    return false;
  }

  IDXGIOutput* output = nullptr;
  IDXGIOutput1* output1 = nullptr;
  for (UINT i = 0; adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND;
       ++i) {
    DXGI_OUTPUT_DESC desc;
    if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor) {
      output->QueryInterface(IID_IDXGIOutput1,
                             reinterpret_cast<void**>(&output1));
      SafeRelease(&output);
      break;
    }
    SafeRelease(&output);
  }

  if (output1) {
    hr = output1->DuplicateOutput(device_, &duplication_);
    if (FAILED(hr)) {
      SafeRelease(&duplication_);
    } else {
      DXGI_OUTDUPL_DESC dupDesc;
      duplication_->GetDesc(&dupDesc);
      rotation_ = dupDesc.Rotation;
    }
  }

  SafeRelease(&output1);
  SafeRelease(&adapter);
  SafeRelease(&dxgiDevice);
  return duplication_ != nullptr;
}

void ScreenCapturerDXGI::ReleaseDuplication() {
  SafeRelease(&staging_);
  SafeRelease(&duplication_);
  SafeRelease(&context_);
  SafeRelease(&device_);
}

void ScreenCapturerDXGI::CaptureLoop() {
  const int fps = capability_.maxFPS > 0 ? capability_.maxFPS : 15;
  const auto frameInterval = std::chrono::milliseconds(1000 / fps);

  while (!stop_) {
    const auto frameStart = std::chrono::steady_clock::now();

    std::vector<uint8_t> buffer;
    int width = 0;
    int height = 0;
    if (AcquireFrame(buffer, width, height)) {
      DeliverFrame(buffer.data(), buffer.size(), width, height,
                   VideoPixelFormat::kBGRA, GetTickCount64());
    } else if (invalidated_) {
      // duplication 失效(显示模式变化等), 等待后重建
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      void* handle = nullptr;
      if (ParseScreenSourceId(source_.sourceId, &handle) &&
          InitDuplication(handle)) {
        invalidated_ = false;
      }
      continue;
    }

    const auto elapsed = std::chrono::steady_clock::now() - frameStart;
    if (!stop_ && elapsed < frameInterval)
      std::this_thread::sleep_for(frameInterval - elapsed);
  }
}

bool ScreenCapturerDXGI::AcquireFrame(std::vector<uint8_t>& buffer,
                                      int& width,
                                      int& height) {
  if (!duplication_)
    return false;

  DXGI_OUTDUPL_FRAME_INFO frameInfo;
  IDXGIResource* resource = nullptr;
  HRESULT hr = duplication_->AcquireNextFrame(100, &frameInfo, &resource);
  if (hr == DXGI_ERROR_WAIT_TIMEOUT)
    return false;  // 无新帧
  if (hr == DXGI_ERROR_ACCESS_LOST) {
    invalidated_ = true;
    return false;
  }
  if (FAILED(hr))
    return false;

  ID3D11Texture2D* texture = nullptr;
  resource->QueryInterface(IID_ID3D11Texture2D,
                           reinterpret_cast<void**>(&texture));

  bool success = false;
  if (texture) {
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    // 重建/创建 staging 纹理 (CPU 可读)
    if (!staging_) {
      D3D11_TEXTURE2D_DESC stagingDesc = {};
      stagingDesc.Width = desc.Width;
      stagingDesc.Height = desc.Height;
      stagingDesc.MipLevels = 1;
      stagingDesc.ArraySize = 1;
      stagingDesc.Format = desc.Format;
      stagingDesc.SampleDesc.Count = 1;
      stagingDesc.Usage = D3D11_USAGE_STAGING;
      stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      if (FAILED(device_->CreateTexture2D(&stagingDesc, nullptr, &staging_)))
        staging_ = nullptr;
    }

    if (staging_) {
      context_->CopyResource(staging_, texture);

      D3D11_MAPPED_SUBRESOURCE mapped;
      if (SUCCEEDED(context_->Map(staging_, 0, D3D11_MAP_READ, 0, &mapped))) {
        const size_t rowBytes = static_cast<size_t>(desc.Width) * 4;
        buffer.resize(rowBytes * desc.Height);
        const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
        for (UINT row = 0; row < desc.Height; ++row) {
          memcpy(buffer.data() + row * rowBytes,
                 src + row * mapped.RowPitch, rowBytes);
        }
        context_->Unmap(staging_, 0);

        width = static_cast<int>(desc.Width);
        height = static_cast<int>(desc.Height);
        // 旋转 90/270 时交换宽高, 画面方向由下游处理
        if (rotation_ == DXGI_MODE_ROTATION_ROTATE90 ||
            rotation_ == DXGI_MODE_ROTATION_ROTATE270) {
          const int tmp = width;
          width = height;
          height = tmp;
        }
        success = true;
      }
    }
  }

  SafeRelease(&texture);
  duplication_->ReleaseFrame();
  SafeRelease(&resource);
  return success;
}

}  // namespace videocapturemodule
}  // namespace hrtc
