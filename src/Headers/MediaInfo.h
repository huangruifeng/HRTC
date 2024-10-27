#pragma once
#include <stdint.h>
namespace hrtc{

    
//////////////////////////////////////////////////////////////////////////////
// Definition of FourCC codes
//////////////////////////////////////////////////////////////////////////////

// Convert four characters to a FourCC code.
// Needs to be a macro otherwise the OS X compiler complains when the kFormat*
// constants are used in a switch.
#ifdef __cplusplus
#define HRTC_FOURCC(a, b, c, d)                                        \
  ((static_cast<uint32_t>(a)) | (static_cast<uint32_t>(b) << 8) | \
   (static_cast<uint32_t>(c) << 16) | /* NOLINT */                \
   (static_cast<uint32_t>(d) << 24))  /* NOLINT */
#else
#define HRTC_FOURCC(a, b, c, d)                                     \
  (((uint32_t)(a)) | ((uint32_t)(b) << 8) |       /* NOLINT */ \
   ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24)) /* NOLINT */
#endif



struct VideoFormat {
    enum Format{
        Unknown = -1,
        I420 = HRTC_FOURCC('I', '4', '2', '0'),
        IYUV = HRTC_FOURCC('I', 'Y', 'U', 'V'),
        ARGB = HRTC_FOURCC('A', 'R', 'G', 'B'),
        ABGR = HRTC_FOURCC('A', 'B', 'G', 'R'),
        YUY2 = HRTC_FOURCC('Y', 'U', 'Y', '2'),
        YV12 = HRTC_FOURCC('Y', 'V', '1', '2'),
        MJPEG = HRTC_FOURCC('M', 'J', 'P', 'G'),
        NV12 = HRTC_FOURCC('N', 'V', '1', '2'),
    };
    Format format = Unknown;
    int width = 0;
    int height = 0;
};

struct AudioFormat
{
    enum Format {
        SAMPLE_FMT_NONE = -1,
        SMPLE_FMT_U8,          ///< unsigned 8 bits
        SMPLE_FMT_S16,         ///< signed 16 bits
        SAMPLE_FMT_S32,         ///< signed 32 bits
        SAMPLE_FMT_FLT,         ///< float
    };
    int channel = 0;
    int sampleRate = 0;
    Format foramt = SAMPLE_FMT_NONE;
    int bitsPerSample = 0;
};

class IMediaInfo {
public:
#define CHANNEL_SIZE  4
    virtual ~IMediaInfo() = default;
    enum MediaType {
        Audio,
        Video
    };
    union MediaFormat
    {
        MediaFormat() {
            Audio.sampleRate = 0;
        }
        AudioFormat Audio;
        VideoFormat Video;
    };

    bool IsVideo()const{ return m_mediaType == Video;}
    bool IsAudio()const {return m_mediaType == Audio;}

    MediaFormat Format() const{return m_mediaFormat;}

    IMediaInfo(MediaType type,MediaFormat format):m_mediaType(type),m_mediaFormat(format){}
    IMediaInfo(MediaType type):m_mediaType(type) {}
    virtual void SetMediaFormat(MediaFormat format) { m_mediaFormat = format; };
    virtual uint8_t* GetData(int channel)const{ return nullptr;}
    virtual int GetLineSize(int channel)const{return 0;}
    virtual int GetSize()const {return 0;}

protected:
    MediaType m_mediaType;
    MediaFormat m_mediaFormat;
};

class MediaInfo : public IMediaInfo
{
public:
    MediaInfo(MediaType type,MediaFormat format):IMediaInfo(type,format){
        m_size = 0;
        for (int i = 0; i < CHANNEL_SIZE; i++) {
            m_data[i] = nullptr;
            m_lineSize[i] = 0;
        }

    }
    MediaInfo(MediaType type) :IMediaInfo(type) {
        m_size = 0;
        for (int i = 0; i < CHANNEL_SIZE; i++) {
            m_data[i] = nullptr;
            m_lineSize[i] = 0;
        }
    }

    void Delloc(){
        if (m_data[0]) {
            delete[] m_data[0];
        }
        m_size = 0;
        for (int i = 0; i < CHANNEL_SIZE; i++) {
            m_data[i] = nullptr;
            m_lineSize[i] = 0;
        }
    }

    void Alloc(){

        if(m_mediaType == Audio){

        }
        else if(m_mediaType == Video){
            int width = m_mediaFormat.Video.width;
            int height = m_mediaFormat.Video.height;
            int newsize = 0;
            switch (m_mediaFormat.Video.format)
            {      
            case VideoFormat::I420: //I420
            case VideoFormat::YV12:
            case VideoFormat::NV12:
            case VideoFormat::MJPEG:
                
                newsize = width*height*3/2;
                if (m_size < newsize) {
                    Delloc();
                    m_size = newsize;
                    m_data[0] = new uint8_t[m_size];
                }
                m_data[1] = m_data[0] + width * height;
                m_data[2] = m_data[1] + width * height / 4;
                m_lineSize[0] = width;
                m_lineSize[1] = width / 2;
                m_lineSize[2] = width / 2;
                break;
            case VideoFormat::ARGB:
            case VideoFormat::ABGR:
                newsize = width*height*4;
                if (m_size < newsize) {
                    Delloc();
                    m_data[0] = new uint8_t[m_size];
                }
                m_lineSize[0] = width * height * 4;
                break;
            case VideoFormat::IYUV: //I422
            case VideoFormat::YUY2:
                //todo support I422;
            default:
                break;
            }
        }
    }
    virtual uint8_t* GetData(int channel)const override{ 
        if(channel < 0 ||channel > CHANNEL_SIZE)
            return nullptr;
        return m_data[channel];
    };
    virtual int GetLineSize(int channel)const override{
        if(channel < 0 ||channel > CHANNEL_SIZE)
            return 0;
        return m_lineSize[channel];
    };
    virtual int GetSize()const {return m_size;}

    uint8_t *m_data[CHANNEL_SIZE];
    int m_lineSize[CHANNEL_SIZE];
    int m_size;
};
}