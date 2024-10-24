set(target_name "hrtc_engine")

add_subdirectory(Base)
add_subdirectory(VideoCapture)
add_subdirectory(VideoRender)


file(GLOB RTC_NODE_SRC  Node/*.h Node/*.cpp)
file(GLOB RTC_PIN_SRC  Pin/*.h Pin/*.cpp)
file(GLOB RTC_API_SRC Headers/*.h)
SOURCE_GROUP("Node" FILES ${RTC_NODE_SRC})
SOURCE_GROUP("Pin" FILES ${RTC_PIN_SRC})
add_library(${target_name} STATIC ${RTC_NODE_SRC} ${RTC_PIN_SRC} ${RTC_API_SRC})
target_link_libraries(${target_name} hrtc_base)
# if(HRTC_ANDROID)
#     target_link_libraries(${target_name} jni_utils)
# endif()                    
# set_target_properties(${target_name} PROPERTIES FOLDER "engine")