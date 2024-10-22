set(target_name "hrtc_base")

file(GLOB RTC_BASE_SRC *.h *.cpp)
file(GLOB RTC_VIDEO_BASE_SRC VideoBase/*.h VideoBase/*.cpp)
add_library(${target_name} STATIC ${RTC_BASE_SRC} ${RTC_VIDEO_BASE_SRC})