# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-07
# * 版本: v1.0
# * 更新记录: 初版创建 reporting 模块构建目标
# * 描述: 定义数据归档与报告生成模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_reporting INTERFACE)

target_compile_features(lockstep_reporting INTERFACE cxx_std_17)

target_include_directories(lockstep_reporting
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
