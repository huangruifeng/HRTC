#pragma once
#include <Node/BaseNode.h>
#include <VideoCapture/VideoCaptureFactory.h>
#include <Base/ErrorCode.h>
#include <Base/Thread/DispatchQueue.h>
#include <Headers/MediaInfo.h>
#include <string>
#include <Headers/HrtcEngine.h>
namespace hrtc {
    class VideoSourceNode : public IVideoCaptureSource ,public BaseNode, public  RawVideoSinkInterface{
    public:
        VideoSourceNode();
        ~VideoSourceNode();
        RtcResult InitDevice(const std::string&id) override;
        RtcResult StartCapture() override;
        RtcResult StopCapture() override;

        RtcResult Connect(INode* node) override;
        RtcResult Disconnect(INode* node) override;
    protected:
        RtcResult StartInternal();
        RtcResult StopInternal();
        virtual int32_t OnRawFrame(uint8_t* videoFrame,
            size_t videoFrameLength,
            const VideoCaptureCapability& frameInfo,
            VideoRotation rotation,
            int64_t captureTime) override;
    private:
        bool m_start;
        std::shared_ptr<dispatch_task_queue::task_queue> m_thread;
        std::shared_ptr<VideoCaptureModule> m_videoCap;
        MediaInfo m_info;
        bool m_firstFrame;
    };
}