#include "VideoRender/VideoRender.h"
#include "windows.h"
#include <memory>
#include "VideoRender/windows/d3d9/d3d9_render.h"
using namespace hrtc;
VideoRenderer* VideoRenderer::CreatePlatformRenderer(void* window,
                                                     size_t width,
                                                     size_t height,
                                                     bool isLocal,
                                                     void* display) {
  if (IsWindow((HWND)window)) {
    RECT windowRect;
    if (GetWindowRect((HWND)window, &windowRect)) {
      width = windowRect.right - windowRect.left;
      height = windowRect.bottom - windowRect.top;
    }
  }

 // todo hrf: support d3d11;
  std::unique_ptr<VideoRenderer> renderer(new D3D9Renderer(width, height));
  if (renderer->Init(window)) {
      return renderer.release();
  }
  return nullptr;
}