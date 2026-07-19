<!--
/**********************************************************
* 文件名: README.md
* 日期: 2026-07-19
* 版本: v3.0
* 更新记录: 增加 v3 稀疏事件归档、统一时间轴和 design_gap mask 输入。
* 描述: 说明 C++ 协议分析模块边界、1024-bit 资源和诊断输出规则
**********************************************************/
-->

# protocol_analyzer

本目录对应 M12，负责读取当前任务固定 VCD/schema，按固化 `lockstep_trace` 资源解析协议字段、协议事件、事务聚合、关键行为、异常识别和波形时间定位。

边界规则：

- 标准输入为 `waveform/capture.vcd`、`waveform/capture_schema.json`、capture sidecar，
  以及 sidecar 声明的可选 `evidence/protocol_events.json`。
- VCD 合同为 `CH0..CH1023` 共 1024 路标量；解析器也接受同位序的 1024-bit packed fixture。
- 标准输出为 `evidence/protocol_analysis.json`，波形页读取同一分析模型。
- 通道映射、协议解码和显示配置来自 `resources/lockstep_trace/`，不接受任务级手工导入配置。
- 协议未采集或缺字段时，不生成伪事务，必须输出明确诊断。
- 诊断问题由本模块聚合后主动上报 M14，analysis 中保留 `error_id` 引用。
- 协议解析结果可以写入报告记录，但可选协议缺失不改变强制证据结论。
- 波形渲染属于 `waveform_viewer/`，本模块只输出可定位的语义结果。

适配说明：

- C++ decoder 参考 `D:/0_ongoing/lockstep/software/logic_analyzer` 中的 NOEL-V decoder、TAP 状态机和 `zla-decode-events-v1` 事件字段设计，按本项目 `lockstep_protocol_probe_pack` 的 1024-bit 位图重新实现。
- 不引入 Python decoder、DSView 进程或 sigrok 运行时；输入仍是本项目的 VCD、schema 和 capture sidecar。
- 事件字段只声明实际采集或可由硬件 hint 无歧义推导的内容。CAN 缺少 bitrate/帧字段、USB PHY 未连接、UART 缺少格式元数据时，输出 limited/unavailable，不生成伪造完整事务。
- 稀疏 `raw_line` 记录只输出 `raw_line_event`；只有连续边沿和必要配置足以重建事务时才提升为已解码协议事件。
