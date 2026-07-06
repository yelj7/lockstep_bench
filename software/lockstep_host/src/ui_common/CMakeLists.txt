qt_add_library(lockstep_ui_common STATIC
    asset_registry.cpp
    asset_registry.h
    global_status.cpp
    global_status.h
    main_window_shell.cpp
    main_window_shell.h
    splash_widget.cpp
    splash_widget.h
    top_status_bar.cpp
    top_status_bar.h
    ui_contract.cpp
    ui_contract.h
    ui_theme.cpp
    ui_theme.h
    ui_types.cpp
    ui_types.h
)

target_link_libraries(lockstep_ui_common
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
)

target_include_directories(lockstep_ui_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(lockstep_ui_common PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(lockstep_ui_common PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_ui_common PRIVATE -Wall -Wextra -Wpedantic)
endif()
