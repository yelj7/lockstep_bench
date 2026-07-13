<!--
/**********************************************************
* 文件名: README.md
* 日期: 2026-07-06
* 版本: v1.2
* 更新记录: 增加离线构建环境检查、部署和套件制作脚本
* 描述: 说明开发脚本目录边界
**********************************************************/
-->

# scripts

本目录保存开发期构建、运行、测试和打包脚本。脚本可以生成安装包资源清单和离线交付产物，但不得把用户任务数据、证据或报告固化进安装包。

- `build-kylin-offline-package.sh`：在银河麒麟 V10 SP1 x86_64 原生环境完成构建、测试、CPack、许可证收集和离线套件生成。
- `generate-runtime-dependencies.sh`：生成逐 ELF 的内置/系统依赖 JSON 清单。
- `audit-linux-runtime.sh`：检查 ELF 架构、动态库缺失、构建路径泄漏和正式资源清单。
- `check-kylin-build-env.sh`：按统一 requirements 清单只读检查平台、能力和版本，可生成环境锁文件。
- `bootstrap-kylin-build-env.sh`：仅使用指定 `file:` 本地仓库一键安装缺失环境并复检。
- `prepare-kylin-build-env-bundle.sh`：从完整 DEB 目录制作可搬运的离线环境套件。
