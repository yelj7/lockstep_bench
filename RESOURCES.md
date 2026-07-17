<!--
/**********************************************************
* 文件名: RESOURCES.md
* 日期: 2026-07-17
* 版本: v1.1
* 更新记录: 明确资料范围限于 lockstep_bench 并排除同级原型目录
* 描述: 汇总掌握 lockstep_bench 仓库设计所需的可信资料
**********************************************************/
-->

# lockstep_bench 设计学习资源

## Knowledge

- [工程源码结构规划](docs/开发文档/源码库结构规划.md)
  项目内部的模块到目录映射和任务数据结构口径。用于建立整体地图。
- [上位机源码模块边界](software/lockstep_host/src/README.md)
  当前实现中 `apps`、`workspace`、`target_control` 等目录的职责说明。用于判断代码归属。
- [M02 工作区任务管理设计说明](docs/开发文档/模块设计说明/M02_工作区任务管理模块设计说明书.md)
  任务对象、状态、接口、磁盘结构和验收规则。用于第一条纵向调用链。
- [模块开发完成说明](software/lockstep_host/docs/模块开发完成说明.md)
  已实现能力和验证结果的集中说明。用于区分规划设计与当前实现。
- [Qt 6: Signals & Slots](https://doc.qt.io/qt-6/signalsandslots.html)
  Qt 官方信号槽机制说明。用于理解 UI 如何把事件传给控制器。
- [Qt 6: Object Model](https://doc.qt.io/qt-6/object.html)
  Qt 官方对象模型说明。用于理解 `QObject`、父子所有权和元对象系统。
- [CMake Tutorial](https://cmake.org/cmake/help/latest/guide/tutorial/index.html)
  CMake 官方教程。用于理解目标、依赖、链接和测试注册。
- [C++ language reference](https://en.cppreference.com/w/cpp/language)
  C++ 语言参考。用于按需查询类、命名空间、引用、指针和 RAII，不作为顺序教材通读。

## Wisdom (Communities)

- [Qt Forum](https://forum.qt.io/)
  Qt 官方社区。用于验证平台相关行为和常见 Qt 工程问题。
- [CMake Discourse](https://discourse.cmake.org/)
  CMake 官方社区。用于构建模型、工具链和跨平台问题讨论。

## Gaps

- 当前缺少一份与最新代码同步的端到端总体架构说明，学习过程中将从真实调用链逐步补齐。
- M09/M10 采集模块尚缺少顶层模块设计说明书，需要结合源码、测试和硬件接口验证。
- `software/lockstep_host/src/apps/CMakeLists.txt` 仍从仓库外 `../../../software/error_inj` 复制脚本；该原型目录不是学习资料，此处只作为可移植性和交付边界风险。
- [仓库入口说明](README.md)
  `lockstep_bench` 的顶层用途和正式目录入口。用于确认学习边界。
