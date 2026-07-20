<!--
/**********************************************************
* 文件名: FT601_LIBUSB_WINDOWS.md
* 日期: 2026-07-17
* 版本: 1.3
* 更新记录: 将系统 WinUSB 调整为新部署默认后端，保留已有 libusbK 绑定兼容性。
* 描述: 记录 Windows 上 FT601 的 libusb/WinUSB 绑定、验证和恢复步骤。
**********************************************************/
-->

# FT601 libusb Windows 驱动绑定

`lockstep_ui_preview.exe` 仅通过 libusb 访问 `VID=0403, PID=601F` 的 FT601。程序读取 active configuration descriptor，自动发现控制端点 `0x01` 和采集端点 `0x02/0x82`；当前板卡预期控制 interface 0、采集 interface 1。打开时按顺序 claim 两个接口、发送公开的 20 字节 streaming-mode 请求，并使用 32 KiB bulk IN 缓冲区。产品不加载或分发闭源 FTDI 运行时。

Windows 首次使用前，需要以管理员身份把目标 FT601 接口绑定到系统自带 WinUSB。开发联调使用开源 Zadig/libwdi 自动生成设备专用 INF/CAT；正式部署必须使用经过目标 Windows 和 Secure Boot 环境验证的签名驱动包。已有 libusbK 绑定仍可由 libusb 使用，但不再作为新主机默认方案。只选择 VID/PID 与目标板一致的 FT601，不要修改 CMSIS-DAP、JTAG 或其他 USB 设备的驱动。

先验证 USB descriptor、接口、端点和 streaming 初始化，不发送 FPGA 采集协议命令：

```powershell
lockstep_ui_preview.exe --usb-status
```

绑定后运行：

```powershell
lockstep_ui_preview.exe --capture-status
```

若仍提示无法 claim 接口，关闭占用 FT601 的其他程序，重新插拔设备并确认设备管理器显示 WinUSB。正常 STOP 恢复只允许 cancel/close/reopen，不得执行 USB port reset 或 cycle。

板上验收基线为 `--capture-status` 成功、长 watchdog 后恢复 CONFIGURED、`--capture-smoke --trigger-timeout-samples 1200000000` 成功，以及三轮不中途重烧的 4096 x 1024-bit 正式采集。

驱动安装属于一次性部署动作，不得在每次普通启动时静默执行。自动安装功能只有在签名驱动包完成独立验收后才能接入产品。
