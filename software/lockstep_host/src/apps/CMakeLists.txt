# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-14
# * 版本: v2.3
# * 更新记录: 主分支恢复复制 FTD3XX 运行库并保留工作台确认弹窗模块。
# * 描述: 构建 lockstep_ui_preview、确认弹窗模块并复制 D3XX 与运行资源。
# **********************************************************/

set(LOCKSTEP_UI_PREVIEW_SOURCES
    ui_preview_main.cpp
    workbench_controller.cpp
    workbench_controller.h
    debug_service_main.cpp
    debug_service_entry.h
)

add_library(lockstep_workbench_dialogs STATIC
    workbench_dialogs.cpp
    workbench_dialogs.h
)
target_link_libraries(lockstep_workbench_dialogs
    PUBLIC
        lockstep_ui_common
        Qt${QT_VERSION_MAJOR}::Widgets
)
target_compile_features(lockstep_workbench_dialogs PUBLIC cxx_std_17)
target_include_directories(lockstep_workbench_dialogs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(lockstep_ui_preview ${LOCKSTEP_UI_PREVIEW_SOURCES})
else()
    add_executable(lockstep_ui_preview ${LOCKSTEP_UI_PREVIEW_SOURCES})
endif()

find_package(hidapi CONFIG REQUIRED)

set(LOCKSTEP_HIDAPI_TARGET hidapi::hidapi)
if(UNIX AND TARGET hidapi::hidapi-hidraw)
    set(LOCKSTEP_HIDAPI_TARGET hidapi::hidapi-hidraw)
endif()

file(GLOB_RECURSE LOCKSTEP_RESOURCE_SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/resources/*"
)
set(LOCKSTEP_PACKAGED_RESOURCES_DIR "${CMAKE_BINARY_DIR}/packaged-resources"
    CACHE INTERNAL "Generated resources used by runtime and packages")
set(LOCKSTEP_PACKAGED_MANIFEST "${LOCKSTEP_PACKAGED_RESOURCES_DIR}/manifest.json")

add_custom_command(
    OUTPUT "${LOCKSTEP_PACKAGED_MANIFEST}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${LOCKSTEP_PACKAGED_RESOURCES_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "${LOCKSTEP_PACKAGED_RESOURCES_DIR}"
    DEPENDS ${LOCKSTEP_RESOURCE_SOURCE_FILES}
    COMMENT "Copy packaged runtime resources"
    VERBATIM
)

add_custom_target(lockstep_package_resources ALL
    DEPENDS "${LOCKSTEP_PACKAGED_MANIFEST}"
)
add_dependencies(lockstep_ui_preview lockstep_package_resources)

target_link_libraries(lockstep_ui_preview
    PRIVATE
        lockstep_acquisition
        lockstep_fault_injection
        lockstep_error_handling
        lockstep_protocol_analyzer
        lockstep_reporting
        lockstep_resources
        lockstep_target_control
        lockstep_ui_common
        lockstep_waveform_viewer
        lockstep_workspace
        lockstep_workbench_dialogs
        ${LOCKSTEP_HIDAPI_TARGET}
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::SerialPort
        Qt${QT_VERSION_MAJOR}::Widgets
)

target_compile_features(lockstep_ui_preview PRIVATE cxx_std_17)
target_compile_definitions(lockstep_ui_preview PRIVATE LOCKSTEP_APP_VERSION="${PROJECT_VERSION}")
set_target_properties(lockstep_ui_preview PROPERTIES WIN32_EXECUTABLE TRUE)

if(WIN32)
    find_file(LOCKSTEP_HIDAPI_RUNTIME_DLL
        NAMES libhidapi-0.dll
        PATHS
            "${CMAKE_PREFIX_PATH}/bin"
            "D:/Program Files/msys64/ucrt64/bin"
        NO_DEFAULT_PATH
    )
    if(LOCKSTEP_HIDAPI_RUNTIME_DLL)
        add_custom_command(
            TARGET lockstep_ui_preview
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LOCKSTEP_HIDAPI_RUNTIME_DLL}"
                $<TARGET_FILE_DIR:lockstep_ui_preview>
            COMMENT "Copy hidapi runtime next to lockstep_ui_preview"
        )
    endif()

    find_file(LOCKSTEP_FTD3XX_RUNTIME_DLL
        NAMES FTD3XX.dll
        PATHS
            "${CMAKE_SOURCE_DIR}/../../software/lockstep_host/resources"
        NO_DEFAULT_PATH
    )
    if(LOCKSTEP_FTD3XX_RUNTIME_DLL)
        add_custom_command(
            TARGET lockstep_ui_preview
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LOCKSTEP_FTD3XX_RUNTIME_DLL}"
                $<TARGET_FILE_DIR:lockstep_ui_preview>
            COMMENT "Copy FT601 D3XX runtime next to lockstep_ui_preview"
        )
    else()
        message(FATAL_ERROR "FTD3XX.dll is required for the Windows product")
    endif()
endif()

if(MSVC)
    target_compile_options(lockstep_ui_preview PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_ui_preview PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_target(lockstep_copy_runtime_resources
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:lockstep_ui_preview>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${LOCKSTEP_PACKAGED_RESOURCES_DIR}"
        $<TARGET_FILE_DIR:lockstep_ui_preview>/resources
    COMMENT "Refresh lockstep runtime resources next to lockstep_ui_preview"
)

add_custom_command(
    TARGET lockstep_ui_preview
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:lockstep_ui_preview>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${LOCKSTEP_PACKAGED_RESOURCES_DIR}"
        $<TARGET_FILE_DIR:lockstep_ui_preview>/resources
    COMMENT "Copy lockstep resources next to lockstep_ui_preview"
)

set(LOCKSTEP_FAULT_SCRIPT_DIR "${CMAKE_SOURCE_DIR}/../../../software/error_inj")
add_custom_command(
    TARGET lockstep_ui_preview
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:lockstep_ui_preview>/resources/error_injection
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LOCKSTEP_FAULT_SCRIPT_DIR}/find_sem_addr.py"
        "${LOCKSTEP_FAULT_SCRIPT_DIR}/clear_pcap_for_icap.tcl"
        "${LOCKSTEP_FAULT_SCRIPT_DIR}/sem_uart_inject_ranges.py"
        $<TARGET_FILE_DIR:lockstep_ui_preview>/resources/error_injection
    COMMENT "Copy fixed fault-injection scripts next to lockstep_ui_preview"
    VERBATIM
)
