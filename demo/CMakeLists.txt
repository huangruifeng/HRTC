set(target_name "hrtc_demo")

if(HRTC_WINDOWS)
set(CMAKE_WIN32_EXECUTABLE ON)
file(GLOB whiteBoardSource  Whiteboard/*.h Whiteboard/*.cpp)
add_executable(${target_name} main.cpp model.h ${whiteBoardSource})
target_link_libraries(${target_name} hrtc_engine hrtc_whiteboard)
endif()

# ---------------- Qt board demo (optional) ----------------
# 需要 Qt5 (Widgets + Network，AI 助手 HTTP 用)；未安装时自动跳过，不影响其余构建
find_package(Qt5 COMPONENTS Widgets Network QUIET)
if(NOT Qt5Widgets_FOUND OR NOT Qt5Network_FOUND)
    # 自动探测常见 Qt 安装路径
    foreach(qt_candidate
            "C:/Qt/Qt5.14.2/5.14.2/msvc2015_64"
            "C:/Qt/Qt5.14.2/msvc2015_64"
            "C:/Qt/5.14.2/msvc2015_64")
        if(EXISTS "${qt_candidate}/lib/cmake/Qt5Widgets")
            list(APPEND CMAKE_PREFIX_PATH "${qt_candidate}")
            find_package(Qt5 COMPONENTS Widgets Network QUIET)
            if(Qt5Widgets_FOUND AND Qt5Network_FOUND)
                break()
            endif()
        endif()
    endforeach()
endif()

if(Qt5Widgets_FOUND AND Qt5Network_FOUND)
    add_subdirectory(QtBoard)
else()
    message(STATUS "Qt5 (Widgets+Network) not found, skip Qt board demo (set CMAKE_PREFIX_PATH to Qt dir to enable)")
endif()

# ---------------- protocol round-trip test ---------------- 
add_subdirectory(ProtocolTest)

# ---------------- join probe (console E2E client) ----------------
add_subdirectory(JoinProbe)