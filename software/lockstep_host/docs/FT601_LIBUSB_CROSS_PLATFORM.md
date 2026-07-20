<!--
/**********************************************************
* 文件名: FT601_LIBUSB_CROSS_PLATFORM.md
* 日期: 2026-07-19
* 版本: 1.4
* 更新记录: Windows 固定使用 libusbK，并将 FT601 自动绑定与产品启动自检集成。
* 描述: 说明跨平台设备发现、权限部署、诊断和验收要求。
**********************************************************/
-->

# FT601 libusb 跨平台合同

三个目标平台共用同一 C++ `LibusbRuntime`、FT601 VID/PID、端点合同和双版本采集协议，不允许增加平台专用采集 SDK。连续窗口帧保持 v2，稀疏协议事件帧使用 v3；两路帧按 frame sequence、capture ID 和时间基准联合校验。

| 平台 | libusb 后端 | 首次部署 |
|---|---|---|
| Windows 10/11 | libusbK | 安装或首次启动自动绑定专用 FT601；正式发布使用签名 INF/CAT |
| Linux | usbfs | 安装 `99-lockstep-ft601.rules` 并重新登录或重插设备 |
| 银河麒麟 V10 SP1 | usbfs | DEB 自动安装 udev rule 和 libusb 运行依赖 |

普通 Linux 可以使用系统 Qt/libusb 构建后执行 `cmake --install <build-dir>`，安装树位于 `/opt/lockstep-host`，启动入口为 `/usr/bin/lockstep-host`。银河麒麟离线包由构建脚本显式启用 `LOCKSTEP_KYLIN_PACKAGE=ON`，其 Qt 5.15.2 和 amd64 限制不会施加到普通 Linux 构建。

程序必须读取 active configuration descriptor，同时发现包含 bulk OUT `0x01` 的控制接口和包含 bulk OUT `0x02`、bulk IN `0x82` 的采集接口。当前 FT601 descriptor 预期控制 interface 0、采集 interface 1，接口号不得硬编码。

打开设备时必须先 claim 控制接口，再 claim 采集接口，并向 `0x01` 发送公开 `ft60x-rs` 实现采用的 20 字节 streaming-mode 请求。请求中的数据端点固定为 `0x82/0x02`。缺少控制端点、任一接口 claim 失败、初始化传输失败或短写都必须使打开失败。参考实现：[ft60x-rs](https://github.com/apertus-open-source-cinema/ft60x-rs)。

FT601 bulk IN 统一提交 32 KiB 缓冲区。短响应允许以 timeout 加非零 transferred bytes 返回，并继续交给协议帧解码器；不得恢复为 64 字节 STATUS 读或 4 KiB 连续读，否则多命令响应会破坏帧边界。

诊断顺序：

```text
--usb-status -> --capture-status -> --capture-smoke -> 正式采集
```

`--usb-status` 必须输出 VID/PID、bus/address、序列号、USB 速度、接口号和端点，并在报告成功前完成 streaming 初始化。它不发送 FPGA 采集协议的 CONFIG、ARM 或 STOP。

Windows 受控部署只绑定专用 FT601 `0403:601F/MI_00`、序列号 `000000000001` 到 libusbK 3.1.0.0。上位机每次启动先执行幂等检查：已正确绑定时直接运行 `--usb-status`，只有首次安装或驱动漂移时才自动请求 UAC 并调用随包 Zadig；日常拔插无需操作。Digilent HS2 `0403:6014` 必须保持 FTDI 原厂驱动，PL 配置只使用 Vivado Hardware Manager。

Linux/麒麟运行时不得使用 root。若普通用户无法打开设备，应检查 udev rule、当前桌面会话的 `uaccess`、`plugdev` 组和是否存在占用接口的内核或用户态进程。

STOP 和 watchdog 恢复只允许取消传输、释放接口、关闭句柄、按序列号或端口路径重新打开。正常恢复不得 reset/cycle USB port，避免停止 FPGA 侧 FT601 时钟。

正式自动化可向 `--live-capture` 传入 `--run-request <json>`。产品必须在 ARM ACK 后才执行该 CMSIS-DAP run 请求，再进入 FT601 collect，避免使用固定 sleep 猜测 ARM 时刻。

离线重放非默认协议子集时，必须把采集配置中的掩码原样传给 `--event-enable-mask <mask>`；省略时按默认 `0x19f` 严格校验 `EVENT_META`，不得从事件内容猜测请求掩码。

每个平台至少验证：设备热插拔、重启后重新枚举、三轮连续 4096 x 1024-bit 采集、CRC/序号/capture ID、显式 STOP 后 CONFIGURED、重新 ARM、watchdog 后恢复以及 JSON/HTML 证据一致性。
