#pragma once
#include <cstdint>
namespace hrtc
{
    static const int64_t kNumMillisecsPerSec = INT64_C(1000);
    static const int64_t kNumMicrosecsPerSec = INT64_C(1000000);
    static const int64_t kNumNanosecsPerSec = INT64_C(1000000000);

    static const int64_t kNumMicrosecsPerMillisec =
        kNumMicrosecsPerSec / kNumMillisecsPerSec;
    static const int64_t kNumNanosecsPerMillisec =
        kNumNanosecsPerSec / kNumMillisecsPerSec;
    static const int64_t kNumNanosecsPerMicrosec =
        kNumNanosecsPerSec / kNumMicrosecsPerSec;

    // Elapsed milliseconds between NTP base, 1900 January 1 00:00 GMT
    // (see https://tools.ietf.org/html/rfc868), and January 1 00:00 GMT 1970
    // epoch. This is useful when converting between the NTP time base and the
    // time base used in RTCP reports.
    constexpr int64_t kNtpJan1970Millisecs = 2'208'988'800 * kNumMillisecsPerSec;

    int64_t TimeMillis();
    // Deprecated. Do not use this in any new code.
    inline int64_t Time() {
        return TimeMillis();
    }

    // Returns the current time in microseconds.
    int64_t TimeMicros();

    // Returns the current time in nanoseconds.
    int64_t TimeNanos();
    
} // namespace hrtc
