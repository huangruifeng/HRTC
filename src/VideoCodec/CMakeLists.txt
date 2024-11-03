set(target_name "hrtc_video_codec")

file(GLOB VIDEO_CODEC_SOURCE  *.h *.cpp)
file(GLOB VIDEO_CODEC_x264_SOURCE x264/*.h x264/*.cpp)

add_library(${target_name} STATIC ${VIDEO_CODEC_SOURCE} ${VIDEO_CODEC_x264_SOURCE})
target_link_libraries(${target_name} x264.static yuv)
