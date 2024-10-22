cmake_minimum_required(VERSION 3.6)
project(HRTC)

include(compilation_options.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(src)
add_subdirectory(demo)

# cmake statistics
message("----------------------------------------------------------")
message("***************** CMAKE BUILD STATISTICS *****************")
message("----------------------------------------------------------")
message("CMAKE_C_FLAGS: " ${CMAKE_C_FLAGS})
message("CMAKE_C_FLAGS_DEBUG: " ${CMAKE_C_FLAGS_DEBUG})
message("CMAKE_C_FLAGS_RELEASE: " ${CMAKE_C_FLAGS_RELEASE})
message("CMAKE_CXX_FLAGS: " ${CMAKE_CXX_FLAGS})
message("CMAKE_CXX_FLAGS_DEBUG: " ${CMAKE_CXX_FLAGS_DEBUG})
message("CMAKE_CXX_FLAGS_RELEASE: " ${CMAKE_CXX_FLAGS_RELEASE})
message("archive output dir: " ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
message("library output dir: " ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
message("runtime output dir: " ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
message("----------------------------------------------------------")