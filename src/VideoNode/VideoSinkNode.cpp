#include "VideoNode/VideoSinkNode.h"
using namespace hrtc;

RtcResult hrtc::VideoSinkNode::Init(void* hwnd)
{
    if (hwnd) {
        std::lock_guard<std::mutex> lock (m_mutex);
        m_videoRender.reset(VideoRenderer::CreatePlatformRenderer(hwnd, 0, 0));

        if (m_videoRender) {
            m_init = true;
            return HRTC_CODE_OK;
        }
    }
    return hrtc::HRTC_CODE_ERROR_INVALID_ARG;
}

RtcResult hrtc::VideoSinkNode::Deinit()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_init = false;
    m_videoRender.reset();
    return HRTC_CODE_OK;
}

RtcResult hrtc::VideoSinkNode::Connect(INode* node)
{
    RtcResult res = HRTC_CODE_ERROR_THREAD_NULLPTR;
    if (m_thread) {
        m_thread->sync([this, &res, node] {
            res = ConnectDefault(dynamic_cast<BaseNode*>(node));
        });
    }
    return res;
}

RtcResult hrtc::VideoSinkNode::Disconnect(INode* node)
{
    RtcResult res = HRTC_CODE_ERROR_THREAD_NULLPTR;
    if (m_thread) {
        m_thread->sync([this, &res, node] {
            res = Disconnect(dynamic_cast<BaseNode*>(node));
        });
    }
    return res;
}

hrtc::VideoSinkNode::VideoSinkNode():BaseNode(SINK, VIDEO),m_thread(dispatch_task_queue::task_queue::current()),m_init(false)
{
}

hrtc::VideoSinkNode::~VideoSinkNode()
{
}

void hrtc::VideoSinkNode::OnData(IPin* pin, const IMediaInfo& sample)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_init) {
        m_videoRender->RenderFrame(sample);
    }
}
