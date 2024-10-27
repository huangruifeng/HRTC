#include <Headers/HrtcEngine.h>

class RtcModel {
public:
    RtcModel():m_apiThread(hrtc::CreateThread())
              ,m_captureSource(hrtc::CreateVideoCaptureSource(m_apiThread))
              ,m_renderSink(hrtc::CreateVideoRenderSink(m_apiThread))
              ,m_deviceManager(hrtc::CreateVideoDeviceManager(m_apiThread))
              ,m_captureStart(false)
              ,m_renderSelfStart(false){
        
        m_captureSource->Connect(m_renderSink.get());
        m_videoDevice = m_deviceManager->ListVideoDevice();
    }

    virtual ~RtcModel() {
       //m_captureSource->Disconnect(m_renderSink.get());
    }

    void OnDeviceSelect(int index) {
        if (index >= m_videoDevice.size() || index < 0)
            return;
        m_captureSource->InitDevice(m_videoDevice[index].deviceId);
    }

    void OnCaptureEnable(bool enable) {
        if (enable) {
            m_captureSource->StartCapture();
            m_captureStart = true;
        }
        else {
            m_captureSource->StopCapture();
            m_captureStart = false;
        }
    }

    void OnRenderSelfEnable(void* hwnd) {
        if (hwnd) {
            m_renderSink->Init(hwnd);
            m_renderSelfStart = true;
        }
        else {
            m_renderSink->Deinit();
            m_renderSelfStart = false;
        }
    }
protected:
    std::shared_ptr<hrtc::IThread> m_apiThread;
    std::shared_ptr<hrtc::IVideoCaptureSource> m_captureSource;
    std::shared_ptr<hrtc::IVideoDeviceManager> m_deviceManager;
    std::shared_ptr<hrtc::IVideoRenderSink> m_renderSink;
    std::vector<hrtc::DeviceInfo> m_videoDevice;
    bool m_captureStart;
    bool m_renderSelfStart;
};