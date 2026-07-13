# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-13
# * 版本: v1.0
# * 更新记录: 初版创建安装资源清单工具构建目标
# * 描述: 构建仅在打包阶段使用的资源清单生成工具
# **********************************************************/

add_executable(lockstep_resource_manifest_tool
    resource_manifest_tool.cpp
)

target_link_libraries(lockstep_resource_manifest_tool
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
)

target_compile_features(lockstep_resource_manifest_tool PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(lockstep_resource_manifest_tool PRIVATE /W4 /permissive-)
else()
    target_compile_options(lockstep_resource_manifest_tool PRIVATE -Wall -Wextra -Wpedantic)
endif()
