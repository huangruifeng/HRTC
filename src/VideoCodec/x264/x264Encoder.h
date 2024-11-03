#pragma once
#include <VideoCodec/VideoEncoder.h>
extern "C" {
#include <x264.h>
}
namespace hrtc{
    class X264VideoEncoder : public VideoEncoder {
    public:
        ~X264VideoEncoder();
        X264VideoEncoder();
        bool Encode(const MediaInfo& input, MediaInfo& output)override;
    private:
        bool InitCodec();
        void DeInitCodec();
    private:
        int m_width;
        int m_height;
        x264_param_t m_encoderParam;
        x264_t* m_x264;
        x264_picture_t m_x264Picture;
    };
}