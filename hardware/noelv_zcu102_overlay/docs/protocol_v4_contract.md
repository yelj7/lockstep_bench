/**********************************************************
* 文件名: protocol_v4_contract.md
* 日期: 2026-07-22
* 版本: 1.5
* 更新记录: 固化 v4 结束原因与 Host 结果映射，移除 v3 PROGRAM_DONE=0 语义。
* 描述: 规定 v4 帧版本、窗口、记录、统计、丢失和兼容语义。
**********************************************************/

# Lockstep Capture Protocol v4

## 版本边界

v4 保留公共 32-byte 帧头、CRC 和现有 frame type 数值，但所有新采集命令、响应、连续帧与事件帧的 header version 必须为 4。帧生成器接收显式 frame version，禁止根据 frame type 推断。

新 Host 保留 v2/v3 原始证据 importer。旧 Host 与 v4 bitstream 必须在 HELLO/CONFIG 阶段明确拒绝，不允许把 512-bit sample 或 16-byte record 当成旧布局继续运行。

## 统一窗口

触发 `T` 只有在已有 2047 个有效预触发 sample 后才可接受：

```text
window_start = T - 2047
window_end = T + 2048
window_end_exclusive = T + 2049
```

连续和 sparse 都只覆盖 `[window_start, window_end_exclusive)` 的 4096 个有效 sample。正常结束原因 0 为 `WINDOW_COMPLETE`，与 MI_02 串口收到的 `PROGRAM_RUN_DONE` 相互独立。PROGRAM_RUN_DONE 不作为采样 RTL 的直接输入；仅当采样尚未触发时，Host 才可据此发送内部收口命令并记录 `NO_TRIGGER_PROGRAM_DONE`。

## ARM 与 ready 门禁

`ARM_ACK` 仅表示硬件接受了本轮 capture_id、配置和 ARM 命令，不表示预触发历史已经填满。Host 收到 ARM_ACK 后必须继续查询同一 capture_id 的状态；只有 continuous 与 sparse 均已积累至少 2047 个有效预触发位置、相关 CDC 状态稳定且 `capture_ready=true` 后，才允许启动程序并接受触发。ready 超时必须失败并禁止启动程序，ready 前出现的 PC 或 mismatch 条件不得形成触发。

## 来源能力

| mask | 数值 | 含义 |
|---|---:|---|
| implemented | `0x19f` | AHB、UART、SPI、CAN、I2C、JTAG、mismatch |
| continuous | `0x101` | AHB、mismatch |
| sparse | `0x09e` | UART、SPI、CAN、I2C、JTAG |
| design gap | `0x060` | ETH、USB |

ETH、USB 不得产生 EVENT_DATA。AHB、mismatch 不得产生 sparse 事务或与 continuous 重复计数。

## CAPTURE_META

v4 payload 为 16 words：

| word | 字段 |
|---:|---|
| 0 | sample rate Hz |
| 1 | window sample count，固定 4096 |
| 2 | pretrigger count，固定 2047 |
| 3 | trigger + post count，固定 2049 |
| 4 | enabled protocol/source mask |
| 5 | input invert mask |
| 6 | physical continuous channels，固定 512 |
| 7 | continuous sample bits，固定 512 |
| 8 | window start low 32-bit index |
| 9 | trigger low 32-bit index |
| 10..11 | window origin tick，64-bit little-word order |
| 12..13 | trigger tick，64-bit little-word order |
| 14..15 | window end exclusive tick，64-bit little-word order |

## EVENT_META

v4 payload 为 16 words：

| word | 字段 |
|---:|---|
| 0 | timebase Hz |
| 1 | sparse implemented source mask，固定 `0x09e` |
| 2 | sparse enabled source mask，即配置 mask 与 `0x09e` |
| 3 | design gap mask |
| 4 | record bytes，固定 16 |
| 5 | bits 7:0 layout id=2；其余 0 |
| 6 | watchdog/config audit value |
| 7 | hard-timeout/config audit value |
| 8 | bits 17:0 window initial state；其余 0 |
| 9..10 | window origin tick |
| 11..12 | trigger tick |
| 13..14 | window end exclusive tick |
| 15 | retained window record count |

initial state 是 `window_start` 前一个有效 sample 的同步线状态。第一条位于 window_start 的 record 必须满足 `state_after XOR change_mask == initial_state`。

## EVENT_DATA

一帧一条 16-byte record，payload word count 为 4：

| word | 字段 |
|---:|---|
| 0 | 32-bit absolute sample index |
| 1 | capture-local change sequence，ARM 时从 0 开始 |
| 2 | bits 17:0 state_after；bits 19:18 必须为 0；bits 27:20 source mask；bits 31:28 必须为 0 |
| 3 | bits 17:0 change mask；bits 31:18 必须为 0 |

必须满足：

- `change_mask != 0`。
- `state_before = state_after XOR change_mask`。
- source mask 必须精确等于 change mask 涉及的协议组。
- 同一有效 sample 最多一条 record；同拍多协议变化合并。
- Host 以 modular 32-bit 差值映射到 `[origin, end_exclusive)`，并验证状态链和 sequence 连续。

18-bit state 从低到高为 UART 4 bit、SPI 线状态 4 bit、CAN 2 bit、I2C 2 bit、真实 RV-JTAG 4 bit、SPI CPOL 1 bit、SPI CPHA 1 bit。CPOL/CPHA 来自真实 SPI 控制器配置，变化归属 SPI source；解析器在每个 CS 事务开始时锁存当时 mode，允许同一窗口内切换 mode 0/1/2/3。禁止依赖单一全局 mode hint 或从数据模式猜测 CPHA。普通 JTAG 没有板级连接，不属于实现来源。source mask 使用九源 ID 的低 8 bit；ETH/USB 位必须为 0。

## EVENT_END

v4 payload 为 16 words：

| word | 字段 |
|---:|---|
| 0 | end reason |
| 1 | overflow mask |
| 2 | ARM 后观察到的变化记录总数 |
| 3 | 窗口内 retained 总数 |
| 4 | uploaded 总数 |
| 5 | hardware dropped 总数 |
| 6..14 | 九源 dropped count |
| 15 | bits 8:0 sparse enabled mask；bits 17:9 sparse implemented mask；其余 0 |

结束原因必须包含独立的 `NO_TRIGGER_PROGRAM_DONE`：它只用于 MI_02 已确认程序正常完成、但本轮尚无真实触发点的收口，sample_count 必须为 0，禁止生成 trigger 或 VCD。它不得复用 `WINDOW_COMPLETE`、`HOST_TERMINATE` 或 trigger timeout 的编码；具体数值必须在共享 v4 常量中唯一声明并由 RTL、Host 和测试共同引用。

v4 end reason 数值固定如下：

| 数值 | 符号 |
|---:|---|
| 0 | `WINDOW_COMPLETE` |
| 1 | `HOST_TERMINATE` |
| 2 | `WATCHDOG` |
| 3 | `OVERFLOW` |
| 4 | `HARD_TIMEOUT` |
| 5 | `FATAL_ERROR` |
| 6 | `TRIGGER_TIMEOUT` |
| 7 | `NO_TRIGGER_PROGRAM_DONE` |

`HOST_TERMINATE` 由 Host 根据真实 trigger 分为 terminated_before_trigger/termination_truncated；`upload_timeout` 只属于 Host 上传阶段，不产生伪造的硬件 END reason。v3 的 `PROGRAM_DONE=0` 不得在 v4 解释。

ARM 到 window_start 之前被环形缓存正常覆盖的历史记录不计 drop。必须满足 `observed >= retained == uploaded == EVENT_DATA 数`、逐源 drop 之和等于 hardware dropped，并且 `reason=OVERFLOW` 与实际 loss 双向一致。任一 CRC、frame sequence、change sequence、状态链、容量或统计错误都必须失败。

## 持久化

Host 输出逻辑 1024-channel `capture.vcd`：CH0..511 来自 continuous，低速线恢复到既有高位 raw-line 位置，SPI CPOL/CPHA 分别恢复到既有 CH550/CH551 并用于逐事务解析，其余高位由 schema 标记 unavailable/constant。sidecar 中的 CPOL/CPHA 只作审计，不得成为第二解析来源。profile 为 `trace.noelv.ahb512_sparse18_v4`。

sidecar/archive 使用 v4 schema，64-bit tick 以十进制字符串保存，并记录 `continuous_sample_width=512`、`logical_vcd_width=1024`、`reconstructed_state_width=18`、capture ID、counter generation、初始状态、raw record 和统计。协议分析只解析合并 VCD；raw sparse 只用于审计，不再作为第二事务来源。

VCD 时间戳使用统一绝对时间轴：`absolute_tick = window_origin_tick + local_sample_index`，并按同一个 `timebase_hz` 转换为声明的 VCD timescale。sidecar 同时记录 origin/start/trigger/end-exclusive tick、timebase_hz 和 vcd_timescale。禁止 VCD 从局部 0 开始后再由 analyzer 静默添加 sparse 偏移。
