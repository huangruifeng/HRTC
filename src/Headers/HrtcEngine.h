#pragma once
#include <Base/ErrorCode.h>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#define HRTC_EXPORT
#ifdef HRTC_WINDOWS
#ifdef HRTC_EXPORT
#define HRTC_API  __declspec(dllexport)
#else
#define HRTC_API  __declspec(dllimport)
#endif 
#endif

namespace hrtc {

    class Iparameter {
    public:
        virtual ~Iparameter() = default;

    };

    class IRtcCommon {
    public:
        virtual ~IRtcCommon() = default;
        virtual std::shared_ptr<Iparameter> GetParmeter() { return nullptr; };
    };
        
    class IThread : public IRtcCommon {
    public:
        virtual ~IThread() = default;
        virtual void BeginInvoke(const std::function<void()>&) = 0;
        virtual void Invoke(const std::function<void()>&) = 0;
    };

    class INode : public IRtcCommon{
    public:
        virtual ~INode() = default;
        virtual RtcResult Connect(INode* node) = 0;
        virtual RtcResult Disconnect(INode* node) = 0;
    };

    class IVideoCaptureSource : public virtual INode {
    public:
        virtual ~IVideoCaptureSource() = default;
        virtual RtcResult InitDevice(const std::string& id) = 0;
        virtual RtcResult StartCapture() = 0;
        virtual RtcResult StopCapture() = 0;
    };

    class IVideoRenderSink: public virtual INode {
    public:
        virtual ~IVideoRenderSink() = default;
        virtual RtcResult Init(void* hwnd) = 0;
        virtual RtcResult Deinit() = 0;
    };

    class IVideoEncoder : public INode {
    public:
        virtual ~IVideoEncoder() = default;
        virtual RtcResult StartEncode() = 0;
        virtual RtcResult StopEncode() = 0;
    };

    struct DeviceInfo {
        std::string deviceId;
        std::string deviceName;
    };
    class IVideoDeviceManager : public IRtcCommon {
    public:
        virtual ~IVideoDeviceManager() = default;
        virtual std::vector<DeviceInfo> ListVideoDevice() = 0;
    };

    HRTC_API std::shared_ptr<IThread> CreateThread(const std::string&name = "");
    HRTC_API std::shared_ptr<IVideoCaptureSource> CreateVideoCaptureSource(const std::shared_ptr<IThread>& api = nullptr);
    HRTC_API std::shared_ptr<IVideoRenderSink> CreateVideoRenderSink(const std::shared_ptr<IThread>& api = nullptr);
    HRTC_API std::shared_ptr<IVideoDeviceManager> CreateVideoDeviceManager(const std::shared_ptr<IThread>& api = nullptr);
    HRTC_API std::shared_ptr<IVideoEncoder> CreateVideoEncodeNode(const std::shared_ptr<IThread>& api = nullptr);
}
