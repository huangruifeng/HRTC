set(target_name "hrtc_whiteboard")

file(GLOB_RECURSE WHITEBOARD_SRC *.h *.cpp)
add_library(${target_name} STATIC ${WHITEBOARD_SRC})

target_link_libraries(${target_name} PUBLIC msgpack-cxx)