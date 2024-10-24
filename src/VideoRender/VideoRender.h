#pragma once
#include<cstdint>
#include <Headers/MediaInfo.h>
namespace hrtc
{
class VideoRenderer {
public:
  static VideoRenderer* CreateNullRenderer();

  // Returns a renderer rendering to a platform specific window if possible,
  // NULL if none can be created.
  // Creates a platform-specific renderer if possible, returns NULL if a
  // platform renderer could not be created. This occurs, for instance, when
  // running without an X environment on Linux.
  static VideoRenderer* CreatePlatformRenderer(void* window,
                                               size_t width, size_t height, bool isLocal = false, void* display = nullptr);

  // Destroy a platform-specific renderer.
  static void Destroy(VideoRenderer* renderer);

  // Enable render mirror.
  virtual void EnableMirror(bool enable = true) {}

  virtual void GetActualSize(size_t& width,size_t &height) { 
    width = 0;
    height = 0;
  }

  virtual void* GetWindowView() const { return nullptr; }

  virtual bool Init(void* window) { return true; }

  virtual int RenderFrame(const IMediaInfo& frame){};

  // By default, VideoRenderer will present video. Through disable video
  // present, discrete app will present video frame instead.
  static void DisableVideoPresent() { present_ = false; }

  virtual ~VideoRenderer() {}

 protected:
  VideoRenderer() {}
  static bool                 present_;
};
} // namespace hrtc