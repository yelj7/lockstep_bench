# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-07
# * 版本: v1.2
# * 更新记录: 改为清晰领域目录组织，避免 core 过度聚合
# * 描述: 统一组织 lockstep_host 各领域模块的 CMake 子目录
# **********************************************************/

add_subdirectory(workspace)
add_subdirectory(resources)
add_subdirectory(workflow)
add_subdirectory(target_control)
add_subdirectory(fault_injection)
add_subdirectory(acquisition)
add_subdirectory(waveform_viewer)
add_subdirectory(protocol_analyzer)
add_subdirectory(reporting)
add_subdirectory(error_handling)
add_subdirectory(ui_common)
add_subdirectory(apps)
