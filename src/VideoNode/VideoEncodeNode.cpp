#include "VideoEncodeNode.h"

using namespace hrtc;

hrtc::VideoEncodeNode::VideoEncodeNode():BaseNode(FILTER, VIDEO)
                                        ,m_thread(dispatch_task_queue::task_queue::current())
                                        ,m_start(false)
{
}

hrtc::VideoEncodeNode::~VideoEncodeNode()
{
    StopEncode();
}

RtcResult hrtc::VideoEncodeNode::StartEncode()
{
    RtcResult res = HRTC_CODE_WARNING_WRONG_STATE;
    m_thread->sync([this,&res] {
        if (!m_start) {
            m_encoder = VideoEncoder::CreateEncoder("x264");
            m_decodeThread = dispatch_task_queue::create("decode_thread");
            m_decodeThread->sync([&res] {
                res = HRTC_CODE_OK;
            });
        }
        m_start = true;
    });
    return res;
}

RtcResult hrtc::VideoEncodeNode::StopEncode()
{
    RtcResult res = HRTC_CODE_WARNING_WRONG_STATE;
    m_thread->sync([this, &res] {
        if (m_start) {
            m_start = false;
            m_decodeThread->clear();
            m_decodeThread->sync([&res]() {
                res = HRTC_CODE_OK;
            });
        }
    });
    return res;
}

void hrtc::VideoEncodeNode::OnData(IPin* pin, const IMediaInfo& sample)
{
    if (m_start) {
        m_decodeThread->async([sample,this]() {
            MediaInfo info(IMediaInfo::Video);
            m_encoder->Encode((const MediaInfo&)sample,info);
            SendDataToOutput(info);
        });
    }
}
