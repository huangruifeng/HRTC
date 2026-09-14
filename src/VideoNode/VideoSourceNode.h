#pragma once
#include <Node/BaseNode.h>
#include <CameraCapture/VideoDeviceManager.h>
#include <Base/ErrorCode.h>
#include <Base/Thread/DispatchQueue.h>
#include <Headers/MediaInfo.h>
#include <string>
#include <Headers/HrtcEngine.h>
namespace hrtc {
    class VideoSourceNode : public IVideoCaptureSource ,public BaseNode, public IVideoSink{
    public:
        DEFINE_INODE_IMP(m_thread)

        VideoSourceNode();
        ~VideoSourceNode();
        RtcResult InitDevice(const std::string&id) override;
        RtcResult StartCapture() override;
        RtcResult StopCapture() override;

    protected:
        RtcResult StartInternal();
        RtcResult StopInternal();
        void OnVideoFrame(const VideoFrame& frame) override;
    private:
        bool m_start;
        std::shared_ptr<dispatch_task_queue::task_queue> m_thread;
        std::shared_ptr<VideoDevice> m_videoDevice;
        VideoDeviceManager m_deviceManager;
        MediaInfo m_info;
        bool m_firstFrame;
    };
}
