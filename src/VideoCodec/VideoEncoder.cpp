#pragma once
#include "VideoEncoder.h"
#include "x264/x264Encoder.h"
namespace hrtc{

    std::shared_ptr<VideoEncoder> CreateEncoder(const std::string& param)
    {
        return std::make_shared<X264VideoEncoder>();
    }
}