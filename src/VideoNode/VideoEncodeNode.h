#pragma once
#include <Node/BaseNode.h>
#include <Base/ErrorCode.h>
#include <Base/Thread/DispatchQueue.h>
#include <Headers/HrtcEngine.h>
#include <VideoCodec/VideoEncoder.h>
namespace hrtc {
    class VideoEncodeNode : public IVideoEncoder,public BaseNode {
    public:
        DEFINE_INODE_IMP(m_thread);
        VideoEncodeNode();
        ~VideoEncodeNode();
        virtual RtcResult StartEncode()override;
        virtual RtcResult StopEncode() override;

        virtual void OnData(IPin* pin, const IMediaInfo& sample) override;
    private:
        std::shared_ptr<dispatch_task_queue::task_queue> m_thread;
        std::shared_ptr<dispatch_task_queue::task_queue> m_decodeThread;
        bool m_start;
        std::shared_ptr<VideoEncoder> m_encoder;
    };

}