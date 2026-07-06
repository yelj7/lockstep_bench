# /**********************************************************
# * 文件名: CMakeLists.txt
# * 日期: 2026-07-06
# * 版本: v1.1
# * 更新记录: 引入 src 统一构建入口
# * 描述: 定义 lockstep_host 上位机软件工程和顶层构建配置
# **********************************************************/

cmake_minimum_required(VERSION 3.16)

project(lockstep_host
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)

add_subdirectory(src)
