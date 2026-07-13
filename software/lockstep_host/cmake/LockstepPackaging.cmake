# /**********************************************************
# * 文件名: LockstepPackaging.cmake
# * 日期: 2026-07-13
# * 版本: v1.0
# * 更新记录: 初版创建银河麒麟安装与 CPack 配置
# * 描述: 定义上位机 Linux 安装树、私有运行库和 DEB 元数据
# **********************************************************/

if(NOT UNIX OR APPLE)
    return()
endif()

if(NOT QT_VERSION_MAJOR EQUAL 5 OR NOT Qt5Core_VERSION VERSION_EQUAL "5.15.2")
    message(FATAL_ERROR "银河麒麟离线包必须使用 Qt 5.15.2，当前版本为 ${Qt5Core_VERSION}")
endif()

set(LOCKSTEP_INSTALL_ROOT "opt/lockstep-host")
set(LOCKSTEP_RUNTIME_ROOT "${CMAKE_BINARY_DIR}/linux-runtime")

set_target_properties(lockstep_host lockstep_debug_service PROPERTIES
    INSTALL_RPATH "\$ORIGIN/../lib"
    BUILD_WITH_INSTALL_RPATH FALSE
)

get_target_property(QT_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
execute_process(
    COMMAND "${QT_QMAKE_EXECUTABLE}" -query QT_INSTALL_PLUGINS
    OUTPUT_VARIABLE LOCKSTEP_QT_PLUGIN_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE LOCKSTEP_QMAKE_RESULT
)
if(NOT LOCKSTEP_QMAKE_RESULT EQUAL 0 OR LOCKSTEP_QT_PLUGIN_DIR STREQUAL "")
    message(FATAL_ERROR "无法通过 qmake 确定 Qt 插件目录")
endif()

add_custom_command(
    OUTPUT "${LOCKSTEP_RUNTIME_ROOT}/runtime-collected.stamp"
    COMMAND ${CMAKE_COMMAND}
        -DMAIN_EXECUTABLE=$<TARGET_FILE:lockstep_host>
        -DDEBUG_SERVICE=$<TARGET_FILE:lockstep_debug_service>
        -DQT_PLUGIN_DIR=${LOCKSTEP_QT_PLUGIN_DIR}
        -DOUTPUT_ROOT=${LOCKSTEP_RUNTIME_ROOT}
        -P "${CMAKE_SOURCE_DIR}/cmake/CollectLinuxRuntime.cmake"
    DEPENDS lockstep_host lockstep_debug_service
    COMMENT "Collect private Linux runtime dependencies"
    VERBATIM
)
add_custom_target(lockstep_collect_linux_runtime ALL
    DEPENDS "${LOCKSTEP_RUNTIME_ROOT}/runtime-collected.stamp")

install(TARGETS lockstep_host lockstep_debug_service
    RUNTIME DESTINATION "${LOCKSTEP_INSTALL_ROOT}/bin"
)
install(DIRECTORY "${LOCKSTEP_PACKAGED_RESOURCES_DIR}/"
    DESTINATION "${LOCKSTEP_INSTALL_ROOT}/resources"
)
install(DIRECTORY "${LOCKSTEP_RUNTIME_ROOT}/lib/"
    DESTINATION "${LOCKSTEP_INSTALL_ROOT}/lib"
)
install(DIRECTORY "${LOCKSTEP_RUNTIME_ROOT}/plugins/"
    DESTINATION "${LOCKSTEP_INSTALL_ROOT}/plugins"
)
install(PROGRAMS "${CMAKE_SOURCE_DIR}/packaging/linux/lockstep-host"
    DESTINATION "usr/bin"
)
install(FILES "${CMAKE_SOURCE_DIR}/packaging/linux/lockstep-host.desktop"
    DESTINATION "usr/share/applications"
)
install(FILES "${CMAKE_SOURCE_DIR}/packaging/linux/99-lockstep-cmsis-dap.rules"
    DESTINATION "etc/udev/rules.d"
)
install(FILES
    "${CMAKE_SOURCE_DIR}/docs/银河麒麟离线安装说明.md"
    "${CMAKE_SOURCE_DIR}/docs/第三方依赖与许可证.md"
    DESTINATION "${LOCKSTEP_INSTALL_ROOT}/share/doc"
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "lockstep-host")
set(CPACK_PACKAGE_VENDOR "Lockstep Project")
set(CPACK_PACKAGE_CONTACT "Lockstep Project Maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "锁步研发测试系统上位机")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "lockstep-host_${PROJECT_VERSION}_amd64")
set(CPACK_PACKAGING_INSTALL_PREFIX "/")
set(CPACK_SET_DESTDIR ON)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libx11-6, libxcb1, libudev1, udev")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/linux/postinst"
    "${CMAKE_SOURCE_DIR}/packaging/linux/postrm"
)

include(CPack)
