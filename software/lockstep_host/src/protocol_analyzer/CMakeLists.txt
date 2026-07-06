# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-06
# * 版本: v1.0
# * 更新记录: 初版创建 protocol_analyzer 模块构建目标
# * 描述: 定义自研协议分析模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_protocol_analyzer INTERFACE)

target_compile_features(lockstep_protocol_analyzer INTERFACE cxx_std_17)

target_include_directories(lockstep_protocol_analyzer
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
