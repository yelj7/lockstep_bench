# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-13
# * 版本: v1.0
# * 更新记录: 上位机目标正式化并接入安装资源生成
# * 描述: 定义主程序、调试服务及运行资源构建目标
# **********************************************************/

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(lockstep_host
        ui_preview_main.cpp
        workbench_controller.cpp
        workbench_controller.h
    )
    qt_add_executable(lockstep_debug_service_smoke
        debug_service_smoke_main.cpp
    )
    qt_add_executable(lockstep_debug_service
        debug_service_main.cpp
    )
else()
    add_executable(lockstep_host
        ui_preview_main.cpp
        workbench_controller.cpp
        workbench_controller.h
    )
    add_executable(lockstep_debug_service_smoke
        debug_service_smoke_main.cpp
    )
    add_executable(lockstep_debug_service
        debug_service_main.cpp
    )
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
set(LOCKSTEP_PACKAGED_DEBUG_SERVICE
    "${LOCKSTEP_PACKAGED_RESOURCES_DIR}/debug_adapters/self_debug_service/lockstep_debug_service")

add_custom_command(
    OUTPUT
        "${LOCKSTEP_PACKAGED_MANIFEST}"
        "${LOCKSTEP_PACKAGED_DEBUG_SERVICE}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${LOCKSTEP_PACKAGED_RESOURCES_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "${LOCKSTEP_PACKAGED_RESOURCES_DIR}"
    COMMAND $<TARGET_FILE:lockstep_resource_manifest_tool>
        --manifest "${LOCKSTEP_PACKAGED_MANIFEST}"
        --debug-service $<TARGET_FILE:lockstep_debug_service>
        --version "${PROJECT_VERSION}"
    DEPENDS
        lockstep_resource_manifest_tool
        lockstep_debug_service
        ${LOCKSTEP_RESOURCE_SOURCE_FILES}
    COMMENT "Generate packaged resources with the built debug service"
    VERBATIM
)

add_custom_target(lockstep_package_resources ALL
    DEPENDS
        "${LOCKSTEP_PACKAGED_MANIFEST}"
        "${LOCKSTEP_PACKAGED_DEBUG_SERVICE}"
)

add_dependencies(lockstep_host lockstep_package_resources)

target_link_libraries(lockstep_host
    PRIVATE
        lockstep_error_handling
        lockstep_protocol_analyzer
        lockstep_reporting
        lockstep_resources
        lockstep_target_control
        lockstep_ui_common
        lockstep_waveform_viewer
        lockstep_workflow
        lockstep_workspace
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::SerialPort
        Qt${QT_VERSION_MAJOR}::Widgets
)

target_compile_features(lockstep_host PRIVATE cxx_std_17)
target_compile_features(lockstep_debug_service_smoke PRIVATE cxx_std_17)
target_compile_features(lockstep_debug_service PRIVATE cxx_std_17)
target_compile_definitions(lockstep_host PRIVATE LOCKSTEP_APP_VERSION="${PROJECT_VERSION}")
target_compile_definitions(lockstep_debug_service PRIVATE LOCKSTEP_APP_VERSION="${PROJECT_VERSION}")

set_target_properties(lockstep_host
    PROPERTIES
        WIN32_EXECUTABLE TRUE
)

set_target_properties(lockstep_debug_service
    PROPERTIES
        WIN32_EXECUTABLE TRUE
)

target_link_libraries(lockstep_debug_service_smoke
    PRIVATE
        lockstep_resources
        lockstep_target_control
        Qt${QT_VERSION_MAJOR}::Core
)

target_link_libraries(lockstep_debug_service
    PRIVATE
        ${LOCKSTEP_HIDAPI_TARGET}
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
)

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
            TARGET lockstep_debug_service
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LOCKSTEP_HIDAPI_RUNTIME_DLL}"
                $<TARGET_FILE_DIR:lockstep_debug_service>
            COMMENT "Copy hidapi runtime next to debug service"
        )
    endif()
endif()

if(MSVC)
    target_compile_options(lockstep_host PRIVATE /W4 /permissive-)
    target_compile_options(lockstep_debug_service_smoke PRIVATE /W4 /permissive-)
    target_compile_options(lockstep_debug_service PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_host PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(lockstep_debug_service_smoke PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(lockstep_debug_service PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_target(lockstep_copy_runtime_resources
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:lockstep_host>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${LOCKSTEP_PACKAGED_RESOURCES_DIR}"
        $<TARGET_FILE_DIR:lockstep_host>/resources
    COMMENT "Refresh lockstep runtime resources next to executables"
)

add_custom_command(
    TARGET lockstep_host
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:lockstep_host>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${LOCKSTEP_PACKAGED_RESOURCES_DIR}"
        $<TARGET_FILE_DIR:lockstep_host>/resources
    COMMENT "Copy lockstep resources next to executable"
)

add_custom_command(
    TARGET lockstep_debug_service_smoke
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:lockstep_debug_service_smoke>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${LOCKSTEP_PACKAGED_RESOURCES_DIR}"
        $<TARGET_FILE_DIR:lockstep_debug_service_smoke>/resources
    COMMENT "Copy lockstep resources next to debug service smoke executable"
)
