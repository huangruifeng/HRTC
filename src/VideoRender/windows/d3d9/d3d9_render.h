#pragma once
#include "VideoRender/VideoRender.h"
#include <Windows.h>
#include <d3d9.h>
#include <Base/HrtcRefPtr.h>
#include <Base/Thread/DispatchQueue.h>
namespace hrtc{

class D3D9Renderer : public VideoRenderer {
 public:
  D3D9Renderer(size_t width, size_t height);
  virtual ~D3D9Renderer();
  virtual bool Init(void* window) override;
  virtual void EnableMirror(bool enable) override{mirror_ = enable;};;
  void GetActualSize(size_t& width, size_t& height) override;

  virtual int RenderFrame(const IMediaInfo& frame)override;

 private:
     bool RenderYUVSurface(const IMediaInfo& frame,
         IDirect3DSurface9* surface,
         int renderWidth,
         int renderHeight);
     bool RenderRGBSurface(const IMediaInfo& frame,
         IDirect3DSurface9* surface,
         int renderRidth,
         int renderHeight);
  size_t width_;
  size_t height_;
  size_t surfaceWidth_;
  size_t surfaceHeight_;
  size_t backbufferWidth_;
  size_t backbufferHeight_;
  int64_t last_clear_color_ms_;
  RECT last_present_rect_;
  bool mirror_;

  HWND hwnd_;
  bool isD3DI_;
  hrtc::rtc_refptr<IDirect3DDevice9Ex> d3d_device_;

  hrtc::rtc_refptr<IDirect3DSurface9> surface_;
  hrtc::rtc_refptr<IDirect3DSurface9> rt_surface_;

  bool initialized_;
  D3DFORMAT surface_format_;
  // d3d_device reset needs to be called in the same thread as d3d_device
  // creation, so wrap it up in a single task queue
  static std::shared_ptr<dispatch_task_queue::task_queue> d3dCreationQueue_;
  static rtc_refptr<IDirect3D9Ex> d3dGloba_;
  std::shared_ptr<dispatch_task_queue::task_queue> GetCreationTaskQueue();
};
}