# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-07
# * 版本: v1.0
# * 更新记录: 初版创建 workspace 模块构建目标
# * 描述: 定义工作区、任务与任务级产物路径模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_workspace INTERFACE)

target_compile_features(lockstep_workspace INTERFACE cxx_std_17)

target_include_directories(lockstep_workspace
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
