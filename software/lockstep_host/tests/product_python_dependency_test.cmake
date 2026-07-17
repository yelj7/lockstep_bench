# /**********************************************************
# * 文件名: product_python_dependency_test.cmake
# * 日期: 2026-07-15
# * 版本: v1.0
# * 更新记录: 初版创建产品 Python 依赖白名单门禁
# * 描述: 禁止采集、解析和报告链路携带 Python，仅允许固定错误注入脚本。
# **********************************************************/

if(NOT DEFINED PRODUCT_SOURCE_DIR OR NOT DEFINED APP_RESOURCE_DIR)
    message(FATAL_ERROR "缺少产品源码或运行资源目录")
endif()

file(GLOB_RECURSE PRODUCT_PYTHON "${PRODUCT_SOURCE_DIR}/*.py")
if(PRODUCT_PYTHON)
    message(FATAL_ERROR "产品源码目录包含 Python: ${PRODUCT_PYTHON}")
endif()

file(GLOB_RECURSE RUNTIME_PYTHON "${APP_RESOURCE_DIR}/*.py")
foreach(PYTHON_FILE IN LISTS RUNTIME_PYTHON)
    file(TO_CMAKE_PATH "${PYTHON_FILE}" NORMALIZED_PATH)
    if(NOT NORMALIZED_PATH MATCHES "/error_injection/(find_sem_addr|sem_uart_inject_ranges)\\.py$")
        message(FATAL_ERROR "产品运行资源包含非错误注入 Python: ${PYTHON_FILE}")
    endif()
endforeach()

string(ASCII 122 108 97 LEGACY_CAPTURE_ACRONYM)
if(DEFINED BUILD_DIR)
    file(GLOB_RECURSE BUILD_ENTRIES LIST_DIRECTORIES true "${BUILD_DIR}/*")
    foreach(BUILD_ENTRY IN LISTS BUILD_ENTRIES)
        string(TOLOWER "${BUILD_ENTRY}" BUILD_ENTRY_LOWER)
        string(FIND "${BUILD_ENTRY_LOWER}" "${LEGACY_CAPTURE_ACRONYM}" LEGACY_BUILD_OFFSET)
        if(NOT LEGACY_BUILD_OFFSET EQUAL -1)
            message(FATAL_ERROR "构建产物包含意义不明的历史采集缩写: ${BUILD_ENTRY}")
        endif()
    endforeach()
endif()

file(GLOB_RECURSE PRODUCT_RUNTIME_SOURCE
    "${PRODUCT_SOURCE_DIR}/*.cpp"
    "${PRODUCT_SOURCE_DIR}/*.h")
foreach(SOURCE_FILE IN LISTS PRODUCT_RUNTIME_SOURCE)
    file(READ "${SOURCE_FILE}" SOURCE_CONTENT)
    string(TOLOWER "${SOURCE_CONTENT}" SOURCE_CONTENT_LOWER)
    string(FIND "${SOURCE_CONTENT_LOWER}" "${LEGACY_CAPTURE_ACRONYM}" LEGACY_ACRONYM_OFFSET)
    if(NOT LEGACY_ACRONYM_OFFSET EQUAL -1)
        message(FATAL_ERROR "产品源码包含意义不明的历史采集缩写: ${SOURCE_FILE}")
    endif()
    foreach(FORBIDDEN_TEXT "capture_cli.py" "ft601_probe.py" "decode_sidecar.py" "bus_analyzer.py"
                           "python" "py.exe" ".py")
        string(FIND "${SOURCE_CONTENT_LOWER}" "${FORBIDDEN_TEXT}" FORBIDDEN_OFFSET)
        if(NOT FORBIDDEN_OFFSET EQUAL -1)
            message(FATAL_ERROR "产品运行源码引用 Python 工具: ${SOURCE_FILE}: ${FORBIDDEN_TEXT}")
        endif()
    endforeach()
endforeach()
