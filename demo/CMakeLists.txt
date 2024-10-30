set(target_name "hrtc_demo")

if(HRTC_WINDOWS)
set(CMAKE_WIN32_EXECUTABLE ON)
file(GLOB whiteBoardSource  Whiteboard/*.h Whiteboard/*.cpp)
add_executable(${target_name} main.cpp model.h ${whiteBoardSource})
target_link_libraries(${target_name} hrtc_engine)
endif()