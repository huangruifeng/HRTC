set(target_name "hrtc_video_render")

file(GLOB VIDEO_RENDER_SOURCE  *.h *.cpp)

if(HRTC_WINDOWS)
    file(GLOB VIDEO_RENDER_PLATFORM_SOURCE     windows/d3d9/*.h 
                                        windows/d3d9/*.cc 
                                        windows/*.cc
                                        )
elseif(HRTC_LINUX)
    file(GLOB VIDEO_RENDER_PLATFORM_SOURCE  linux/*.h linux/*.cpp)
endif()

add_library(${target_name} STATIC ${VIDEO_RENDER_SOURCE} ${VIDEO_RENDER_PLATFORM_SOURCE})
target_link_libraries(${target_name} hrtc_base)
