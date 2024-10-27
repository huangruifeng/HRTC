#include "Headers/HrtcEngine.h"
#include "VideoNode/VideoSinkNode.h"
#include "VideoNode/VideoSourceNode.h"
#include "Base/Thread/DispatchQueue.h"
using namespace hrtc;

class HrtcThread : public IThread {
public:
    HrtcThread(const std::string& name):m_taskQueue(dispatch_task_queue::create(1,name)) {
    }
    virtual void BeginInvoke(const std::function<void()>& f) override {
        m_taskQueue->async(f);
    }

    virtual void Invoke(const std::function<void()>& f)override {
        m_taskQueue->sync(f);
    }

private:
    std::shared_ptr<dispatch_task_queue::task_queue> m_taskQueue;
};

class VideoDeviceManager : public IVideoDeviceManager {
public:
    std::vector<DeviceInfo> ListVideoDevice() override{
        std::vector<DeviceInfo> ret;
        if (m_thread) {
            m_thread->sync([&] {
                auto num = m_deviceInfo->NumberOfDevices();
                ret.resize(num);
                for (int i = 0; i < num; i++) {
                    char id[256] = { 0 };
                    char name[256] = { 0 };
                    m_deviceInfo->GetDeviceName(i, name, sizeof(name), id, sizeof(id));
                    ret[i].deviceId = id;
                    ret[i].deviceName = name;
                }
            });
        }
        return ret;
    }

    VideoDeviceManager() :m_thread(dispatch_task_queue::task_queue::current()),m_deviceInfo(VideoCaptureFactory::CreateDeviceInfo()) {
    }
    ~VideoDeviceManager() {
        delete m_deviceInfo;
    }
private:
    std::shared_ptr < dispatch_task_queue::task_queue> m_thread;
    VideoCaptureModule::DeviceInfo* m_deviceInfo;
};


template<class T>
std::shared_ptr<T> CreateWithThread(const std::shared_ptr<IThread>& apiThread) {
    std::shared_ptr<T> ret;
    if (apiThread) {
        apiThread->Invoke([&] {
            ret = std::make_shared<T>();
        });
    }
    else {
        auto sdkThread = dispatch_task_queue::task_queue::current();
        if (sdkThread) {
            ret = std::make_shared<T>();
        }
    }
    return ret;
}

std::shared_ptr<IVideoCaptureSource> hrtc::CreateVideoCaptureSource(const std::shared_ptr<IThread>& apiThread) {
    return CreateWithThread<VideoSourceNode>(apiThread);
}


std::shared_ptr<IVideoRenderSink> hrtc::CreateVideoRenderSink(const std::shared_ptr<IThread>& apiThread) {
    return CreateWithThread<VideoSinkNode>(apiThread);
}


std::shared_ptr<IThread> hrtc::CreateThread(const std::string& name) {
    return std::make_shared<HrtcThread>(name);
}

std::shared_ptr<IVideoDeviceManager> hrtc::CreateVideoDeviceManager(const std::shared_ptr<IThread>& apiThread) {
    return CreateWithThread<VideoDeviceManager>(apiThread);
}