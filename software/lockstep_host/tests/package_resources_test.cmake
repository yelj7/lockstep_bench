# /**********************************************************
# * 文件名: package_resources_test.cmake
# * 日期: 2026-07-14
# * 版本: v2.0
# * 更新记录: 改为验证单一可执行文件架构下的可复制资源树
# * 描述: 验证资源包不再包含外部调试服务程序或其配置字段。
# **********************************************************/

if(NOT DEFINED SOURCE_RESOURCES OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "缺少资源复制测试参数")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(COPY "${SOURCE_RESOURCES}/" DESTINATION "${TEST_ROOT}/resources")

set(MANIFEST "${TEST_ROOT}/resources/manifest.json")
set(BOARD_PROFILE
    "${TEST_ROOT}/resources/board_profiles/noelv_zcu102_cmsis_dap.json")
set(INTERFACE_CONFIG
    "${TEST_ROOT}/resources/debug_adapters/self_debug_service/interface_cmsis_dap.json")
set(TARGET_CONFIG
    "${TEST_ROOT}/resources/debug_adapters/self_debug_service/target_noelv_riscv.json")

foreach(REQUIRED_FILE
    "${MANIFEST}"
    "${BOARD_PROFILE}"
    "${INTERFACE_CONFIG}"
    "${TARGET_CONFIG}")
    if(NOT EXISTS "${REQUIRED_FILE}")
        message(FATAL_ERROR "复制后的资源树缺少文件: ${REQUIRED_FILE}")
    endif()
endforeach()

file(READ "${MANIFEST}" MANIFEST_CONTENT)
file(READ "${BOARD_PROFILE}" PROFILE_CONTENT)
foreach(FORBIDDEN_TEXT
    "debug.self_service.executable"
    "targetDebugToolPath")
    string(FIND "${MANIFEST_CONTENT}${PROFILE_CONTENT}" "${FORBIDDEN_TEXT}" FORBIDDEN_OFFSET)
    if(NOT FORBIDDEN_OFFSET EQUAL -1)
        message(FATAL_ERROR "资源配置仍引用已删除的独立调试服务: ${FORBIDDEN_TEXT}")
    endif()
endforeach()
