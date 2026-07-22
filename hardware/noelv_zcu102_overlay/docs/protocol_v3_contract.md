/**********************************************************
* 文件名: protocol_v3_contract.md
* 日期: 2026-07-19
* 版本: 1.3
* 更新记录: 明确首次 overflow 立即停止接收并排空已有事件。
* 描述: 定义协商方式、事件帧、记录格式、状态语义和结束条件。
**********************************************************/

# 采集协议 v3 合同

## 兼容策略

- 帧头保持 32 字节，CRC、sequence 和 capture ID 语义不变。
- v2 `CONFIG_CAPTURE` 的 52 字节负载保持不变，设备只输出 v2 连续窗口帧。
- v3 主机先完成原有 52 字节 `CONFIG_CAPTURE`，再发送 16 字节 `CONFIG_EVENTS (0x0006)`；只有事件配置成功才允许输出事件帧。
- 连续窗口帧保持版本 2；`CONFIG_EVENTS`、`START_EVENT_STREAM` 和事件帧使用版本 3，同一 capture 可混合两种帧头版本。
- v2 主机遇到版本 3 必须明确报“不兼容”，不得把未知事件帧当成采样数据。
- v3 主机接受版本 2 或 3；版本 2 没有事件流时报告 `unavailable/legacy_bitstream`。

## 双路径

连续窗口保持 `2047 pre + trigger + 2048 post = 4096` 个 1024-bit 连续周期样本。事件路径从同一次全局触发开始，使用同一 capture ID；事件时间戳为扩展后的 64-bit 全局 `sample_abs_index`，低 32 位与连续窗口的 `window_start/trigger_index` 直接对齐，且每次 ARM 不清零。

## 协议位图

| 位 | 协议 |
|---:|---|
| 0 | AHB |
| 1 | UART |
| 2 | SPI |
| 3 | CAN |
| 4 | I2C |
| 5 | ETH |
| 6 | USB |
| 7 | JTAG（标准或 RISC-V） |
| 8 | mismatch |

`trigger_reason_mask` 表示采集为什么开始；`event_reason_mask` 表示单条事件由哪些协议活动产生。两者不得混用。

## v3 事件配置

`CONFIG_EVENTS` 使用版本 3 帧头和 16 字节负载：

| 偏移 | 宽度 | 字段 |
|---:|---:|---|
| 0 | 4 | `event_enable_mask` |
| 4 | 4 | `event_limit`，当前固定为 0，表示事件流不受 4096 点周期窗口限制 |
| 8 | 4 | `event_watchdog_ticks`，必须非 0 |
| 12 | 4 | `event_hard_timeout_ticks`，必须不小于 watchdog |

设备回显 `implemented_source_mask`、`enabled_source_mask` 和 `design_gap_mask`。当前 ETH/USB 对应位必须在 `design_gap_mask` 中置位，且不得产生事件记录。

`event_limit=0` 时，事件流持续到 `program_done`、STOP、watchdog、hard timeout 或 overflow，且九路独立 FIFO 通过公平轮询上传；AHB/JTAG 高频活动不得耗尽 4096 点周期窗口对应的全局配额。

## 事件流释放

`START_EVENT_STREAM (0x0007)` 使用版本 3 帧头和零字节负载。主机必须在收到 ARM 的 `STATUS_RSP` 后立即发送该命令，再执行板级 run/错误注入回调。设备接受命令后只产生单周期内部 release，不发送响应帧，也不得记录 BUSY 延迟错误。

事件上传只有在 `global_trigger_seen && stream_released && event_enable_mask != 0` 时启动。事件采集本身仍从全局触发周期开始，因此 release 到达前的事件先进入 FIFO；release 后立即持续排空，避免等待 4096 点窗口上传结束造成溢出。若主机写 release 失败并发出 `STOP_CAPTURE`，STOP 必须强制释放已有事件并完成 `EVENT_END -> CAPTURE_END` 排空，使设备可恢复到 `CONFIGURED`。

## 新帧

| 类型 | 名称 | 负载 |
|---:|---|---|
| `0x8103` | `EVENT_META` | 64 字节 |
| `0x8104` | `EVENT_DATA` | N x 64 字节，N >= 1 |
| `0x8105` | `EVENT_END` | 64 字节 |

事件流内部顺序为 `EVENT_META -> EVENT_DATA* -> EVENT_END`。它与连续窗口帧共享全局 frame sequence；EVENT 与 SAMPLE 在完整帧边界采用轮询仲裁，双路持续请求时严格交替，任一路不得饿死。`EVENT_META` 和 `EVENT_DATA` 均可早于 `CAPTURE_META`。命令响应在帧请求边界优先于两条采集流。`CAPTURE_END` 必须是该 capture 的最后一帧，只在连续窗口和事件路径都完成或被停止并排空后发送。

## 事件记录

固定 64 字节，小端：

| 偏移 | 宽度 | 字段 |
|---:|---:|---|
| 0 | 8 | `timestamp_ticks` |
| 8 | 4 | `capture_id` |
| 12 | 4 | `local_sequence`，按协议独立递增 |
| 16 | 1 | `protocol_id`，取 0..8 |
| 17 | 1 | `event_type` |
| 18 | 1 | `source_kind` |
| 19 | 1 | `flags` |
| 20 | 4 | `event_reason_mask`，仅低 9 位有效且包含 `protocol_id` 对应位 |
| 24 | 4 | `payload_length`，0..32 |
| 28 | 4 | 保留，必须为 0 |
| 32 | 32 | `payload`，未使用字节必须为 0 |

`source_kind`：0=`raw_line`，1=`decoded_hint`，2=`bus_transfer`，3=`lockstep_comparator`。解析器只按实际来源解释负载，不允许将 `raw_line` 包装成已解码事务。

## 结束与丢失语义

事件结束原因：0=`program_done`，1=`host_stop`，2=`watchdog`，3=`overflow`，4=`hard_timeout`，5=`fatal_error`。首次 overflow 必须立即停止接收新事件，并在已有分源 FIFO 和跨时钟 FIFO 排空后发送 `EVENT_END reason=overflow`。`EVENT_END` 保存结束原因、overflow mask、accepted/emitted/dropped 总数、9 个逐协议 dropped 计数以及 enabled/implemented mask。逐协议 emitted 由 `EVENT_DATA` 记录计数得到，accepted 必须等于 emitted+dropped。任一 dropped 非零都必须置 overflow，并使板级验收失败。仲裁必须保证持续有请求的已启用源在有限次授权内被服务。

## 首批实现边界

- AHB：真实完成传输的压缩事件，来源为 `bus_transfer`。
- UART/SPI/I2C/CAN：真实线级边沿/边界事件，来源为 `raw_line`；离线软件从带时间戳边沿解码。
- JTAG：标准和 RISC-V TAP 均产生带来源标志的 TCK 上升沿事件。
- mismatch：比较器位图变化事件。
- ETH/USB：`unavailable/design_gap`，直到真实探针源接入并完成正向板测。
- `program_done`：当前统一顶层没有真实 MMIO/探针来源，硬件输入固定为 0；本轮只能用事件静默 watchdog、hard timeout 或主机 STOP 结束，报告不得声称 `program_done_drained`。后续必须接入真实程序结束标记并实现 DRAINING/quiet guard 后，才能启用该结论。
