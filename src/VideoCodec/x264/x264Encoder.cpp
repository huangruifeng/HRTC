#include "x264Encoder.h"
#include <vector>
#include <libyuv/convert.h>
using namespace hrtc;

hrtc::X264VideoEncoder::~X264VideoEncoder()
{
    if (m_x264) {
        x264_encoder_close(m_x264);
        m_x264 = nullptr;
    }
}

hrtc::X264VideoEncoder::X264VideoEncoder()
{
   
}

bool hrtc::X264VideoEncoder::Encode(const MediaInfo& input, MediaInfo& output)
{

    if (input.Format().Video.width != m_width || input.Format().Video.height != m_height) {
        m_width = input.Format().Video.width;
        m_height = input.Format().Video.height;
        DeInitCodec();
        if (!InitCodec()) {
            DeInitCodec();
            return false;
        }
    }

    libyuv::ConvertToI420(input.GetData(0), input.GetSize(),
        m_x264Picture.img.plane[0], m_x264Picture.img.i_stride[0],
        m_x264Picture.img.plane[1], m_x264Picture.img.i_stride[1],
        m_x264Picture.img.plane[2], m_x264Picture.img.i_stride[2],
        0, 0, m_width, m_height, m_width, m_height, libyuv::RotationMode::kRotate0,
        input.Format().Video.format);

    m_x264Picture.i_pts = input.m_time;
    //m_x264Picture.

    x264_nal_t* nalOut;
    int nalNum;
    x264_picture_t out;
    auto ret = x264_encoder_encode(m_x264, &nalOut, &nalNum, &m_x264Picture, &out);
    if (ret <= 0)
        return false;
    //if (ret == 0)
    //    return true;
    int dataSize = 0;
    int spsIndex = -1;
    int ppsIndex = -1;
    bool isKey = false;
    std::vector<int> frameIndexs;
    for (int i = 0; i < nalNum; ++i) {
        //I P B
        if (nalOut[i].i_type == NAL_SLICE_IDR ) {
            dataSize += nalOut[i].i_payload;
            frameIndexs.push_back(i);
            isKey = true;
        }
        else if (nalOut[i].i_type == NAL_SLICE) {
            dataSize += nalOut[i].i_payload;
            frameIndexs.push_back(i);
        }
        else if (nalOut[i].i_type == NAL_SEI) {
            continue;
        }
        else if (nalOut[i].i_type == NAL_SPS) {
            spsIndex = i;
        }
        else if (nalOut[i].i_type == NAL_PPS) {
            ppsIndex = i;
        }
    }

    if (isKey && spsIndex != -1 && ppsIndex != -1) {
        dataSize += nalOut[spsIndex].i_payload;
        dataSize += nalOut[ppsIndex].i_payload;
    }

    hrtc::IMediaInfo::MediaFormat f;
    f.Video.format = VideoFormat::H264;
    f.Video.key = isKey;
    output.SetMediaFormat(f);
    output.Alloc(dataSize);
    output.m_time = out.i_pts;

    output.m_lineSize[0] = dataSize;
    uint8_t* data = output.m_data[0];
    if (isKey && spsIndex != -1 && ppsIndex != -1) {
        memcpy(data, nalOut[spsIndex].p_payload, nalOut[spsIndex].i_payload);
        data += nalOut[spsIndex].i_payload;
        memcpy(data, nalOut[ppsIndex].p_payload, nalOut[ppsIndex].i_payload);
        data += nalOut[ppsIndex].i_payload;
    }
    for (auto index : frameIndexs) {
        memcpy(data, nalOut[index].p_payload, nalOut[index].i_payload);
        data += nalOut[index].i_payload;
    }

    return true;
}


void Log264(void*, int i_level, const char* psz, va_list args)
{
    char buff[1024];
    //va_list args2;
    //va_copy(args2, args);
#if HRTC_WINDOWS
    vsprintf(buff, psz, args);
#else
    sprintf(buff, psz, args);
#endif
    //va_end(args2);
}


bool hrtc::X264VideoEncoder::InitCodec()
{
    //todo:hrf add Param moudle.
    memset(&m_encoderParam, 0, sizeof(m_encoderParam));
    x264_param_default_preset(&m_encoderParam, "veryfast", "zerolatency");

    m_encoderParam.i_width = m_width;
    m_encoderParam.i_height = m_height;

    //帧率
    m_encoderParam.i_fps_num = 30;
    m_encoderParam.i_fps_den = 1;

    //GOP 2s
    m_encoderParam.i_keyint_max = 2 * m_encoderParam.i_fps_num / m_encoderParam.i_fps_den;

    //foramt
    m_encoderParam.i_csp = X264_CSP_I420;

    //Slice （一帧图像生成多少个Slice,一般一个图像分辨率大可以多个)
    m_encoderParam.i_slice_count = 1;

    //b帧数 0 关闭
    m_encoderParam.i_bframe = 0;

    //线程数
    m_encoderParam.i_threads = 1;

    //1,帧前面都带SPS与PPS. 0,第一个才会带SPS PPS， 
    m_encoderParam.b_repeat_headers = 1;

    //码率控制 单位kbps CQP,CRF,ABR
    //m_encoderParam.rc.i_rc_method = X264_RC_ABR;
    //m_encoderParam.rc.f_rf_constant = 0;
    //m_encoderParam.rc.i_bitrate = 1000;
    m_encoderParam.rc.f_rf_constant = (float)25;
    m_encoderParam.rc.i_rc_method = X264_RC_CRF;
    m_encoderParam.rc.i_bitrate = 0;
    //最大码率
    //m_encoderParam.rc.i_vbv_max_bitrate = 100000;
    
    //码率缓冲区
    m_encoderParam.rc.i_vbv_buffer_size = 15000;

    //计算帧间隔的方式 1，使用时间戳。 0使用（fps）计算
    m_encoderParam.b_vfr_input = 0;

    //设置 profile
    x264_param_apply_profile(&m_encoderParam,"baseline");

    //log 参数
    m_encoderParam.p_log_private = nullptr;
    m_encoderParam.i_log_level = X264_LOG_WARNING;
    m_encoderParam.pf_log = Log264;

    // open
    m_x264 = x264_encoder_open(&m_encoderParam);
    if (m_x264) {
        return false;
    }

    //alloc pic
    memset(&m_x264Picture, 0, sizeof(x264_picture_t));
    if (x264_picture_alloc(&m_x264Picture, m_encoderParam.i_csp, m_width, m_height) < 0) {
        DeInitCodec();
        return false;
    }

    m_x264Picture.i_type = X264_TYPE_AUTO;
    return true;
}

void hrtc::X264VideoEncoder::DeInitCodec()
{
    x264_picture_clean(&m_x264Picture);

    if (m_x264) {
        x264_encoder_close(m_x264);
        m_x264 = nullptr;
    }
}
