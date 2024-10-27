#include "VideoNode/VideoSourceNode.h"
#include "Base/GuardHelper.h"
#include "libyuv/convert.h"
using namespace hrtc;


hrtc::VideoSourceNode::VideoSourceNode():BaseNode(SOURCE,VIDEO),m_start(false),m_thread(dispatch_task_queue::task_queue::current()), m_info(IMediaInfo::Video)
{
    m_thread = dispatch_task_queue::task_queue::current();
    m_firstFrame = true;
}

hrtc::VideoSourceNode::~VideoSourceNode()
{
}

RtcResult hrtc::VideoSourceNode::InitDevice(const std::string& id)
{
    RtcResult res = HRTC_CODE_ERROR_THREAD_NULLPTR;
    if (m_thread) {
        m_thread->sync([this,&res,&id]() {
            auto start = m_start;
            if (start) {
                StopInternal();
            }

            auto tmp = VideoCaptureFactory::Create(id.c_str());
            if (!tmp) {//select device failed.
               /* if (start) {
                    StartCapture();
                }*/
                res = HRTC_CODE_ERROR_INVALID_ARG;
                return;
            }
            m_videoCap = tmp;
            if (start) {
                res = StartInternal();
                return;
            }
            res = HRTC_CODE_OK;
         });
    }

    return res;
}

RtcResult hrtc::VideoSourceNode::StartCapture()
{
    RtcResult res = HRTC_CODE_ERROR_THREAD_NULLPTR;
    if (m_thread) {
        m_thread->sync([this, &res] {
            res = StartInternal();
        });
    }
    return res;
}

RtcResult hrtc::VideoSourceNode::StopCapture()
{
    RtcResult res = HRTC_CODE_ERROR_THREAD_NULLPTR;
    if (m_thread) {
        m_thread->sync([this, &res] {
            res = StopInternal();
        });
    }
    return res;
}

RtcResult hrtc::VideoSourceNode::Connect(INode* node)
{
    RtcResult res = HRTC_CODE_ERROR_THREAD_NULLPTR;
    if (m_thread) {
        m_thread->sync([this, &res,node] {
           res = ConnectDefault(dynamic_cast<BaseNode*>(node));
        });
    }
    return res;
}

RtcResult hrtc::VideoSourceNode::Disconnect(INode* node)
{
    RtcResult res = HRTC_CODE_ERROR_THREAD_NULLPTR;
    if (m_thread) {
        m_thread->sync([this, &res, node] {
            res = Disconnect(dynamic_cast<BaseNode*>(node));
        });
    }
    return res;
}

RtcResult hrtc::VideoSourceNode::StartInternal()
{
    if (m_videoCap) {

        if (!m_start || !m_videoCap->CaptureStarted()) {

            //todo support caplicty;
            VideoCaptureCapability cap;
            cap.width = 800;
            cap.height = 600;
            cap.maxFPS = 15;
            cap.videoType = VideoType::kYUY2;
            m_videoCap->RegisterCaptureDataCallback(this);
            auto res = m_videoCap->StartCapture(cap);
            if (HRTC_SUCCESSED(res)) {
                m_start = true;
            }
            return res;
        }
        m_start = true;
        return HRTC_CODE_ERROR_WRONG_STATE;
    }
    return HRTC_CODE_ERROR_NOT_INITIALIZED;
}

RtcResult hrtc::VideoSourceNode::StopInternal()
{
    if (m_videoCap) {
        if (m_start || m_videoCap->CaptureStarted()) {
            auto res = m_videoCap->StopCapture();
            if (HRTC_SUCCESSED(res)) {
                m_videoCap->DeRegisterCaptureDataCallback();
                m_start = false;
                m_firstFrame = true;
            }
            return res;
        }
        m_start = false;
        return HRTC_CODE_ERROR_WRONG_STATE;
    }
    return HRTC_CODE_ERROR_NOT_INITIALIZED;
}


int32_t hrtc::VideoSourceNode::OnRawFrame(uint8_t* videoFrame, size_t videoFrameLength, const VideoCaptureCapability& frameInfo, VideoRotation rotation, int64_t captureTime)
{
    if (m_firstFrame) {
        hrtc::IMediaInfo::MediaFormat foramt;
        foramt.Video.format = VideoFormat::I420;
        foramt.Video.width = frameInfo.width;
        foramt.Video.height = frameInfo.height;
        m_info.SetMediaFormat(foramt);
        m_info.Alloc();
        m_firstFrame = true;
    }

    libyuv::ConvertToI420(videoFrame, videoFrameLength,
        m_info.GetData(0), m_info.GetLineSize(0),
        m_info.GetData(1), m_info.GetLineSize(1),
        m_info.GetData(2), m_info.GetLineSize(2),
        0, 0, frameInfo.width, frameInfo.height,
        frameInfo.width, frameInfo.height,
        libyuv::RotationMode::kRotate0,
        frameInfo.GetVideoFormat());

    SendDataToOutput(m_info);
    return HRTC_CODE_OK;
}

