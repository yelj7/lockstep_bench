# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-07
# * 版本: v1.0
# * 更新记录: 初版创建 workspace 模块构建目标
# * 描述: 定义工作区、任务与任务级产物路径模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_workspace STATIC
    workspace_manager.cpp
    workspace_manager.h
)

target_link_libraries(lockstep_workspace
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
)

target_compile_features(lockstep_workspace PUBLIC cxx_std_17)

target_include_directories(lockstep_workspace
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(lockstep_workspace PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_workspace PRIVATE -Wall -Wextra -Wpedantic)
endif()
