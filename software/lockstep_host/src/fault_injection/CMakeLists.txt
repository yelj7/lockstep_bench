# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-14
# * 版本: v1.1
# * 更新记录: 接入固化脚本编排器
# * 描述: 构建唯一上位机产品使用的错误注入模块。
# **********************************************************/

add_library(lockstep_fault_injection STATIC
    fault_injection_orchestrator.cpp
    fault_injection_orchestrator.h
)
target_link_libraries(lockstep_fault_injection PUBLIC Qt${QT_VERSION_MAJOR}::Core)
target_compile_features(lockstep_fault_injection PUBLIC cxx_std_17)
target_include_directories(lockstep_fault_injection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
