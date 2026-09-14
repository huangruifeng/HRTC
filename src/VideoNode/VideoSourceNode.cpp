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

            auto tmp = m_deviceManager.CreateDevice(id);
            if (!tmp) {//select device failed.
               /* if (start) {
                    StartCapture();
                }*/
                res = HRTC_CODE_ERROR_INVALID_ARG;
                return;
            }
            m_videoDevice = tmp;
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


RtcResult hrtc::VideoSourceNode::StartInternal()
{
    if (m_videoDevice) {

        if (!m_start || !m_videoDevice->IsCapturing()) {

            //todo support capability;
            VideoCapability requested;
            requested.width = 800;
            requested.height = 600;
            requested.maxFPS = 15;
            requested.format = VideoPixelFormat::kYUY2;

            VideoCapability selected;
            if (FindBestCapability(m_videoDevice->GetCapabilities(), requested, selected) < 0) {
                return HRTC_CODE_ERROR_NOT_SUPPORTED;
            }
            m_videoDevice->SetVideoSink(this);
            auto res = m_videoDevice->StartCapture(selected);
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
    if (m_videoDevice) {
        if (m_start || m_videoDevice->IsCapturing()) {
            auto res = m_videoDevice->StopCapture();
            if (HRTC_SUCCESSED(res)) {
                m_videoDevice->SetVideoSink(nullptr);
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


void hrtc::VideoSourceNode::OnVideoFrame(const VideoFrame& frame)
{
    if (m_firstFrame) {
        hrtc::IMediaInfo::MediaFormat foramt;
        foramt.Video.format = VideoFormat::I420;
        foramt.Video.width = frame.width();
        foramt.Video.height = frame.height();
        m_info.SetMediaFormat(foramt);
        m_info.Alloc();
        m_firstFrame = false;
    }

    VideoCapability frameInfo;
    frameInfo.format = frame.format();
    frameInfo.width = frame.width();
    frameInfo.height = frame.height();

    libyuv::ConvertToI420(frame.data(), frame.size(),
        m_info.GetData(0), m_info.GetLineSize(0),
        m_info.GetData(1), m_info.GetLineSize(1),
        m_info.GetData(2), m_info.GetLineSize(2),
        0, 0, frame.width(), frame.height(),
        frame.width(), frame.height(),
        libyuv::RotationMode::kRotate0,
        frameInfo.GetVideoFormat());

    SendDataToOutput(m_info);
}
