#pragma once
#include <mutex>
#include<condition_variable>

namespace hrtc {
class HEvent {
public:
    HEvent() : m_result(false){}

    void set() { 
        std::lock_guard<std::mutex> lock(m_timeMutex);
        m_result = true;
        m_timeCv.notify_one(); 
    }

    void reset(){
        std::lock_guard<std::mutex> lock(m_timeMutex);
        m_result = false;
        m_timeCv.notify_all();
    }

    void Wait(){
        std::unique_lock<std::mutex> lock(m_timeMutex);
        m_timeCv.wait(lock,[&](){
            return m_result;
        });
    }

    bool tryWait(long millisecond) {
        std::unique_lock<std::mutex> lock(m_timeMutex);
        return m_timeCv.wait_for(lock, std::chrono::milliseconds(millisecond),[&](){
            return m_result;
        });
    }
private:
    bool                    m_result;
    std::mutex              m_timeMutex;
    std::condition_variable m_timeCv;
};
}