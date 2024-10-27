#pragma once
#include <memory>
namespace hrtc
{
template <class T>
class rtc_refptr {
public:
    //using element_type = typename std::remove_extent<T>::type;

    rtc_refptr() : ptr_(nullptr) {}

    rtc_refptr(T* p) : ptr_(p) {
        if (ptr_) ptr_->AddRef();
    }

    template<typename U>
    rtc_refptr(U* p) : ptr_(p) {
        if (ptr_) ptr_->AddRef();
    }

    rtc_refptr(const rtc_refptr<T>& r) : ptr_(r.get()) {
        if (ptr_) ptr_->AddRef();
    }

    template <typename U>
    rtc_refptr(const rtc_refptr<U>& r) : ptr_(r.get()) {
        if (ptr_) ptr_->AddRef();
    }

#if __cplusplus >= 201103L || (defined(_MSC_VER) && _MSC_VER >= 1800)
    rtc_refptr(rtc_refptr<T>&& r) : ptr_(r.move()) {}

    template <typename U>
    rtc_refptr(rtc_refptr<U>&& r) : ptr_(r.move()) {}
#endif

    ~rtc_refptr() {
        reset();
    }

    T* get() const { return ptr_; }
    operator bool() const { return (ptr_ != nullptr); }

    T* operator->() const { return  ptr_; }
    T& operator*() const { return *ptr_; }

    // Returns the (possibly null) raw pointer, and makes the rtc_refptr hold a
    // null pointer, all without touching the reference count of the underlying
    // pointed-to object. The object is still reference counted, and the caller of
    // move() is now the proud owner of one reference, so it is responsible for
    // calling Release() once on the object when no longer using it.
    T* move() {
        T* retVal = ptr_;
        ptr_ = nullptr;
        return retVal;
    }

    rtc_refptr<T>& operator=(T* p) {
        if (ptr_ == p) return *this;

        if (p) p->AddRef();
        if (ptr_) ptr_->Release();
        ptr_ = p;
        return *this;
    }

    rtc_refptr<T>& operator=(const rtc_refptr<T>& r) {
        return *this = r.get();
    }

#if __cplusplus >= 201103L || (defined(_MSC_VER) && _MSC_VER >= 1800)
    rtc_refptr<T>& operator=(rtc_refptr<T>&& r) {
        rtc_refptr<T>(std::move(r)).swap(*this);
        return *this;
    }

    template <typename U>
    rtc_refptr<T>& operator=(rtc_refptr<U>&& r) {
        rtc_refptr<T>(std::move(r)).swap(*this);
        return *this;
    }
#endif

    // For working with std::find()
    bool operator==(const rtc_refptr<T>& r) const { return ptr_ == r.ptr_; }

    bool operator!=(const rtc_refptr<T>& r) const { return  ptr_ != r.ptr_; }

    bool operator!=(const T* ptr) const { return ptr_ != ptr; }

    bool operator==(const T*ptr) const { return ptr_ == ptr; }

    // For working with std::set
    bool operator<(const rtc_refptr<T>& r) const { return ptr_ < r.ptr_; }

    void swap(T** pp) {
        T* p = ptr_;
        ptr_ = *pp;
        *pp = p;
    }

    void swap(rtc_refptr<T>& r) { swap(&r.ptr_); }

    void reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

protected:
    T* ptr_;
};

//template <class T1,class T2>
//rtc_refptr<T1> dynamic_pointer_cast(const rtc_refptr<T2>& other)noexcept {
//    const auto ptr = dynamic_cast<typename rtc_refptr<T1>::element_type*>(other.get());
//    if (ptr) {
//        return rtc_refptr<T1>(ptr);
//    }
//    return rtc_refptr<T1>();
//}
//
//template<class T1,class T2>
//rtc_refptr<T1> const_pointer_cast(const rtc_refptr<T2>& other)noexcept {
//    const auto ptr = const_cast<typename rtc_refptr<T1>::element_type*>(other.get());
//    return rtc_refptr<T1>(ptr);
//}
} // namespace hrtc
