#pragma once
#include <Node/BaseNode.h>
#include "VideoRender/VideoRender.h"
#include <Base/ErrorCode.h>
#include <Base/Thread/DispatchQueue.h>
#include <Headers/HrtcEngine.h>
#include <string>
#include <atomic>
#include <mutex>
namespace hrtc {
    class VideoSinkNode : public IVideoRenderSink ,public BaseNode {
    public:
        virtual RtcResult Init(void* hwnd) override;
        virtual RtcResult Deinit() override;
        RtcResult Connect(INode* node) override;
        RtcResult Disconnect(INode* node) override;
        VideoSinkNode();
        ~VideoSinkNode();

        virtual void OnData(IPin* pin, const IMediaInfo& sample) override;
    private:
        std::shared_ptr<VideoRenderer> m_videoRender;
        std::shared_ptr<dispatch_task_queue::task_queue> m_thread;
        bool m_init;
        std::mutex m_mutex;
    };

}