add_subdirectory(3rd/libyuv)
include_directories(3rd/libyuv/include)

add_subdirectory(3rd/msgpack-c)
include_directories(3rd/msgpack-c/include)

if(HRTC_WINDOWS)
    add_definitions(-D SYS_WINDOWS)
endif()

add_subdirectory(3rd/x264)
include_directories(3rd/x264)
