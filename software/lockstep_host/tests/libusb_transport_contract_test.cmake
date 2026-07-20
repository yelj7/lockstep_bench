# /**********************************************************
# * 文件名: libusb_transport_contract_test.cmake
# * 日期: 2026-07-17
# * 版本: 1.0
# * 更新记录: 新增 FT601 libusb 产品依赖与旧驱动零残留合同测试。
# * 描述: 验证源码、资源、运行时动态库和产品导入表均已完成 libusb 迁移。
# **********************************************************/

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED PRODUCT_EXE)
    message(FATAL_ERROR "SOURCE_DIR and PRODUCT_EXE are required")
endif()

string(CONCAT LEGACY_DRIVER_TOKEN "D3" "XX")
string(CONCAT LEGACY_RUNTIME_TOKEN "FT" "D3" "XX")
string(CONCAT LEGACY_RUNTIME_DLL "FT" "D3" "XX.dll")
string(CONCAT LEGACY_CLI_TOKEN "--ft" "d3" "xx")

file(GLOB_RECURSE TRANSPORT_CONTRACT_FILES
    "${SOURCE_DIR}/src/*.cpp"
    "${SOURCE_DIR}/src/*.h"
    "${SOURCE_DIR}/src/CMakeLists.txt"
    "${SOURCE_DIR}/resources/*.json"
    "${SOURCE_DIR}/tests/*.cpp"
)
foreach(CONTRACT_FILE IN LISTS TRANSPORT_CONTRACT_FILES)
    file(READ "${CONTRACT_FILE}" CONTRACT_TEXT)
    if(CONTRACT_TEXT MATCHES "${LEGACY_RUNTIME_TOKEN}|${LEGACY_DRIVER_TOKEN}Runtime|${LEGACY_DRIVER_TOKEN}DeviceInfo|LOCKSTEP_${LEGACY_RUNTIME_TOKEN}|${LEGACY_CLI_TOKEN}|${LEGACY_DRIVER_TOKEN} dynamic|${LEGACY_DRIVER_TOKEN} 重连")
        message(FATAL_ERROR "Legacy FT601 transport residue found in ${CONTRACT_FILE}")
    endif()
endforeach()

if(NOT EXISTS "${PRODUCT_EXE}")
    message(FATAL_ERROR "Product executable is missing: ${PRODUCT_EXE}")
endif()
get_filename_component(PRODUCT_DIR "${PRODUCT_EXE}" DIRECTORY)
if(WIN32 AND NOT EXISTS "${PRODUCT_DIR}/libusb-1.0.dll")
    message(FATAL_ERROR "libusb-1.0.dll is missing next to the product executable")
endif()
if(WIN32 AND EXISTS "${PRODUCT_DIR}/${LEGACY_RUNTIME_DLL}")
    message(FATAL_ERROR "Legacy FT601 runtime must not be packaged with the product")
endif()

if(WIN32 AND (NOT DEFINED OBJDUMP OR NOT EXISTS "${OBJDUMP}"))
    message(FATAL_ERROR "A PE import inspector is required for the libusb product contract")
endif()
if(WIN32)
    execute_process(
        COMMAND "${OBJDUMP}" -p "${PRODUCT_EXE}"
        RESULT_VARIABLE OBJDUMP_RESULT
        OUTPUT_VARIABLE IMPORT_TABLE
        ERROR_VARIABLE OBJDUMP_ERROR
    )
    if(NOT OBJDUMP_RESULT EQUAL 0)
        message(FATAL_ERROR "Unable to inspect product imports: ${OBJDUMP_ERROR}")
    endif()
    if(NOT IMPORT_TABLE MATCHES "libusb-1\\.0\\.dll")
        message(FATAL_ERROR "Product does not import libusb-1.0.dll")
    endif()
    if(IMPORT_TABLE MATCHES "${LEGACY_RUNTIME_DLL}")
        message(FATAL_ERROR "Product still imports the legacy FT601 runtime")
    endif()
endif()
