/**********************************************************
* 文件名: README.md
* 日期: 2026-07-20
* 版本: v1.1
* 更新记录: 扩展 48 事务行为矩阵、256 AHB golden 和板上边界覆盖
* 描述: NOEL-V 八协议行为 benchmark 使用说明
**********************************************************/

# NOEL-V 八协议行为 Benchmark

本目录提供一套可重复的八协议 benchmark，但严格区分板上真实证据和解析器仿真 golden：

- 板上固件从 `0x00000000` 启动，主动产生 AHB、UART、SPI、CAN、I2C 行为。
- JTAG 是外部主机驱动接口，必须由运行编排器在固件执行期间发起一次有界 IDCODE/DMI 扫描。
- 当前整机 `CFG_GRETH=0`，且没有 PL 可见 USB/ULPI 源，因此 ETH、USB 固件阶段默认禁用。
- golden VCD 为 `simulation_only`，用于验证 1024-bit 位图和 C++ 八协议解析器，不得作为上板真实性证据。
- Mismatch 五位在本 benchmark 中必须始终为 0。
- 仿真 golden 中每个协议至少产生 48 个完整事务或帧；AHB 固定产生 256 个事务。
- 板上固件执行 448 次显式 AHB 访问，并分别覆盖 8/16/32-bit 数据宽度。

## 目录

```text
firmware/noelv_eight_protocol_benchmark.c       裸机测试程序
firmware/noelv_eight_protocol_benchmark.srec    可由上位机写入 RAM 的镜像
firmware/build.ps1                              开源 GNU 工具链构建脚本
generate_golden.py                              确定性 1024-bit VCD 生成器
golden/waveform/capture.vcd                     八协议 golden 波形
golden/waveform/capture_schema.json             波形合同
golden/evidence/capture_sidecar.json             仿真证据边界
golden/evidence/protocol_analysis.json           当前 C++ 解析器 golden 输出
expected_protocols.json                         事件数量、类型和语义门禁
benchmark_manifest.json                         板上/仿真启用矩阵与运行顺序
SHA256SUMS                                      关键产物摘要
```

## 行为覆盖

| 协议 | Golden 常见行为 | 当前板上策略 |
|---|---|---|
| AHB | 256 个 READ/WRITE，覆盖 3 种宽度、STALL、BURST、ERROR response | 固件执行 448 次显式 8/16/32-bit 访问与回读校验 |
| UART | 48 个字节，覆盖 TX/RX 和 `00/FF/80/7F/55/A3` 边界 golden | 固件真实发送 48-byte 二进制矩阵及可读结果文本 |
| SPI | 48 个 transfer，轮换 mode 0/1/2/3、边界数据、TX/RX、CS frame | 固件配置四种 mode；当前探针无法反推 mode，板上只验 48 个完整 transfer 和边界 TX 数据 |
| CAN | 48 个 frame，覆盖 `0x120..0x7FF`、DLC 0..8 和错误字段 golden | 固件有界尝试 32 帧；完整帧仍要求 RX 物理回环，禁止为凑数伪造回送 |
| I2C | 48 个 transfer，含 12 次真正 repeated START read、双地址、ACK/NACK | 固件真实产生；无从设备时允许 NACK |
| ETH | 48 个 frame，覆盖 IPv4、ARP、`0x88B5` 和 RX/error | `CFG_GRETH=0`，默认禁用 |
| USB | 48 个 packet、reset、4 个 PID/endpoint | 无 ULPI 源，默认禁用 |
| JTAG | 24 个 IR scan、24 个 DR scan 及 TAP reset、状态迁移 | 由主机在运行期间产生 |

## 构建固件

本机已验证的开源工具链是 MSYS2 UCRT64 的 `riscv64-unknown-elf-*`：

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\firmware\build.ps1
```

输出 SREC 的终止记录为 `S70500000000FA`，因此上位机运行入口必须使用 `0x0`，不能再使用 `0x20124`。

ETH 和 USB 只保留显式编译开关，当前不得启用：

```text
BENCH_ENABLE_ETH=0
BENCH_ENABLE_USB=0
```

构建脚本也暴露了 `-EnableSpi/-EnableCan/-EnableI2c/-EnableEth/-EnableUsb` 参数；默认值与
`benchmark_manifest.json` 一致。当前在命令行传入 `-EnableEth 1` 或 `-EnableUsb 1`
只用于后续集成调试，不能绕过顶层 capability 和板级验收门禁。

ETH 的禁用分支已经准备 GRETH 描述符、MAC 地址和 64-byte `0x88B5` 测试帧。
USB 通过弱符号 `benchmark_usb_platform_transfer()` 预留 endpoint/payload 提交接口；接入真实
ULPI 控制器后由平台实现覆盖该 hook。只有在探针 capability 置位并完成板级连接后才能打开开关。

## 更新和验证 Golden

```powershell
python .\generate_golden.py
cmake --build D:\tmp\lockstep\cmake\lockstep_host --target lockstep_eight_protocol_benchmark_test
D:\tmp\lockstep\cmake\lockstep_host\tests\lockstep_eight_protocol_benchmark_test.exe . --update-golden
ctest --test-dir D:\tmp\lockstep\cmake\lockstep_host -R lockstep_eight_protocol_benchmark_test --output-on-failure
powershell.exe -ExecutionPolicy Bypass -File .\refresh_hashes.ps1
```

普通 CTest 会在临时目录中重新解析 VCD，并严格核对：

- 八协议事件数量完全一致。
- 每组包含规定的事件类型和关键语义。
- 有事件的组必须为 `event_detected`，reason 必须说明检测到真实协议事务。
- Mismatch 组存在但事务数为 0。
- 重放结果的 `groups` 和 `protocol_events` 与提交的 golden JSON 完全一致。

## 上板运行顺序

1. 配置采集并 ARM。
2. 将 SREC 写入 RAM，并从 `0x00000000` 释放 CPU。
3. 固件运行期间，主机执行 48 次有界 JTAG IR/DR、IDCODE 或 DMI 扫描；禁止无界持续轮询占满事件 FIFO。
4. 先记录 UART 的 PASS/PARTIAL，再以最后的 `PROGRAM_RUN_DONE` 作为程序结束标志。
5. 上传 raw、VCD 和协议分析。
6. AHB/UART/SPI/CAN/I2C/JTAG 依据真实活动分别判定；ETH/USB 必须输出 `unavailable/design_gap`。

`PARTIAL` 表示某个外设等待超时或缺少外部响应，但固件仍继续执行后续协议，不会因为单个外设永久阻塞整个 benchmark。

板测完成后必须运行统一证据门禁，不应人工只看报告结论：

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\validate_board_evidence.ps1 `
  -CaptureRoot D:\tmp\lockstep\board-validation\<run>\<round>\capture
```

该门禁同时核验程序摘要、四处 capture ID、4096 x 1024-bit 合同、事件丢失/溢出、
报告产物哈希和各协议最低行为数。CAN 在没有物理 RX 回环时必须为 `limited` 且保留至少
32 条真实线级事件；ETH/USB 必须保持 `unavailable/design_gap`。
