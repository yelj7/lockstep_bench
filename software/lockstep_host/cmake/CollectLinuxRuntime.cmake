# /**********************************************************
# * 文件名: CollectLinuxRuntime.cmake
# * 日期: 2026-07-14
# * 版本: v2.0
# * 更新记录: 仅收集 lockstep_ui_preview 的运行依赖
# * 描述: 收集 Qt、hidapi、C++ 运行库和必要 Qt 插件。
# **********************************************************/

foreach(REQUIRED_VALUE MAIN_EXECUTABLE QT_PLUGIN_DIR OUTPUT_ROOT)
    if(NOT DEFINED ${REQUIRED_VALUE} OR "${${REQUIRED_VALUE}}" STREQUAL "")
        message(FATAL_ERROR "缺少运行库收集参数: ${REQUIRED_VALUE}")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}/lib" "${OUTPUT_ROOT}/plugins")

set(PLUGIN_BINARIES)
foreach(PLUGIN_PATTERN
    "platforms/libqxcb.so"
    "platforminputcontexts/*.so"
    "imageformats/*.so"
    "xcbglintegrations/*.so"
    "platformthemes/*.so")
    file(GLOB MATCHED_PLUGINS "${QT_PLUGIN_DIR}/${PLUGIN_PATTERN}")
    foreach(PLUGIN_PATH IN LISTS MATCHED_PLUGINS)
        file(RELATIVE_PATH PLUGIN_RELATIVE_PATH "${QT_PLUGIN_DIR}" "${PLUGIN_PATH}")
        get_filename_component(PLUGIN_RELATIVE_DIR "${PLUGIN_RELATIVE_PATH}" DIRECTORY)
        file(INSTALL
            DESTINATION "${OUTPUT_ROOT}/plugins/${PLUGIN_RELATIVE_DIR}"
            TYPE FILE
            FILES "${PLUGIN_PATH}")
        list(APPEND PLUGIN_BINARIES "${PLUGIN_PATH}")
    endforeach()
endforeach()

if(NOT EXISTS "${OUTPUT_ROOT}/plugins/platforms/libqxcb.so")
    message(FATAL_ERROR "Qt xcb 平台插件不存在: ${QT_PLUGIN_DIR}/platforms/libqxcb.so")
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${MAIN_EXECUTABLE}"
    LIBRARIES ${PLUGIN_BINARIES}
    RESOLVED_DEPENDENCIES_VAR RESOLVED_DEPENDENCIES
    UNRESOLVED_DEPENDENCIES_VAR UNRESOLVED_DEPENDENCIES
)

if(UNRESOLVED_DEPENDENCIES)
    list(JOIN UNRESOLVED_DEPENDENCIES ", " UNRESOLVED_TEXT)
    message(FATAL_ERROR "存在无法解析的构建依赖: ${UNRESOLVED_TEXT}")
endif()

foreach(DEPENDENCY IN LISTS RESOLVED_DEPENDENCIES)
    get_filename_component(DEPENDENCY_NAME "${DEPENDENCY}" NAME)
    if(DEPENDENCY_NAME MATCHES "^libQt5.*\\.so" OR
       DEPENDENCY_NAME MATCHES "^libhidapi.*\\.so" OR
       DEPENDENCY_NAME MATCHES "^libstdc\\+\\+\\.so" OR
       DEPENDENCY_NAME MATCHES "^libgcc_s\\.so")
        file(INSTALL
            DESTINATION "${OUTPUT_ROOT}/lib"
            TYPE SHARED_LIBRARY
            FOLLOW_SYMLINK_CHAIN
            FILES "${DEPENDENCY}")
    endif()
endforeach()

file(WRITE "${OUTPUT_ROOT}/runtime-collected.stamp" "${CMAKE_VERSION}\n")
