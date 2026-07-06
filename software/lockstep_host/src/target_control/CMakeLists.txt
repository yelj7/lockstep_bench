# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-07
# * 版本: v1.0
# * 更新记录: 初版创建 target_control 模块构建目标
# * 描述: 定义目标连接、调试访问、烧写回读与运行控制模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_target_control INTERFACE)

target_compile_features(lockstep_target_control INTERFACE cxx_std_17)

target_include_directories(lockstep_target_control
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
