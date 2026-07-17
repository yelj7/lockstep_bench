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

add_executable(lockstep_scalar_1024_vcd_import_test
    scalar_1024_vcd_import_test.cpp
)
target_link_libraries(lockstep_scalar_1024_vcd_import_test
    PRIVATE
        lockstep_protocol_analyzer
        lockstep_waveform_viewer
        lockstep_ui_common
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
)
target_compile_features(lockstep_scalar_1024_vcd_import_test PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(lockstep_scalar_1024_vcd_import_test PRIVATE /W4 /permissive-)
else()
target_compile_options(lockstep_scalar_1024_vcd_import_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME lockstep_scalar_1024_vcd_import_test COMMAND lockstep_scalar_1024_vcd_import_test)
set_tests_properties(lockstep_scalar_1024_vcd_import_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(lockstep_sampling_capture_test sampling_capture_test.cpp)
target_link_libraries(lockstep_sampling_capture_test PRIVATE lockstep_acquisition Qt${QT_VERSION_MAJOR}::Core)
target_compile_features(lockstep_sampling_capture_test PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(lockstep_sampling_capture_test PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_sampling_capture_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME lockstep_sampling_capture_test COMMAND lockstep_sampling_capture_test $<TARGET_FILE:lockstep_ui_preview>)

add_executable(lockstep_waveform_ui_test
    waveform_ui_test.cpp
)

target_link_libraries(lockstep_waveform_ui_test
    PRIVATE
        lockstep_ui_common
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
)

target_compile_features(lockstep_waveform_ui_test PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(lockstep_waveform_ui_test PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_waveform_ui_test PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME lockstep_waveform_ui_test COMMAND lockstep_waveform_ui_test)
set_tests_properties(lockstep_waveform_ui_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(lockstep_report_generator_test
    report_generator_test.cpp
)

target_link_libraries(lockstep_report_generator_test
    PRIVATE
        lockstep_reporting
        Qt${QT_VERSION_MAJOR}::Core
)

target_compile_features(lockstep_report_generator_test PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(lockstep_report_generator_test PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_report_generator_test PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME lockstep_report_generator_test COMMAND lockstep_report_generator_test)

add_executable(lockstep_report_ui_test
    report_ui_test.cpp
)

target_link_libraries(lockstep_report_ui_test
    PRIVATE
        lockstep_ui_common
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
)

target_compile_features(lockstep_report_ui_test PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(lockstep_report_ui_test PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_report_ui_test PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME lockstep_report_ui_test COMMAND lockstep_report_ui_test)
set_tests_properties(lockstep_report_ui_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(
    NAME lockstep_package_resources_test
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_RESOURCES=${CMAKE_SOURCE_DIR}/resources
        -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/package_resources_test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/package_resources_test.cmake
)

add_test(
    NAME lockstep_product_python_dependency_test
    COMMAND ${CMAKE_COMMAND}
        -DPRODUCT_SOURCE_DIR=${CMAKE_SOURCE_DIR}/src
        -DAPP_RESOURCE_DIR=$<TARGET_FILE_DIR:lockstep_ui_preview>/resources
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/product_python_dependency_test.cmake
)

add_test(
    NAME lockstep_ui_preview_service_mode_test
    COMMAND $<TARGET_FILE:lockstep_ui_preview>
        --request ${CMAKE_CURRENT_BINARY_DIR}/missing-debug-request.json
)
set_tests_properties(lockstep_ui_preview_service_mode_test PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME lockstep_ui_preview_service_mode_equals_test
    COMMAND $<TARGET_FILE:lockstep_ui_preview>
        --request=${CMAKE_CURRENT_BINARY_DIR}/missing-debug-request.json
)
set_tests_properties(lockstep_ui_preview_service_mode_equals_test PROPERTIES WILL_FAIL TRUE)

if(UNIX)
    add_test(
        NAME lockstep_build_env_scripts_test
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/build_env_scripts_test.sh ${CMAKE_SOURCE_DIR}
    )
endif()
