#include "VideoRender/VideoRender.h"
using namespace hrtc;
class NullRenderer : public VideoRenderer {
    
public:
  NullRenderer(){}
private:
};

VideoRenderer* VideoRenderer::CreateNullRenderer() {
    return new NullRenderer();
}

void VideoRenderer::Destroy(VideoRenderer* renderer) {
  if ( nullptr != renderer ) {
    delete renderer;
  }
}