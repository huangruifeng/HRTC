#include "Base/TimeUtils.h"
#include <chrono>

int64_t hrtc::TimeMillis(){
    std::chrono::system_clock::duration d = 
        std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

int64_t hrtc::TimeMicros(){
    std::chrono::system_clock::duration d = 
        std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}

int64_t hrtc::TimeNanos() {
    std::chrono::system_clock::duration d = 
        std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}