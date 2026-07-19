/**********************************************************
* 文件名: source_mapping.md
* 日期: 2026-07-19
* 版本: 1.0
* 更新记录: 冻结 NOEL-V 基线与项目覆盖层的维护边界。
* 描述: 说明历史 bitstream 输入如何映射到可维护源码。
**********************************************************/

# 源码映射

## 基线

- NOEL-V/GRLIB 基线提交：`ef8d664d40bfcac1d137d671f2b66a2fbf28a90e`。
- `baseline_source_manifest.txt` 保存现有 debug bitstream 实际读取的文件集合。
- `source_lock.csv` 使用相对路径和 SHA-256 锁定每个基线文件，构建机路径不参与摘要。

## 覆盖层

| 维护目录 | 覆盖目标 |
|---|---|
| `rtl/lockstep_capture/` | `designs/noelv-xilinx-zcu102/rtl/lockstep_capture/` |
| `integration/designs/noelv-generic/rtl/core/` | 同名 NOEL-V core 集成目录 |
| `integration/designs/noelv-xilinx-zcu102/rtl/` | 同名 ZCU102 顶层 RTL 目录 |
| `build/` | 远端构建、ILA 和约束输入，不覆盖基线 |

历史 `.codex-tasks/20260717-capture-recovery-debug/hardware_source` 只作为本次基线证据，不再作为维护入口。后续构建必须从指定基线提交重新检出，核对 `source_lock.csv`，应用 overlay 后再生成工程。

## 当前差异

- canonical `D:/0_ongoing/lockstep/hardware/dslogic` 只含通用窗口 CDC 和早期 stage1f top，不含当前 NOEL-V 完整集成。
- 当前仓库因此正式维护 NOEL-V 专用 overlay；通用模块后续可在接口稳定后回合到 canonical hardware，但不能让发布构建依赖两个可变副本。
- 软件维护入口固定为本仓库 `software/lockstep_host`。
