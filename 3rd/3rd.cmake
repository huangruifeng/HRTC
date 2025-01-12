add_subdirectory(3rd/libyuv)
include_directories(3rd/libyuv/include)

add_subdirectory(3rd/msgpack-c)
include_directories(3rd/msgpack-c/include)

if(HRTC_WINDOWS)
    add_definitions(-D SYS_WINDOWS)
endif()

add_subdirectory(3rd/x264)
include_directories(3rd/x264)

add_subdirectory(3rd/libuv)
include_directories(3rd/libuv/include)

set_target_properties(x264.static PROPERTIES FOLDER "3rd/x264")
set_target_properties(x264.common.objects PROPERTIES FOLDER "3rd/x264")
set_target_properties(x264.encoder.objects PROPERTIES FOLDER "3rd/x264")
set_target_properties(x264.filter.objects PROPERTIES FOLDER "3rd/x264")
set_target_properties(x264.input.objects PROPERTIES FOLDER "3rd/x264")
set_target_properties(x264.output.objects PROPERTIES FOLDER "3rd/x264")

set_target_properties(yuv PROPERTIES FOLDER "3rd/yuv")
set_target_properties(yuv_common_objects PROPERTIES FOLDER "3rd/yuv")

set_target_properties(uv_a PROPERTIES FOLDER "3rd/uv")
set_target_properties(uv PROPERTIES FOLDER "3rd/uv")