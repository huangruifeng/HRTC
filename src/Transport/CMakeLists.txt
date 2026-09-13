set(target_name "hrtc_transport")

file(GLOB_RECURSE TRANSPORT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/*.h ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(FILTER TRANSPORT_SOURCE EXCLUDE REGEX "/test/")

add_library(${target_name} STATIC ${TRANSPORT_SOURCE})
target_link_libraries(${target_name} PUBLIC hrtc_base libuv::libuv msgpack-cxx)

# 源文件含 UTF-8 中文注释，MSVC 下强制 UTF-8 源码编码
if (MSVC)
    target_compile_options(${target_name} PRIVATE /utf-8)
endif()

add_executable(transport_server_demo test/server_demo.cpp)
target_link_libraries(transport_server_demo hrtc_base ${target_name})
if (MSVC)
    target_compile_options(transport_server_demo PRIVATE /utf-8)
endif()

add_executable(transport_client_demo test/client_demo.cpp)
target_link_libraries(transport_client_demo hrtc_base ${target_name})
if (MSVC)
    target_compile_options(transport_client_demo PRIVATE /utf-8)
endif()
