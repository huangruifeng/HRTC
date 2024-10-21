/**
 * License: Apache 2.0 with LLVM Exception or GPL v3
 *
 * Author: hrf
 */

#pragma once
#include <atomic>
namespace hrtc {

class SpinLock {
public:
    SpinLock() { _lock.clear(); }
    virtual ~SpinLock() {}
    inline void lock() {
        while (_lock.test_and_set())
            ;
    }
    inline void unlock() { _lock.clear(); }

private:
    std::atomic_flag _lock;
};
}