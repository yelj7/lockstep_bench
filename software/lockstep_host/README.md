<!--
/**********************************************************
* 文件名: README.md
* 日期: 2026-07-06
* 版本: v1.1
* 更新记录: 补充银河麒麟离线安装包构建入口
* 描述: 说明 lockstep_host 源码、资源、脚本、测试和示例目录边界
**********************************************************/
-->

# lockstep_host

本目录是锁步研发测试系统上位机软件源码根目录，按软件设计说明书的源码库结构落地。

```text
lockstep_host/
  CMakeLists.txt
  deployment_config.json
  src/
  profiles/
  scripts/
  tests/
  examples/
  docs/
```

目录边界：

- `src/`：Qt/C++ 源码和模块库目标。
- `profiles/`：随安装包固化的板卡 profile 与调试适配配置来源。
- `scripts/`：开发期构建、运行、测试和打包脚本。
- `tests/`：自动化测试代码和夹具。
- `examples/`：离线演示和回归样例数据。
- `docs/`：上位机内部实现说明，补充项目级文档。

银河麒麟 V10 SP1 x86_64 离线套件必须在目标基线原生环境执行：

```bash
./scripts/build-kylin-offline-package.sh
```

具体依赖、安装、验收和卸载方法见 `docs/银河麒麟离线安装说明.md`。
