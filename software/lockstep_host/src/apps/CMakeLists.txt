# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-14
# * 版本: v2.3
# * 更新记录: 将 FT601 libusbK 自动绑定助手和 Zadig 嵌入 Windows 产品。
# * 描述: 构建 lockstep_ui_preview、确认弹窗模块并复制 libusb 与运行资源。
# **********************************************************/

set(LOCKSTEP_UI_PREVIEW_SOURCES
    ui_preview_main.cpp
    ft601_driver_bootstrap.cpp
    ft601_driver_bootstrap.h
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
        lockstep_workflow
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

    find_file(LOCKSTEP_LIBUSB_RUNTIME_DLL
        NAMES libusb-1.0.dll
        PATHS
            "${CMAKE_PREFIX_PATH}/bin"
            "D:/Program Files/msys64/ucrt64/bin"
        NO_DEFAULT_PATH
    )
    if(LOCKSTEP_LIBUSB_RUNTIME_DLL)
        add_custom_command(
            TARGET lockstep_ui_preview
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LOCKSTEP_LIBUSB_RUNTIME_DLL}"
                $<TARGET_FILE_DIR:lockstep_ui_preview>
            COMMENT "Copy FT601 libusb runtime next to lockstep_ui_preview"
        )
    else()
        message(FATAL_ERROR "libusb-1.0.dll is required for the Windows product")
    endif()

    find_file(LOCKSTEP_ZADIG_EXECUTABLE
        NAMES zadig-2.9.exe
        PATHS
            "D:/tool"
            "${CMAKE_SOURCE_DIR}/packaging/windows/raw"
        NO_DEFAULT_PATH
    )
    if(LOCKSTEP_ZADIG_EXECUTABLE)
        if(QT_VERSION_MAJOR GREATER_EQUAL 6)
            set_source_files_properties(
                "${CMAKE_SOURCE_DIR}/resources/windows/ft601_driver/ensure_ft601_libusbk.ps1"
                PROPERTIES QT_RESOURCE_ALIAS "ensure_ft601_libusbk.ps1")
            set_source_files_properties(
                "${CMAKE_SOURCE_DIR}/resources/windows/ft601_driver/zadig.ini"
                PROPERTIES QT_RESOURCE_ALIAS "zadig.ini")
            set_source_files_properties("${LOCKSTEP_ZADIG_EXECUTABLE}"
                PROPERTIES QT_RESOURCE_ALIAS "zadig-2.9.exe")
            qt_add_resources(lockstep_ui_preview ft601_driver_bootstrap_resources
                PREFIX "/ft601_driver"
                FILES
                    "${CMAKE_SOURCE_DIR}/resources/windows/ft601_driver/ensure_ft601_libusbk.ps1"
                    "${CMAKE_SOURCE_DIR}/resources/windows/ft601_driver/zadig.ini"
                    "${LOCKSTEP_ZADIG_EXECUTABLE}"
            )
        else()
            file(TO_CMAKE_PATH "${LOCKSTEP_ZADIG_EXECUTABLE}" LOCKSTEP_ZADIG_QRC_PATH)
            set(LOCKSTEP_HELPER_QRC_PATH
                "${CMAKE_SOURCE_DIR}/resources/windows/ft601_driver/ensure_ft601_libusbk.ps1")
            set(LOCKSTEP_ZADIG_INI_QRC_PATH
                "${CMAKE_SOURCE_DIR}/resources/windows/ft601_driver/zadig.ini")
            set(LOCKSTEP_FT601_QRC "${CMAKE_CURRENT_BINARY_DIR}/ft601_driver_bootstrap.qrc")
            file(WRITE "${LOCKSTEP_FT601_QRC}"
                "<RCC><qresource prefix=\"/ft601_driver\">"
                "<file alias=\"ensure_ft601_libusbk.ps1\">${LOCKSTEP_HELPER_QRC_PATH}</file>"
                "<file alias=\"zadig.ini\">${LOCKSTEP_ZADIG_INI_QRC_PATH}</file>"
                "<file alias=\"zadig-2.9.exe\">${LOCKSTEP_ZADIG_QRC_PATH}</file>"
                "</qresource></RCC>")
            qt5_add_resources(LOCKSTEP_FT601_RESOURCE_SOURCES "${LOCKSTEP_FT601_QRC}")
            target_sources(lockstep_ui_preview PRIVATE ${LOCKSTEP_FT601_RESOURCE_SOURCES})
        endif()
    else()
        message(FATAL_ERROR "Validated Zadig 2.9 is required for the Windows product")
    endif()
    target_link_libraries(lockstep_ui_preview PRIVATE advapi32 cfgmgr32 setupapi shell32)
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
