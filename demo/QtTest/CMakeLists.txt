cmake_minimum_required(VERSION 3.6)
project(QtPathTest)

set(CMAKE_PREFIX_PATH "C:/Qt/Qt5.14.2/5.14.2/msvc2015_64")
find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_executable(qt_path_test main.cpp)
target_link_libraries(qt_path_test Qt5::Widgets)

add_custom_command(TARGET qt_path_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt5::Core>"
        "$<TARGET_FILE:Qt5::Gui>"
        "$<TARGET_FILE:Qt5::Widgets>"
        "$<TARGET_FILE_DIR:qt_path_test>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:qt_path_test>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_PREFIX_PATH}/plugins/platforms/qwindows.dll"
        "${CMAKE_PREFIX_PATH}/plugins/platforms/qwindowsd.dll"
        "$<TARGET_FILE_DIR:qt_path_test>/platforms"
    COMMENT "Copying Qt runtime dlls")
