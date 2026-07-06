# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-07
# * 版本: v1.0
# * 更新记录: 初版创建 fault_injection 模块构建目标
# * 描述: 定义故障注入模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_fault_injection INTERFACE)

target_compile_features(lockstep_fault_injection INTERFACE cxx_std_17)

target_include_directories(lockstep_fault_injection
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
