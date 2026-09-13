# 协议 round-trip 测试（控制台程序，不依赖 Qt）
set(CMAKE_WIN32_EXECUTABLE OFF)  # 覆盖上层 hrtc_demo 的 WIN32 设置

add_executable(hrtc_protocol_test main.cpp)
target_link_libraries(hrtc_protocol_test hrtc_whiteboard hrtc_transport)

if(MSVC)
    target_compile_options(hrtc_protocol_test PRIVATE /utf-8)
endif()
