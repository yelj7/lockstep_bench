/**********************************************************
* 文件名: REMOTE_BITSTREAM_BUILD_POLICY.md
* 日期: 2026-07-17
* 版本: 1.4
* 更新记录: 将唯一构建目标调整为 78 服务器的 ylj 账户并更新认证变量。
* 描述: 规定硬件实现和比特流只能在指定服务器目录执行。
**********************************************************/

# 比特流远端构建约束

本项目的 Vivado 综合、实现、调试探针插入、比特流生成和 `.ltx` 生成只能在以下环境执行：

- 服务器：`192.168.31.78`
- 用户：`ylj`
- SSH 主机指纹：`ssh-ed25519 255 SHA256:flfX1cyK4sUKevO70V6HjdbP8ZUH3BRNSphiap8EE24`
- 根目录：`/home/ylj/`
- 工程目录：`/home/ylj/NOVELV_SAMPLE/<source_digest>/`
- Vivado：`/tools/Xilinx/Vivado/2022.2/bin/vivado`
- 许可证：`/tools/Vivado_license_2037.lic`

禁止在开发机本地运行 `synth_design`、`opt_design`、`place_design`、`route_design` 或 `write_bitstream`。本地只允许修改源码、运行软件测试、运行 RTL 仿真、准备 source manifest、上传源码以及下载和校验远端产物。

远端每个源码摘要使用独立目录，`debug` 与 `release` 产物分开。远端构建必须先核对 `simulation_gate.json` 中的源码摘要；摘要不一致时拒绝综合。认证密码仅从本地环境变量 `DC_REMOTE_PASSWORD_78` 读取，禁止写入源码、日志、任务证据或命令脚本。

已有阶段产物不要求在服务器重复生成。允许上传综合 DCP、IP DCP、约束、源清单和仿真门禁摘要，并从下一阶段继续，但必须同时满足：Vivado 主次版本一致、器件型号一致、source digest 一致、DCP 可正常打开且无黑盒。续跑记录必须写明输入 DCP 的 SHA-256、上一阶段状态和实际起始命令。任何一项不一致时，远端必须从综合重新开始。

构建完成后，下载比特流、`.ltx`、timing、utilization、DRC、clock interaction、source manifest、构建日志和 SHA-256 清单到 `artifacts/remote_builds/<source_digest>/`。临时上传包、解压目录和本地构建目录统一放在 `D:\tmp\lockstep`，不得写入源码库。只有下载后的 SHA-256 与远端清单一致，产物才可用于上板。
