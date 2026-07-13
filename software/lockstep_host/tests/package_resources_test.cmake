# /**********************************************************
# * 文件名: package_resources_test.cmake
# * 日期: 2026-07-13
# * 版本: v1.0
# * 更新记录: 初版创建安装资源清单生成集成测试
# * 描述: 验证正式调试服务替换占位文件并写入正确版本、状态和摘要
# **********************************************************/

if(NOT DEFINED MANIFEST_TOOL OR NOT DEFINED SOURCE_RESOURCES OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "缺少资源清单测试参数")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(COPY "${SOURCE_RESOURCES}/" DESTINATION "${TEST_ROOT}/resources")

set(DEBUG_SERVICE "${TEST_ROOT}/lockstep_debug_service")
file(WRITE "${DEBUG_SERVICE}" "linux-debug-service-test-payload\n")
file(SHA256 "${DEBUG_SERVICE}" EXPECTED_SHA256)

execute_process(
    COMMAND "${MANIFEST_TOOL}"
        --manifest "${TEST_ROOT}/resources/manifest.json"
        --debug-service "${DEBUG_SERVICE}"
        --version "0.1.0"
    RESULT_VARIABLE TOOL_RESULT
    OUTPUT_VARIABLE TOOL_OUTPUT
    ERROR_VARIABLE TOOL_ERROR
)
if(NOT TOOL_RESULT EQUAL 0)
    message(FATAL_ERROR "资源清单工具失败: ${TOOL_OUTPUT}${TOOL_ERROR}")
endif()

set(INSTALLED_SERVICE
    "${TEST_ROOT}/resources/debug_adapters/self_debug_service/lockstep_debug_service")
if(NOT EXISTS "${INSTALLED_SERVICE}")
    message(FATAL_ERROR "调试服务未写入安装资源树")
endif()
file(SHA256 "${INSTALLED_SERVICE}" INSTALLED_SHA256)
if(NOT INSTALLED_SHA256 STREQUAL EXPECTED_SHA256)
    message(FATAL_ERROR "安装资源树中的调试服务摘要错误")
endif()

file(READ "${TEST_ROOT}/resources/manifest.json" MANIFEST_CONTENT)
foreach(EXPECTED_TEXT
    "\"version\": \"0.1.0\""
    "\"sha256\": \"${EXPECTED_SHA256}\""
    "\"status\": \"enabled\"")
    string(FIND "${MANIFEST_CONTENT}" "${EXPECTED_TEXT}" EXPECTED_OFFSET)
    if(EXPECTED_OFFSET EQUAL -1)
        message(FATAL_ERROR "资源清单缺少预期内容: ${EXPECTED_TEXT}")
    endif()
endforeach()
