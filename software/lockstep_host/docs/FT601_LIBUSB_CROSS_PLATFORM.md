<!--
/**********************************************************
* 文件名: FT601_LIBUSB_CROSS_PLATFORM.md
* 日期: 2026-07-19
* 版本: 1.0
* 更新记录: 固定 Windows、Linux 和银河麒麟统一 FT601 libusb 合同。
* 描述: 说明跨平台设备发现、权限部署、诊断和验收要求。
**********************************************************/
-->

# FT601 libusb 跨平台合同

三个目标平台共用同一 C++ `LibusbRuntime`、FT601 VID/PID、端点合同和采集协议 v2，不允许增加平台专用采集 SDK。

| 平台 | libusb 后端 | 首次部署 |
|---|---|---|
| Windows 10/11 | libusbK | 安装经目标环境验证的签名驱动包 |
| Linux | usbfs | 安装 `99-lockstep-ft601.rules` 并重新登录或重插设备 |
| 银河麒麟 V10 SP1 | usbfs | DEB 自动安装 udev rule 和 libusb 运行依赖 |

普通 Linux 可以使用系统 Qt/libusb 构建后执行 `cmake --install <build-dir>`，安装树位于 `/opt/lockstep-host`，启动入口为 `/usr/bin/lockstep-host`。银河麒麟离线包由构建脚本显式启用 `LOCKSTEP_KYLIN_PACKAGE=ON`，其 Qt 5.15.2 和 amd64 限制不会施加到普通 Linux 构建。

程序必须读取 active configuration descriptor，选择同时包含 bulk OUT `0x02` 和 bulk IN `0x82` 的接口。当前 FT601 descriptor 预期 interface 1，接口号不得硬编码。

诊断顺序：

```text
--usb-status -> --capture-status -> --capture-smoke -> 正式采集
```

`--usb-status` 必须输出 VID/PID、bus/address、序列号、USB 速度、接口号和端点。USB 状态未通过时不得执行 CONFIG、ARM 或 STOP。

Linux/麒麟运行时不得使用 root。若普通用户无法打开设备，应检查 udev rule、当前桌面会话的 `uaccess`、`plugdev` 组和是否存在占用接口的内核或用户态进程。

STOP 和 watchdog 恢复只允许取消传输、释放接口、关闭句柄、按序列号或端口路径重新打开。正常恢复不得 reset/cycle USB port，避免停止 FPGA 侧 FT601 时钟。

每个平台至少验证：设备热插拔、重启后重新枚举、三轮连续 4096 x 1024-bit 采集、CRC/序号/capture ID、显式 STOP 后 CONFIGURED、重新 ARM、watchdog 后恢复以及 JSON/HTML 证据一致性。
