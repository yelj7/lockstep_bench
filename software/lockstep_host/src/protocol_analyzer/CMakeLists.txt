# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-08
# * 版本: v1.1
# * 更新记录: 接入固定 trace VCD 协议解析实现
# * 描述: 定义自研协议分析模块的 CMake 目标
# **********************************************************/

add_library(lockstep_protocol_analyzer STATIC
    protocol_analysis.cpp
    protocol_analysis.h
)

target_link_libraries(lockstep_protocol_analyzer
    PUBLIC
        lockstep_error_handling
        Qt${QT_VERSION_MAJOR}::Core
)

target_compile_features(lockstep_protocol_analyzer PUBLIC cxx_std_17)

target_include_directories(lockstep_protocol_analyzer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(lockstep_protocol_analyzer PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_protocol_analyzer PRIVATE -Wall -Wextra -Wpedantic)
endif()
