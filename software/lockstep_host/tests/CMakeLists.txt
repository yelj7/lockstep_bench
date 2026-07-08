# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-08
# * 版本: v1.0
# * 更新记录: 初版创建 M11/M12 自动化测试目标
# * 描述: 定义波形显示与协议解析模块的 CTest 测试
# **********************************************************/

add_executable(lockstep_trace_analysis_test
    lockstep_trace_analysis_test.cpp
)

target_link_libraries(lockstep_trace_analysis_test
    PRIVATE
        lockstep_error_handling
        lockstep_protocol_analyzer
        lockstep_waveform_viewer
        Qt${QT_VERSION_MAJOR}::Core
)

target_compile_features(lockstep_trace_analysis_test PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(lockstep_trace_analysis_test PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_trace_analysis_test PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME lockstep_trace_analysis_test COMMAND lockstep_trace_analysis_test)
