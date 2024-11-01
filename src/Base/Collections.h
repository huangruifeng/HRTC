#pragma once
#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>
#include <functional>
#include <Base/ErrorCode.h>

namespace hrtc {
class SingleThreaded {
public:
    virtual void lock(){};
    virtual void unlock(){};
};

using MultiThreaded = std::mutex;

template <class T>
class RawPointer {
public:
    RawPointer(T* data) : m_data(data) {}
    T* get()const { return m_data; }
    T* operator->() const { return m_data; }
    T& operator*() const { return *m_data; }

private:
    T* m_data;
};

template <class T,
          template <typename> class Ptr = std::shared_ptr,
          typename MutexPolicy = SingleThreaded,
          template <typename ELE, typename Alloc = std::allocator<ELE> >
          class Container = std::vector>
class Collections {
public:
    typedef T Type;
    typedef Ptr<T> PtrType;

    int32_t AddElement(PtrType observer){
        return RegisterObserver(observer);
    }

    int32_t RemoveElement(PtrType observer){
        return UnregisterObserver(observer);
    }

    Container<PtrType> Copy() {
        std::lock_guard<MutexPolicy> lock(m_mutex);
        return m_observers;
    }

    int32_t Size() {
        std::lock_guard<MutexPolicy> lock(m_mutex);
        return m_observers.size();
    }

    Container<PtrType> CopyObservers() {
        std::lock_guard<MutexPolicy> lock(m_mutex);
        return m_observers;
    }

    bool IsExist(Type* t){
        std::lock_guard<MutexPolicy> lock(m_mutex);
        for (const auto& ob : m_observers) {
            if(ob.get() == t)
                return true;
        }
        return false;
    }

    bool IsExist(PtrType ptr){
        return IsExist(ptr.get());
    }

    int32_t Foreach(std::function<void(PtrType)> fun) {
        std::lock_guard<MutexPolicy> lock(m_mutex);
        for (const auto& ob : m_observers) {
            fun(ob);
        }
        return 0;
    }

    ~Collections() = default;

private:
    int32_t RegisterObserver(PtrType observer) {
        if (observer.get() == nullptr)
            return HRTC_CODE_ERROR_NULLPTR;
        std::lock_guard<MutexPolicy> lock(m_mutex);
        for (const auto& ob : m_observers) {
            if (observer.get() == ob.get()) {
                return HRTC_CODE_ERROR_DUPLICATED;
            }
        }
        m_observers.push_back(observer);
        return HRTC_CODE_OK;
    }
    int32_t UnregisterObserver(PtrType observer) {
        if (observer.get() == nullptr)
            return HRTC_CODE_ERROR_NULLPTR;
        std::lock_guard<MutexPolicy> lock(m_mutex);
        const auto it = std::find_if(m_observers.begin(), m_observers.end(),
                                     [observer](const PtrType ob) {
                                         return observer.get() == ob.get();
                                     });

        if (it != m_observers.end()) {
            m_observers.erase(it);
            return HRTC_CODE_OK;
        }
        return HRTC_CODE_ERROR_NOT_FOUND;
    }

protected:
    Container<PtrType> m_observers;
    MutexPolicy m_mutex;
};
}  // namespace maxme