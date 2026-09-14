set(target_name "hrtc_video_common")

file(GLOB VIDEO_COMMON_SOURCE *.h *.cpp)

add_library(${target_name} STATIC ${VIDEO_COMMON_SOURCE})
target_link_libraries(${target_name} hrtc_base)
