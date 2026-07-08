<!--
/**********************************************************
* 文件名: README.md
* 日期: 2026-07-08
* 版本: v1.1
* 更新记录: 固化 512-bit trace 输入、analysis 输出和 M14 诊断上报规则
* 描述: 说明自研协议分析模块边界、固化资源和诊断输出规则
**********************************************************/
-->

# protocol_analyzer

本目录对应 M12，负责读取当前任务固定 VCD/schema，按固化 `lockstep_trace` 资源解析协议字段、协议事件、事务聚合、关键行为、异常识别和波形时间定位。

边界规则：

- 标准输入为 `waveform/lockstep_trace.vcd` 和 `waveform/lockstep_trace_schema.json`。
- VCD 合同为一条 512-bit packed 信号 `lockstep_trace_sample[511:0]`。
- 标准输出为 `waveform/lockstep_trace_analysis.json`。
- 通道映射、协议解码和显示配置来自 `resources/lockstep_trace/`，不接受任务级手工导入配置。
- 协议未采集或缺字段时，不生成伪事务，必须输出明确诊断。
- 诊断问题由本模块聚合后主动上报 M14，analysis 中保留 `error_id` 引用。
- 协议解析结果可以写入报告记录，但 v1 不作为 `pass` 强制证据。
- 波形渲染属于 `waveform_viewer/`，本模块只输出可定位的语义结果。
