qt_add_executable(lockstep_ui_preview
    ui_preview_main.cpp
)

target_link_libraries(lockstep_ui_preview
    PRIVATE
        lockstep_ui_common
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
