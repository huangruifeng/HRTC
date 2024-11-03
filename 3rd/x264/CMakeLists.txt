cmake_minimum_required(VERSION 3.23 FATAL_ERROR)

project(libx264)

option(X264_ENABLE_SHARED "Build x264 shared library" OFF)
option(X264_ENABLE_STATIC "Build x264 static library" ON)
option(X264_CMAKE_INSTALL "Install x264 artefacts" OFF)

include(CMakeDependentOption)
cmake_dependent_option(X264_ENABLE_TOOL "Build x264 tool" OFF "X264_ENABLE_SHARED OR X264_ENABLE_STATIC" OFF)
cmake_dependent_option(X264_SHARED_TOOL "Build x264 tool with SHARED libx264" OFF "X264_ENABLE_TOOL AND X264_ENABLE_SHARED" OFF)
cmake_dependent_option(X264_STATIC_TOOL "Build x264 tool with STATIC libx264" OFF "X264_ENABLE_TOOL AND X264_ENABLE_STATIC" OFF)


if (${X264_ENABLE_TOOL})
    if(NOT ${X264_STATIC_TOOL} AND NOT ${X264_SHARED_TOOL})
        message(FATAL_ERROR "When X264_ENABLE_TOOL is enabled X264_STATIC_TOOL OR X264_SHARED_TOOL SHOULD BE enabled as well")
    endif()

    if (${X264_STATIC_TOOL} AND ${X264_SHARED_TOOL})
        message(FATAL_ERROR "X264_STATIC_TOOL AND X264_SHARED_TOOL is not allowed")
    endif()

    if (${X264_STATIC_TOOL} AND NOT ${X264_ENABLE_STATIC})
        message(FATAL_ERROR "Cannot build X264_STATIC_TOOL without X264_ENABLE_STATIC")
    endif()

    if (${X264_SHARED_TOOL} AND NOT ${X264_ENABLE_SHARED})
        message(FATAL_ERROR "Cannot build X264_SHARED_TOOL without X264_ENABLE_SHARED")
    endif()
endif()


option(X264_DISABLE_WARNINGS "Disable compilation warnings" ON)
mark_as_advanced(X264_DISABLE_WARNINGS)



set(CMAKE_LINK_LIBRARIES_ONLY_TARGETS ON)

list(APPEND X264_ALL_OBJECT_TARGETS)

add_library(x264.config INTERFACE)
add_library(x264.headers INTERFACE)

function(x264_add_object_libraries LIBRARY_NAME SOURCES)
    add_library(x264.${LIBRARY_NAME}.objects OBJECT)
    target_sources(x264.${LIBRARY_NAME}.objects PRIVATE ${${SOURCES}})
    target_link_libraries(x264.${LIBRARY_NAME}.objects PRIVATE x264.config x264.headers)
    list(APPEND X264_ALL_OBJECT_TARGETS x264.${LIBRARY_NAME}.objects)

    if (X264_ENABLE_SHARED)
    add_library(x264.${LIBRARY_NAME}.objects_pic OBJECT)
    target_sources(x264.${LIBRARY_NAME}.objects_pic PRIVATE ${${SOURCES}})
    target_link_libraries(x264.${LIBRARY_NAME}.objects_pic PRIVATE x264.config x264.headers)
    set_target_properties(x264.${LIBRARY_NAME}.objects_pic PROPERTIES POSITION_INDEPENDENT_CODE ON)
    list(APPEND X264_ALL_OBJECT_TARGETS x264.${LIBRARY_NAME}.objects_pic)
    set(X264_ALL_OBJECT_TARGETS ${X264_ALL_OBJECT_TARGETS} PARENT_SCOPE)
    endif()
endfunction()

find_library(X264_MATH m)
add_library(x264.math INTERFACE)

set(X264_MATH_AS_LINK_OPTION OFF)
set(X264_MATH_AS_EXTERNAL_LIBRARY OFF)

if ("X264_MATH-NOTFOUND" STREQUAL ${X264_MATH})
    set(X264_MATH_AS_LINK_OPTION ON)
else()
    set(X264_MATH_AS_EXTERNAL_LIBRARY ON)
endif()

if (${X264_MATH_AS_LINK_OPTION})
    target_link_options(x264.math INTERFACE "-lm")
endif()

if(${X264_MATH_AS_EXTERNAL_LIBRARY})
    target_link_libraries(x264.math INTERFACE ${X264_MATH})
endif()

set(X264_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(X264_GENERATED_INCLUDE_DIR "${X264_GENERATED_DIR}/include")
file(GENERATE OUTPUT "${X264_GENERATED_INCLUDE_DIR}/config.h" CONTENT "")
target_include_directories(x264.config INTERFACE ${X264_GENERATED_INCLUDE_DIR})

file(READ x264.h HEADER_CONTENT)
string(REGEX MATCH "#define X264_BUILD *[0-9]*" X264_BUILD_DEFINE_LINE ${HEADER_CONTENT})
string(REPLACE "#define X264_BUILD" "" X264_BUILD_DEFINE_VALUE ${X264_BUILD_DEFINE_LINE})
string(STRIP ${X264_BUILD_DEFINE_VALUE} X264_BUILD_DEFINE_VALUE)

set(X264_GPL_DEFINE_VALUE "1")
set(X264_INTERLACED_DEFINE_VALUE "1")
set(X264_BIT_DEPTH_DEFINE_VALUE "0")
set(X264_CHROMA_FORMAT_DEFINE_VALUE "0")

set(X264_REV_DEFINE_VALUE "0")
set(X264_REV_DIFF_DEFINE_VALUE "0")
set(X264_VERSION_DEFINE_VALUE "\" r\"")
set(X264_POINTVER_DEFINE_VALUE "\"0.${X264_BUILD_DEFINE_VALUE}.x\"")

# include(FindGit)
# if(${Git_FOUND})
#     execute_process(COMMAND ${GIT_EXECUTABLE} "rev-list" "origin/master..HEAD" OUTPUT_VARIABLE X264_VER_DIFF)
#     string(REGEX MATCHALL "\n" X264_VER_DIFF ${X264_VER_DIFF})
#     list(LENGTH X264_VER_DIFF X264_VER_DIFF)

#     execute_process(COMMAND ${GIT_EXECUTABLE} "rev-list" "HEAD" OUTPUT_VARIABLE X264_LOCALVER)
#     string(REGEX MATCHALL "\n" X264_LOCALVER ${X264_LOCALVER})
#     list(LENGTH X264_LOCALVER X264_LOCALVER)

#     math(EXPR X264_REV_DEFINE_VALUE "${X264_LOCALVER}-${X264_VER_DIFF}")
#     set(X264_REV_DIFF_DEFINE_VALUE ${X264_VER_DIFF})

#     execute_process(COMMAND ${GIT_EXECUTABLE} "status" OUTPUT_VARIABLE X264_IS_MODIFIED)
#     string(REGEX MATCHALL "modified:" X264_IS_MODIFIED ${X264_IS_MODIFIED})
#     list(LENGTH X264_IS_MODIFIED X264_IS_MODIFIED)
#     set(X264_MOD_MARK "")
#     if(${X264_IS_MODIFIED} GREATER 0)
#         set(X264_MOD_MARK "M")
#     endif()

#     execute_process(COMMAND ${GIT_EXECUTABLE} "rev-list" "-n" "1" "HEAD" OUTPUT_VARIABLE X264_HEAD_GIT_HASH)
#     string(SUBSTRING ${X264_HEAD_GIT_HASH} 0 7 X264_HEAD_GIT_HASH)

#     set(X264_VERSION_SUFFIX "${X264_REV_DEFINE_VALUE}+${X264_VER_DIFF}${X264_MOD_MARK} ${X264_HEAD_GIT_HASH}")

#     set(X264_VERSION_DEFINE_VALUE "\" r${X264_VERSION_SUFFIX}\"")
#     set(X264_POINTVER_DEFINE_VALUE "\"0.${X264_BUILD_DEFINE_VALUE}.${X264_VERSION_SUFFIX}\"")
# endif()

target_compile_definitions(x264.config INTERFACE
    HAVE_STRTOK_R=1

    HAVE_GPL=${X264_GPL_DEFINE_VALUE}
    HAVE_INTERLACED=0
    HAVE_BITDEPTH8=1
    BIT_DEPTH=8
)

if(X264_DISABLE_WARNINGS)
    target_compile_options(x264.config INTERFACE "-w")
endif()

list(APPEND X264_CONFIG_DEFINES
    X264_GPL
    X264_INTERLACED
    X264_BIT_DEPTH
    X264_CHROMA_FORMAT
    X264_REV
    X264_REV_DIFF
    X264_VERSION
    X264_POINTVER
)

foreach(DEFINE_NAME ${X264_CONFIG_DEFINES})
    message(STATUS "(x264.config) ${DEFINE_NAME}=${${DEFINE_NAME}_DEFINE_VALUE}")
    target_compile_definitions(x264.config INTERFACE ${DEFINE_NAME}=${${DEFINE_NAME}_DEFINE_VALUE})
    list(APPEND X264_CONFIG_H_FILE_CONTENT_LINES "#define ${DEFINE_NAME} ${${DEFINE_NAME}_DEFINE_VALUE}")
endforeach()
list(JOIN X264_CONFIG_H_FILE_CONTENT_LINES "\n" X264_CONFIG_H_FILE_CONTENT)
file(GENERATE OUTPUT "${X264_GENERATED_INCLUDE_DIR}/x264_config.h" CONTENT ${X264_CONFIG_H_FILE_CONTENT})

target_include_directories(x264.headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})


list(APPEND X264_COMMON_SOURCES
    common/osdep.c
    common/base.c
    common/cpu.c
    common/tables.c
    common/mc.c
    common/predict.c
    common/pixel.c
    common/macroblock.c
    common/frame.c
    common/dct.c
    common/cabac.c
    common/common.c
    common/rectangle.c
    common/set.c
    common/quant.c
    common/deblock.c
    common/vlc.c
    common/mvpred.c
    common/bitstream.c
)

x264_add_object_libraries(common X264_COMMON_SOURCES)

list(APPEND X264_ENCODER_SOURCES
    encoder/analyse.c
    encoder/api.c
    encoder/cabac.c
    encoder/cavlc.c
    encoder/encoder.c
    encoder/lookahead.c
    encoder/macroblock.c
    encoder/me.c
    encoder/ratecontrol.c
    encoder/set.c
    encoder/slicetype-cl.c
)

x264_add_object_libraries(encoder X264_ENCODER_SOURCES)

list(APPEND X264_FILTER_SOURCES
    filters/filters.c
    filters/video/cache.c
    filters/video/crop.c
    filters/video/depth.c
    filters/video/fix_vfr_pts.c
    filters/video/internal.c
    filters/video/resize.c
    filters/video/select_every.c
    filters/video/source.c
    filters/video/video.c
)

x264_add_object_libraries(filter X264_FILTER_SOURCES)

list(APPEND X264_INPUT_SOURCES
    input/input.c
    input/timecode.c
    input/y4m.c
    input/raw.c
)

x264_add_object_libraries(input X264_INPUT_SOURCES)

list(APPEND X264_OUTPUT_SOURCES
    output/flv.c
    output/flv_bytestream.c
    output/matroska.c
    output/matroska_ebml.c
    output/raw.c
)

x264_add_object_libraries(output X264_OUTPUT_SOURCES)


set(APPEND_STATIC_TO_ARTEFACT_NAME FALSE)
if(WIN32)
    set(APPEND_STATIC_TO_ARTEFACT_NAME TRUE)
endif()

set(X264_STATIC_ARTEFACT_NAME "x264" CACHE STRING "x264 static library name prefix")
mark_as_advanced(X264_STATIC_ARTEFACT_NAME)
if(${APPEND_STATIC_TO_ARTEFACT_NAME})
    set(X264_STATIC_ARTEFACT_NAME "x264_static")
endif()

if(X264_ENABLE_STATIC)
    add_library(x264.static STATIC)
    set_property(TARGET x264.static PROPERTY OUTPUT_NAME ${X264_STATIC_ARTEFACT_NAME})
    target_sources(x264.static PRIVATE
        $<TARGET_OBJECTS:x264.common.objects>
        $<TARGET_OBJECTS:x264.encoder.objects>
    )
endif()


if(X264_ENABLE_SHARED)
    add_library(x264.static_pic STATIC)
    set_property(TARGET x264.static_pic PROPERTY OUTPUT_NAME "${X264_STATIC_ARTEFACT_NAME}_PIC")
    target_sources(x264.static_pic PUBLIC
        $<TARGET_OBJECTS:x264.common.objects_pic>
        $<TARGET_OBJECTS:x264.encoder.objects_pic>
    )
    add_library(x264.shared SHARED)
    set_property(TARGET x264.shared PROPERTY OUTPUT_NAME "x264")
    target_link_libraries(x264.shared PRIVATE x264.static_pic)
    target_sources(x264.shared PRIVATE $<TARGET_OBJECTS:x264.static_pic>)
endif()


if(X264_ENABLE_STATIC)
    target_link_libraries(x264.static PUBLIC x264.math)
endif()


if(X264_ENABLE_SHARED)
    target_link_libraries(x264.shared PUBLIC x264.math)
endif()

if(X264_ENABLE_TOOL)
    add_executable(x264)
    target_sources(x264 PRIVATE
        x264.c
        autocomplete.c
    )
    target_link_libraries(x264 PRIVATE
        x264.config
        x264.headers
        x264.filter.objects
        x264.input.objects
        x264.output.objects
    )
endif()

if(X264_ENABLE_TOOL AND X264_STATIC_TOOL)
    target_link_libraries(x264 PRIVATE x264.static)
endif()

if(X264_ENABLE_TOOL AND X264_SHARED_TOOL)
    target_link_libraries(x264 PRIVATE x264.shared)
endif()

if(NOT X264_CMAKE_INSTALL)
    return()
endif()

install(FILES
    x264.h
    ${X264_GENERATED_INCLUDE_DIR}/x264_config.h
    DESTINATION "include")

if(X264_ENABLE_TOOL)
    install(TARGETS x264 DESTINATION "bin")
endif()

if(X264_ENABLE_SHARED)
    install(TARGETS x264.shared DESTINATION "lib")
endif()

if(X264_ENABLE_STATIC)
    install(TARGETS x264.static DESTINATION "lib")
endif()
