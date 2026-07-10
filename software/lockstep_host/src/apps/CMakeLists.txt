qt_add_executable(lockstep_ui_preview
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

find_package(hidapi CONFIG REQUIRED)

add_dependencies(lockstep_ui_preview lockstep_debug_service)

target_link_libraries(lockstep_ui_preview
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

target_compile_features(lockstep_ui_preview PRIVATE cxx_std_17)
target_compile_features(lockstep_debug_service_smoke PRIVATE cxx_std_17)
target_compile_features(lockstep_debug_service PRIVATE cxx_std_17)

set_target_properties(lockstep_ui_preview
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
        hidapi::hidapi
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
    target_compile_options(lockstep_ui_preview PRIVATE /W4 /permissive-)
    target_compile_options(lockstep_debug_service_smoke PRIVATE /W4 /permissive-)
    target_compile_options(lockstep_debug_service PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_ui_preview PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(lockstep_debug_service_smoke PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(lockstep_debug_service PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_target(lockstep_copy_runtime_resources
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:lockstep_ui_preview>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:lockstep_ui_preview>/resources
    COMMENT "Refresh lockstep runtime resources next to executables"
)

add_custom_command(
    TARGET lockstep_ui_preview
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:lockstep_ui_preview>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:lockstep_ui_preview>/resources
    COMMENT "Copy lockstep resources next to executable"
)

add_custom_command(
    TARGET lockstep_debug_service_smoke
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:lockstep_debug_service_smoke>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:lockstep_debug_service_smoke>/resources
    COMMENT "Copy lockstep resources next to debug service smoke executable"
)
