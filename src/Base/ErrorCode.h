#pragma once
namespace hrtc {
    using RtcResult = int;

#define HRTC_SUCCESSED(x) ((x) >= hrtc::HRTC_CODE_OK)
#define HRTC_FAILED(x) ((x) < hrtc::HRTC_CODE_OK)
#define HRTC_WARNING_CODE_BASE (30000)
#define HRTC_ERROR_CODE_BASE (-30000)

constexpr int HRTC_CODE_OK = 0;
constexpr int HRTC_CODE_ERROR_FAILURE = HRTC_ERROR_CODE_BASE + (-1);
constexpr int HRTC_CODE_ERROR_NULLPTR = HRTC_ERROR_CODE_BASE + (-2);
constexpr int HRTC_CODE_ERROR_INVALID_ARG = HRTC_ERROR_CODE_BASE + (-3);

constexpr int HRTC_CODE_ERROR_NOT_INITIALIZED = HRTC_ERROR_CODE_BASE + (-4);
constexpr int HRTC_CODE_ERROR_WRONG_STATE = HRTC_ERROR_CODE_BASE + (-6);
constexpr int HRTC_CODE_ERROR_INVALID_CONFIG = HRTC_ERROR_CODE_BASE + (-7);
constexpr int HRTC_CODE_API_EXECUTION_FAILURE = HRTC_ERROR_CODE_BASE + (-8);
constexpr int HRTC_CODE_ERROR_NOT_SUPPORTED = HRTC_ERROR_CODE_BASE + (-9);
constexpr int HRTC_CODE_ERROR_DUPLICATED = HRTC_ERROR_CODE_BASE +  (-10);
constexpr int HRTC_CODE_ERROR_NOT_FOUND = HRTC_ERROR_CODE_BASE + (-12);


constexpr int HRTC_CODE_ERROR_PIN_WRONG_DIRECTION = HRTC_ERROR_CODE_BASE + (-20100);
constexpr int HRTC_CODE_ERROR_CONNECT_TO_SELF = HRTC_ERROR_CODE_BASE + (-20101);
constexpr int HRTC_CODE_ERROR_INVALID_PIN = HRTC_ERROR_CODE_BASE + (-20102);

constexpr int HRTC_CODE_ERROR_THREAD_NULLPTR = HRTC_ERROR_CODE_BASE + (-20200);


constexpr int HRTC_CODE_WARNING_NODE_NOT_CONNECT = HRTC_ERROR_CODE_BASE + 5;
constexpr int HRTC_CODE_WARNING_WRONG_STATE = HRTC_ERROR_CODE_BASE + 6;

}