#include "ScreenCapture/ScreenCapture.h"

#include <memory>

#include "ScreenCapture/windows/screen/dxgi/screen_capturer_dxgi.h"
#include "ScreenCapture/windows/screen/gdi/screen_capturer_gdi.h"
#include "ScreenCapture/windows/screen/screen_source_list_win.h"
#include "ScreenCapture/windows/screen/wgc/screen_capturer_wgc.h"

namespace hrtc {
namespace videocapturemodule {

namespace {

// 尝试启动指定后端, 失败时先停止再返回 false
bool TryStartBackend(const std::shared_ptr<ScreenCapturer>& capturer,
                     const ScreenSource& source,
                     const VideoCapability& capability) {
  if (capturer->StartCapture(source, capability) == 0)
    return true;
  capturer->StopCapture();
  return false;
}

// 组合采集器: 显示器源 WGC -> DXGI -> GDI; 窗口源 WGC -> GDI
class ScreenCapturerWin : public ScreenCapturer {
 public:
  ScreenCapturerWin()
      : wgc_(std::make_shared<ScreenCapturerWGC>()),
        gdi_(std::make_shared<ScreenCapturerGDI>()),
        dxgi_(std::make_shared<ScreenCapturerDXGI>()) {}

  const ScreenSource& source() const override { return source_; }

  int StartCapture(const ScreenSource& source,
                   const VideoCapability& capability) override {
    source_ = source;
    ScreenCapturer* backend = nullptr;

    // 首选 WGC: 显示器与窗口均支持(Win10 1903+), 失败自动降级
    if (TryStartBackend(wgc_, source, capability)) {
      backend = wgc_.get();
    } else if (source.type == ScreenSourceType::kScreen &&
               TryStartBackend(dxgi_, source, capability)) {
      // 显示器源回退 DXGI Desktop Duplication
      backend = dxgi_.get();
    }

    // 最终回退 GDI, 兼容所有 Windows 版本
    if (!backend) {
      if (!TryStartBackend(gdi_, source, capability))
        return -1;
      backend = gdi_.get();
    }

    backend_ = backend;
    backend->SetVideoSink(sink_);
    return 0;
  }

  int StopCapture() override {
    if (!backend_)
      return 0;
    const int result = backend_->StopCapture();
    backend_ = nullptr;
    return result;
  }

  bool IsCapturing() override { return backend_ && backend_->IsCapturing(); }

  void SetVideoSink(IVideoSink* sink) override {
    sink_ = sink;
    if (backend_)
      backend_->SetVideoSink(sink);
  }

 private:
  std::shared_ptr<ScreenCapturer> wgc_;
  std::shared_ptr<ScreenCapturer> gdi_;
  std::shared_ptr<ScreenCapturer> dxgi_;
  ScreenCapturer* backend_ = nullptr;
  IVideoSink* sink_ = nullptr;
  ScreenSource source_;
};

}  // namespace

std::vector<ScreenSource> EnumerateScreenSources() {
  return EnumerateScreenSourcesWin();
}

std::shared_ptr<ScreenCapturer> CreateScreenCapturer() {
  return std::make_shared<ScreenCapturerWin>();
}

}  // namespace videocapturemodule
}  // namespace hrtc
