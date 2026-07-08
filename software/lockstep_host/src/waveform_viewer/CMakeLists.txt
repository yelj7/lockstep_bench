# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-08
# * 版本: v1.1
# * 更新记录: 接入固定 trace analysis 读取与展示模型实现
# * 描述: 定义自研波形显示模块的 CMake 目标
# **********************************************************/

add_library(lockstep_waveform_viewer STATIC
    waveform_trace_viewer.cpp
    waveform_trace_viewer.h
)

target_link_libraries(lockstep_waveform_viewer
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
)

target_compile_features(lockstep_waveform_viewer PUBLIC cxx_std_17)

target_include_directories(lockstep_waveform_viewer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(lockstep_waveform_viewer PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_waveform_viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()
