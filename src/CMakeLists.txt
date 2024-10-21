set(target_name "hrtc_engine")

file(GLOB RTC_NODE_SRC  Node/*.h Node/*.cpp)
file(GLOB RTC_BASE_SRC  Base/*.h Base/*.cpp)
file(GLOB RTC_PIN_SRC  Pin/*.h Pin/*.cpp)

SOURCE_GROUP("Node" FILES ${RTC_NODE_SRC})
SOURCE_GROUP("Base" FILES ${RTC_BASE_SRC})
SOURCE_GROUP("Pin" FILES ${RTC_PIN_SRC})
add_library(${target_name} STATIC ${RTC_NODE_SRC} ${RTC_BASE_SRC} ${RTC_PIN_SRC})
# if(HRTC_ANDROID)
#     target_link_libraries(${target_name} jni_utils)
# endif()                    
# set_target_properties(${target_name} PROPERTIES FOLDER "engine")