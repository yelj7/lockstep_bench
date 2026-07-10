# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-07
# * 版本: v1.0
# * 更新记录: 初版创建 target_control 模块构建目标
# * 描述: 定义目标连接、调试访问、烧写回读与运行控制模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_target_control STATIC
    target_control.cpp
    target_control.h
)

target_link_libraries(lockstep_target_control
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
)

target_compile_features(lockstep_target_control PUBLIC cxx_std_17)

target_include_directories(lockstep_target_control
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(lockstep_target_control PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_target_control PRIVATE -Wall -Wextra -Wpedantic)
endif()
