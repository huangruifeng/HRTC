/**
 * License: Apache 2.0 with LLVM Exception or GPL v3
 *
 * Author: hrf
 */

#pragma once 

#include <condition_variable>
#include <mutex>

namespace hrtc {
class CountDownLatch {
public:
    /*! Constructor
     param count, the value the countdownlatch object should be initialized with.
    */
    CountDownLatch(int count) {
        if (count < 0) {
            m_count = 0;
        }
        m_count = count;
    }

    /*
     reset CountDownLatch
    */
    bool Reset(int count) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_count > 0) return false;
        m_count = count;
        return true;
    }

    /*
     await causes the caller to wait until the latch is counted down to zero.
    */
    void Await() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_count == 0; });
    }

    /*
     await causes the caller to wait until the latch is counted down to
     zero, if wait time milliseconds is not zero, then maximum wait is for timeout
     milliseconds \param timeout is waittime in milliseconds.
    */
    bool Await(long timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::milliseconds(timeout),
                      [this] { return m_count == 0; });
    }

    /*
     Countdown decrements the count of the latch, signalling all waiting thread if
     the count reaches zero.
    */
    void CountDown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_count > 0) {
            if (--m_count == 0) {
                m_cv.notify_all();
            }
        }
    }

    /*!
     get_count returns the current count
    */
    int GetCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_count;
    };

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    int m_count;
};
}
