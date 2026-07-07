# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-07
# * 版本: v1.0
# * 更新记录: 初版创建 workflow 模块构建目标
# * 描述: 定义研发测试流程编排模块的 CMake 目标占位
# **********************************************************/

add_library(lockstep_workflow STATIC
    workflow_engine.cpp
    workflow_engine.h
)

target_link_libraries(lockstep_workflow
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
)

target_compile_features(lockstep_workflow PUBLIC cxx_std_17)

target_include_directories(lockstep_workflow
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(lockstep_workflow PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_workflow PRIVATE -Wall -Wextra -Wpedantic)
endif()
