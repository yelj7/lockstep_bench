# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-07
# * 版本: v1.0
# * 更新记录: 初版创建 acquisition 模块构建目标
# * 描述: 定义采集控制与帧流处理模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_acquisition INTERFACE)

target_compile_features(lockstep_acquisition INTERFACE cxx_std_17)

target_include_directories(lockstep_acquisition
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
