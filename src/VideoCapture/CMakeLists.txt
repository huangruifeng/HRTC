set(target_name "hrtc_video_capture")

file(GLOB VIDEO_CAPTURE_SOURCE  *.h *.cpp)

if(HRTC_WINDOWS)
    file(GLOB VIDEO_PLATFORM_SOURCE     windows/ds/*.h 
                                        windows/ds/*.cc 
                                        windows/*.cc
                                        )
elseif(HRTC_LINUX)
    file(GLOB VIDEO_PLATFORM_SOURCE  linux/*.h linux/*.cpp)
endif()

add_library(${target_name} STATIC ${VIDEO_CAPTURE_SOURCE} ${VIDEO_PLATFORM_SOURCE})
target_link_libraries(${target_name} hrtc_base)

if(HRTC_WINDOWS)
    target_link_libraries(${target_name} ole32 oleaut32 strmiids user32)
endif()