#pragma once
#include <stdint.h>
namespace hrtc{

struct VideoFormat {
    enum Format{
        Unknown = 0,
        I420,
        IYUV,
        RGB24,
        BGR24,
        ARGB,
        ABGR,
        RGB565,
        YUY2,
        YV12,
        UYVY,
        MJPEG,
        BGRA,
        NV12,
    };
    Format format;
    int width;
    int height;
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
        AudioFormat Audio;
        VideoFormat Video;
    };

    bool IsVideo()const{ return m_mediaType == Video;}
    bool IsAudio()const {return m_mediaType == Audio;}

    MediaFormat Format() const{return m_mediaFormat};

    IMediaInfo(MediaType type,MediaFormat format):m_mediaType(type),m_mediaFormat(format){}
    virtual uint8_t* GetData(int channel)const{ return nullptr;};
    virtual int GetLineSize(int channel)const{return 0;};

protected:
    MediaType m_mediaType;
    MediaFormat m_mediaFormat;
};

class MediaInfo : public IMediaInfo
{
public:
    MediaInfo(MediaType type,MediaFormat format):IMediaInfo(type,format){
        for(int i = 0 ;i<CHANNEL_SIZE;i++){
            m_data[i] = nullptr;
            m_lineSize[i] = 0;
        }
    }

    void Delloc(){
        for(int i = 0 ;i<CHANNEL_SIZE;i++){
            if(m_data[i]){
                delete[] m_data[i];
            }
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
            switch (m_mediaFormat.Video.format)
            {      
            case VideoFormat::I420: //I420
            case VideoFormat::YV12:
            case VideoFormat::NV12:
            case VideoFormat::MJPEG:
                m_data[0] = new uint8_t[width*height*3/2];
                m_data[1] = m_data[0] + width * height;
                m_data[2] = m_data[1] + width * height/4;
                m_lineSize[0] = width*height;
                m_lineSize[1] = width*height/4;
                m_lineSize[2] = width*height/4;
                break;
            case VideoFormat::RGB24:
            case VideoFormat::BGR24:
                m_data[0] = new uint8_t[width*height*3];
                m_lineSize[0] = width*height*3;
                break;
            case VideoFormat::ARGB:
            case VideoFormat::ABGR:
            case VideoFormat::BGRA:
                m_data[0] = new uint8_t[width*height*4];
                m_lineSize[0] = width*height*4;
                break;
            case VideoFormat::IYUV: //I422
            case VideoFormat::YUY2:
            case VideoFormat::UYVY:
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

    uint8_t *m_data[CHANNEL_SIZE];
    int m_lineSize[CHANNEL_SIZE];
};
}