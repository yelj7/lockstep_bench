<!--
/**********************************************************
* 文件名: FT601_LIBUSB_WINDOWS.md
* 日期: 2026-07-17
* 版本: 1.4
* 更新记录: 固定 libusbK 并说明产品启动时的自动绑定与自检。
* 描述: 记录 Windows 上 FT601 的 libusb/libusbK 自动绑定、验证和恢复步骤。
**********************************************************/
-->

# FT601 libusb Windows 驱动绑定

`lockstep_ui_preview.exe` 仅通过 libusb 访问 `VID=0403, PID=601F` 的 FT601。程序读取 active configuration descriptor，自动发现控制端点 `0x01` 和采集端点 `0x02/0x82`；当前板卡预期控制 interface 0、采集 interface 1。打开时按顺序 claim 两个接口、发送公开的 20 字节 streaming-mode 请求，并使用 32 KiB bulk IN 缓冲区。产品不加载或分发闭源 FTDI 运行时。

Windows 固定将本系统专用 FT601 的 `0403:601F/MI_00`、序列号 `000000000001` 绑定到 libusbK 3.1.0.0。安装程序或上位机首次启动会自动检查并请求一次 UAC，使用随产品分发且经过签名与 SHA256 校验的 Zadig 完成绑定；后续启动只做幂等检查和 `--usb-status`，不会重复安装。Digilent HS2 `0403:6014` 始终保留 FTDI 原厂驱动，禁止由本功能修改。

先验证 USB descriptor、接口、端点和 streaming 初始化，不发送 FPGA 采集协议命令：

```powershell
lockstep_ui_preview.exe --usb-status
```

绑定后运行：

```powershell
lockstep_ui_preview.exe --capture-status
```

若仍提示无法 claim 接口，关闭占用 FT601 的其他程序，重新启动上位机并确认启动日志显示 libusbK 3.1.0.0 与 USB 自检通过。正常 STOP 恢复只允许 cancel/close/reopen，不得执行 USB port reset 或 cycle。

板上验收基线为 `--capture-status` 成功、长 watchdog 后恢复 CONFIGURED、`--capture-smoke --trigger-timeout-samples 1200000000` 成功，以及三轮不中途重烧的 4096 x 1024-bit 正式采集。

每次启动都会检查驱动，但只有状态不符合合同时才请求管理员权限执行绑定。用户拒绝 UAC、序列号不符、组件哈希错误或产品 USB 自检失败时，上位机会停止启动并显示错误，不会降级为未验证状态。
