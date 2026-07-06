# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-07
# * 版本: v1.0
# * 更新记录: 初版创建 workflow 模块构建目标
# * 描述: 定义研发测试流程编排模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_workflow INTERFACE)

target_compile_features(lockstep_workflow INTERFACE cxx_std_17)

target_include_directories(lockstep_workflow
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
