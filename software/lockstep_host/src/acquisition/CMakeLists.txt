# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-14
# * 版本: v2.0
# * 更新记录: 接入原生采集帧、采集会话、D3XX 传输和 1024 路 VCD 导出
# * 描述: 构建唯一上位机产品使用的采集静态库。
# **********************************************************/

add_library(lockstep_acquisition STATIC
    sampling_capture.cpp
    sampling_capture.h
)

target_link_libraries(lockstep_acquisition PUBLIC Qt${QT_VERSION_MAJOR}::Core)
target_compile_features(lockstep_acquisition PUBLIC cxx_std_17)
target_include_directories(lockstep_acquisition PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(lockstep_acquisition PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_acquisition PRIVATE -Wall -Wextra -Wpedantic)
endif()
