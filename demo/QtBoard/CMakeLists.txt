# Qt 黑板 demo：QGraphicsView 渲染层 + 白板数据层（Whiteboard/Transport 不动）
# 目录结构按功能划分：
#   app     - 程序入口、主窗口、加入房间对话框
#   data    - 白板数据层、网络会话、基础工具
#   view    - 画布视图、元素选择框、调试探针
#   panels  - 弹出面板（笔/擦除/取色/更多/页面/设置/幻灯片管理）
#   toolbar - 底部工具栏与自绘图标
#   resources - 图标/图片与资源清单
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(_qtBoardDirs app data view panels toolbar)
set(qtBoardSource "")
foreach(_dir ${_qtBoardDirs})
    file(GLOB _dirSource "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/*.cpp")
    list(APPEND qtBoardSource ${_dirSource})
endforeach()

add_executable(hrtc_board_demo ${qtBoardSource} resources/resources.qrc)
target_link_libraries(hrtc_board_demo hrtc_engine hrtc_whiteboard hrtc_transport Qt5::Widgets)

# 各功能目录加入包含路径，源码内保持 #include "XXX.h" 平铺形式
target_include_directories(hrtc_board_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/data
    ${CMAKE_CURRENT_SOURCE_DIR}/view
    ${CMAKE_CURRENT_SOURCE_DIR}/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/toolbar)

if(MSVC)
    target_compile_options(hrtc_board_demo PRIVATE /utf-8)
endif()

# 运行期依赖：把 Qt DLL 与平台插件拷贝到 exe 目录（hrtc_engine.dll 已由全局输出目录放同处）
get_filename_component(_qt_prefix "${Qt5Core_DIR}/../../../" ABSOLUTE)
set(_qt_plugins_dir "${_qt_prefix}/plugins")

add_custom_command(TARGET hrtc_board_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt5::Core>"
        "$<TARGET_FILE:Qt5::Gui>"
        "$<TARGET_FILE:Qt5::Widgets>"
        "$<TARGET_FILE_DIR:hrtc_board_demo>"
    COMMENT "Copying Qt runtime dlls")

foreach(_plugin "qwindows.dll" "qwindowsd.dll")
    if(EXISTS "${_qt_plugins_dir}/platforms/${_plugin}")
        add_custom_command(TARGET hrtc_board_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:hrtc_board_demo>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_qt_plugins_dir}/platforms/${_plugin}"
                "$<TARGET_FILE_DIR:hrtc_board_demo>/platforms"
            COMMENT "Copying Qt platform plugin ${_plugin}")
    endif()
endforeach()
