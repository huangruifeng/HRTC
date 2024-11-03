#pragma once
#include <Headers/MediaInfo.h>
#include <memory>
#include <string>
namespace hrtc{
    class VideoEncoder {
    public:
        virtual ~VideoEncoder(){}

        virtual bool Encode(const MediaInfo& input,MediaInfo& output) = 0;
    };

    static std::shared_ptr<VideoEncoder> CreateEncoder(const std::string& param);
}