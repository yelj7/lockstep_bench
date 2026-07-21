<!--
/**********************************************************
* 文件名: README.md
* 日期: 2026-07-14
* 版本: v2.1
* 更新记录: 明确程序镜像摘要和采集编号一致性合同
* 描述: 说明数据归档与报告生成模块的职责和产物
**********************************************************/
-->

# reporting

本目录对应 M13，负责基于任务内真实证据建立统一 `ReportDocumentModel`、计算四态结论，并生成机器可读 JSON 与自包含 HTML 阅读版。

接口规则：

- 强制证据区分 `not_run`、`missing`、`passed` 和 `failed`。
- 结论优先级为 `blocked`、`fail`、`incomplete`、`pass`。
- warning 和可选记录缺失不阻断 `pass`。
- `report.json` 是权威事实源，`report.html` 只负责阅读和打印。
- 每次生成写入 `reports/versions/<report_id>/`，并原子更新根目录 latest 文件。
- 报告只记录任务内相对路径、逻辑资源标识、版本和摘要，不记录本机物理路径。
- 无可导入程序文件时，`program.sha256` 必须来自 `program_write_record.json.image_sha256`。
- `capture_id` 以 `capture_sidecar.json` 为准，并与 `protocol_events.json`、`capture_status.json` 交叉核验；不一致时报告必须为 `blocked`。

归档结构：

```text
reports/
  report.json
  report.html
  latest.json
  versions/<report_id>/
    report.json
    report.html
    manifest.json
```
