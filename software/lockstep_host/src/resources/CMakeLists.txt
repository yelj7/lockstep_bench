# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-07
# * 版本: v1.0
# * 更新记录: 初版创建 resources 模块构建目标
# * 描述: 定义固化资源与模式配置管理模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_resources INTERFACE)

target_compile_features(lockstep_resources INTERFACE cxx_std_17)

target_include_directories(lockstep_resources
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
