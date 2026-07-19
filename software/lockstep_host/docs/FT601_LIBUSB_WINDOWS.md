<!--
/**********************************************************
* 文件名: FT601_LIBUSB_WINDOWS.md
* 日期: 2026-07-17
* 版本: 1.1
* 更新记录: 固定开源 libusbK 后端和端点驱动的接口发现合同。
* 描述: 记录 Windows 上 FT601 的 libusbK 绑定、验证和恢复步骤。
**********************************************************/
-->

# FT601 libusb Windows 驱动绑定

`lockstep_ui_preview.exe` 仅通过 libusb 访问 `VID=0403, PID=601F` 的 FT601。程序读取 active configuration descriptor，并自动选择同时包含 bulk OUT `0x02` 和 bulk IN `0x82` 的接口；当前板卡预期为 interface 1。产品不加载或分发闭源 FTDI 运行时。

Windows 首次使用前，需要以管理员身份把目标 FT601 绑定为开源 libusbK 驱动。开发联调可以使用开源 Zadig/libwdi；正式部署必须使用经过目标 Windows 和 Secure Boot 环境验证的签名驱动包。只选择 VID/PID 与目标板一致的 FT601，不要修改 CMSIS-DAP、JTAG 或其他 USB 设备的驱动。

先验证 USB descriptor、接口和端点，不向 FPGA 发送采集命令：

```powershell
lockstep_ui_preview.exe --usb-status
```

绑定后运行：

```powershell
lockstep_ui_preview.exe --capture-status
```

若仍提示无法 claim 接口，关闭占用 FT601 的其他程序，重新插拔设备并确认设备管理器显示 libusbK。正常 STOP 恢复只允许 cancel/close/reopen，不得执行 USB port reset 或 cycle。

驱动安装属于一次性部署动作，不得在每次普通启动时静默执行。自动安装功能只有在签名驱动包完成独立验收后才能接入产品。
