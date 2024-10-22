#pragma once

// RTC_EXPORT is used to mark symbols as exported or imported when HRTC is
// built or used as a shared library.
// When HRTC is built as a static library the RTC_EXPORT macro expands to
// nothing.

#ifdef HRTC_ENABLE_SYMBOL_EXPORT

#ifdef HRTC_WIN

#ifdef HRTC_LIBRARY_IMPL
#define RTC_EXPORT __declspec(dllexport)
#else
#define RTC_EXPORT __declspec(dllimport)
#endif

#else  // HRTC_WIN

#if __has_attribute(visibility)
#define RTC_EXPORT __attribute__((visibility("default")))
#endif

#endif  // WEBRTC_WIN

#endif  // HRTC_ENABLE_SYMBOL_EXPORT

#ifndef RTC_EXPORT
#define RTC_EXPORT
#endif
