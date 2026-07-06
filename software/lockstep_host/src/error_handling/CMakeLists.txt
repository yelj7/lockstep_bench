# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-06
# * 版本: v1.0
# * 更新记录: 初版创建 error_handling 模块构建目标
# * 描述: 定义统一错误处理、日志和维护检查模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_error_handling INTERFACE)

target_compile_features(lockstep_error_handling INTERFACE cxx_std_17)

target_include_directories(lockstep_error_handling
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
