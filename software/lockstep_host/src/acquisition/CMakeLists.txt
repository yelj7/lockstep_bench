# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-14
# * 版本: v2.1
# * 更新记录: 将 FT601 传输依赖迁移为 libusb
# * 描述: 构建唯一上位机产品使用的采集静态库。
# **********************************************************/

add_library(lockstep_acquisition STATIC
    sampling_capture.cpp
    sampling_capture.h
)

find_path(LOCKSTEP_LIBUSB_INCLUDE_DIR
    NAMES libusb.h
    PATH_SUFFIXES libusb-1.0
    PATHS
        "${CMAKE_PREFIX_PATH}/include"
        "D:/Program Files/msys64/ucrt64/include"
)
find_library(LOCKSTEP_LIBUSB_LIBRARY
    NAMES usb-1.0 libusb-1.0
    PATHS
        "${CMAKE_PREFIX_PATH}/lib"
        "D:/Program Files/msys64/ucrt64/lib"
)
if(NOT LOCKSTEP_LIBUSB_INCLUDE_DIR OR NOT LOCKSTEP_LIBUSB_LIBRARY)
    message(FATAL_ERROR "libusb-1.0 development files are required for FT601 capture")
endif()

target_link_libraries(lockstep_acquisition
    PUBLIC Qt${QT_VERSION_MAJOR}::Core
    PRIVATE "${LOCKSTEP_LIBUSB_LIBRARY}"
)
target_compile_features(lockstep_acquisition PUBLIC cxx_std_17)
target_include_directories(lockstep_acquisition PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(lockstep_acquisition SYSTEM PRIVATE "${LOCKSTEP_LIBUSB_INCLUDE_DIR}")

if(MSVC)
    target_compile_options(lockstep_acquisition PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_acquisition PRIVATE -Wall -Wextra -Wpedantic)
endif()
