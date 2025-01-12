set(target_name "hrtc_transport")

file(GLOB TRANSPORT_SOURCE  *.h *.cpp)

add_library(${target_name} STATIC ${TRANSPORT_SOURCE})
target_link_libraries(${target_name} libuv::libuv)
