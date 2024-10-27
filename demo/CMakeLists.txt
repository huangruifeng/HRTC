set(target_name "hrtc_demo")

if(HRTC_WINDOWS)
set(CMAKE_WIN32_EXECUTABLE ON)

add_executable(${target_name} main.cpp model.h)
target_link_libraries(${target_name} hrtc_engine)
endif()