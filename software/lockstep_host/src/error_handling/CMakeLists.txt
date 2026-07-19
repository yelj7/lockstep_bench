# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-06
# * 版本: v1.0
# * 更新记录: 初版创建 error_handling 模块构建目标
# * 描述: 定义统一错误处理、日志和维护检查模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_error_handling STATIC
    diagnostic_rules.cpp
    diagnostic_rules.h
    error_registry.cpp
    error_registry.h
)

target_link_libraries(lockstep_error_handling
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
)

target_compile_features(lockstep_error_handling PUBLIC cxx_std_17)

target_include_directories(lockstep_error_handling
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(lockstep_error_handling PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_error_handling PRIVATE -Wall -Wextra -Wpedantic)
endif()
