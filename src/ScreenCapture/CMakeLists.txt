set(target_name "hrtc_screen_capture")

file(GLOB SCREEN_CAPTURE_SOURCE *.h *.cpp)
if(HRTC_WINDOWS)
    file(GLOB SCREEN_PLATFORM_SOURCE    windows/screen/*.h
                                        windows/screen/*.cc
                                        windows/screen/*.cpp
                                        windows/screen/gdi/*.h
                                        windows/screen/gdi/*.cc
                                        windows/screen/dxgi/*.h
                                        windows/screen/dxgi/*.cc
                                        windows/screen/wgc/*.h
                                        windows/screen/wgc/*.cc
                                        )
endif()

add_library(${target_name} STATIC ${SCREEN_CAPTURE_SOURCE} ${SCREEN_PLATFORM_SOURCE})
target_link_libraries(${target_name} hrtc_video_common hrtc_base)

if(HRTC_WINDOWS)
    target_link_libraries(${target_name} hrtc_video_common hrtc_base user32 gdi32 d3d11 dxgi RuntimeObject)
endif()
