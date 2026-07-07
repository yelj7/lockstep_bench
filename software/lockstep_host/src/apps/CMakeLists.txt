qt_add_executable(lockstep_ui_preview
    ui_preview_main.cpp
    workbench_controller.cpp
    workbench_controller.h
)

target_link_libraries(lockstep_ui_preview
    PRIVATE
        lockstep_error_handling
        lockstep_reporting
        lockstep_resources
        lockstep_target_control
        lockstep_ui_common
        lockstep_workflow
        lockstep_workspace
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
)

target_compile_features(lockstep_ui_preview PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(lockstep_ui_preview PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_ui_preview PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(
    TARGET lockstep_ui_preview
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:lockstep_ui_preview>/resources
    COMMENT "Copy lockstep resources next to executable"
)
