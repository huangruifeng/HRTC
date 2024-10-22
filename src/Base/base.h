#pragma once

#define LOG_DEBUG(t,x)
#define LOG_ERROR(t,x)
#define LOG_INFO(t,x)
#define LOG_WARNING(t,x)

#define HRTC_ASSERT_MSG_DEBUG(cond,msg)
#define HRTC_ASSERT_DEBUG(cond)

#define HRTC_ASSERT_MSG(cond,msg)
#define HRTC_ASSERT(cond)

#define RETURN_ERROR_IF_NULLPTR(x)                              \
    if(!x) {                                                    \
        HRTC_ASSERT_MSG_DEBUG(false, #x"can not be nullptr");     \
        return HRTC_CODE_ERROR_NULLPTR;                              \
    }

#define LOG_AND_RETURN_IF_NULLPTR(x)                            \
    if(!x) {                                                    \
        HRTC_ASSERT_MSG_DEBUG(false, #x"can not be nullptr");     \
        LOG_ERROR(TAG, #x"can not be nullptr");                 \
        return HRTC_CODE_ERROR_NULLPTR;                              \
    }

#define LOG_MSG_AND_RETURN_IF_NULLPTR(x, msg)                   \
    if(!x) {                                                    \
        HRTC_ASSERT_MSG_DEBUG(false, msg);                        \
        LOG_ERROR(TAG, msg);                                    \
        return HRTC_CODE_ERROR_NULLPTR;                              \
    }

#define LOG_MSG_AND_RETURN_VOID_IF_NULLPTR(x, msg) \
    if (!(x)) {                                      \
        HRTC_ASSERT_MSG_DEBUG(false, msg);          \
        LOG_ERROR(TAG, msg);                       \
        return;                                    \
    }


template <typename T, size_t N>
char (&ArraySizeHelper(T (&array)[N]))[N];

#define arraysize(array) (sizeof(ArraySizeHelper(array)))
