<!--
/**********************************************************
* 文件名: FT601_LIBUSB_WINDOWS.md
* 日期: 2026-07-17
* 版本: 1.0
* 更新记录: 新增 FT601 从厂商驱动切换到 libusb 后端的 Windows 部署说明。
* 描述: 记录 WinUSB/libusbK 绑定、验证和回退步骤。
**********************************************************/
-->

# FT601 libusb Windows 驱动绑定

`lockstep_ui_preview.exe` 使用 libusb 访问 `VID=0403, PID=601F` 的 FT601，接口号为 0，bulk OUT/IN 端点分别为 `0x02` 和 `0x82`。产品不再加载或分发 FTDI D3XX 运行时。

Windows 首次使用前，需要以管理员身份通过 Zadig 将目标 FT601 设备的驱动绑定为 `WinUSB`（推荐）或 `libusbK`。只选择 VID/PID 与目标板一致的 FT601，不要修改 CMSIS-DAP、JTAG 或其他 USB 设备的驱动。

绑定后运行：

```powershell
lockstep_ui_preview.exe --capture-status
```

若仍提示无法 claim 接口，关闭占用 FT601 的其他程序，重新插拔设备并确认设备管理器显示所选 WinUSB/libusbK 驱动。需要恢复厂商工具时，在设备管理器中卸载该设备驱动并重新安装 FTDI D3XX 驱动。
