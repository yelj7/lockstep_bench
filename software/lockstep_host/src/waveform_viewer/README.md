<!--
/**********************************************************
* 文件名: README.md
* 日期: 2026-07-08
* 版本: v1.1
* 更新记录: 固化当前任务固定 VCD/schema/analysis 自动读取规则
* 描述: 说明自研波形显示模块边界和输入输出规则
**********************************************************/
-->

# waveform_viewer

本目录对应 M11，负责自动读取当前任务固定波形产物、显示 9 组协议事务、展开字段级波形、维护统一时间轴、支持缩放平移和协议事件定位显示。

边界规则：

- 只加载当前任务 `waveform/lockstep_trace.vcd`、`lockstep_trace_schema.json`、`lockstep_trace_analysis.json`。
- 不提供用户手动导入 VCD、通道映射或协议配置的接口。
- 默认显示 AHB、UART、SPI、CAN、I2C、ETH、USB、JTAG、mismatch 九组顶层事务行。
- 展开协议组后显示字段级波形，bus 字段优先合并为同一行并按显示配置格式化。
- 若 analysis 缺失、过期或摘要不匹配，可后台请求 `protocol_analyzer/` 重新解析并刷新。
- 协议语义解析属于 `protocol_analyzer/`。
- 波形缺失或解析不可用时可记录为非阻断状态，v1 不阻断 `pass`。
