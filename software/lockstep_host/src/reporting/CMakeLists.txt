# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-07
# * 版本: v1.0
# * 更新记录: 初版创建 reporting 模块构建目标
# * 描述: 定义数据归档与报告生成模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_reporting STATIC
    report_generator.cpp
    report_generator.h
)

target_link_libraries(lockstep_reporting
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
)

target_compile_features(lockstep_reporting PUBLIC cxx_std_17)

target_include_directories(lockstep_reporting
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(lockstep_reporting PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_reporting PRIVATE -Wall -Wextra -Wpedantic)
endif()
