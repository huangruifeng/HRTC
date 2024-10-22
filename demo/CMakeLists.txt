set(target_name "hrtc_demo")

set(CMAKE_WIN32_EXECUTABLE ON)

add_executable(${target_name} main.cpp)
target_link_libraries(${target_name} hrtc_video_capture hrtc_base)
# if(HRTC_ANDROID)
#     target_link_libraries(${target_name} jni_utils)
# endif()                    
# set_target_properties(${target_name} PROPERTIES FOLDER "engine")