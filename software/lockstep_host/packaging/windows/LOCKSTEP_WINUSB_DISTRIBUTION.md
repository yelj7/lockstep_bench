<!--
/**********************************************************
* 文件名: LOCKSTEP_WINUSB_DISTRIBUTION.md
* 日期: 2026-07-20
* 版本: 1.1
* 更新记录: 增加序列号门禁、默认功能验收、便携 OpenOCD 和平台策略边界。
* 描述: 说明一次 UAC、日常免 Zadig、验收、恢复和正式签名边界。
**********************************************************/
-->

# Lockstep Windows WinUSB 部署包

产品和工具链只使用开源 libusb、libwdi/Zadig 与 OpenOCD，不包含 D3XX、Adept 或 Vivado 运行依赖。Windows 内核侧使用系统自带 WinUSB。

## 一次性部署

连接 FT601 与 Digilent HS2，然后运行联合入口。普通终端只会请求一次 UAC：

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\install_lockstep_all_winusb.ps1 `
  -Ft601Serial 000000000001 -Hs2Serial 210308AAFDCA
```

脚本自动请求 UAC、检测唯一在线实例、备份当前 OEM 驱动，再自动控制 Zadig。只有以下合同同时匹配才会点击安装：

| 目标 | VID:PID | 接口 | Zadig 名称 |
|---|---|---|---|
| FT601 | `0403:601F` | `MI_00` | `FTDI SuperSpeed-FIFO Bridge (Interface 0)` |
| HS2 | `0403:6014` | 无 MI | `Digilent USB Device` 或 `USB Serial Converter` |

可先增加 `-DryRun`。该模式只用普通权限检查设备、序列号、当前驱动、Zadig 签名和变更计划，不请求 UAC、不启动 Zadig，也不安装。FT601 与 HS2 都必须显式输入机身序列号，脚本拒绝只凭通用 VID/PID 选择设备。安装前必须成功导出当前 OEM 驱动；安装完成后还会按同一序列号重新枚举并确认 `Service=WinUSB`。安装后拔插、重启或更换 USB 口不需要再次运行 Zadig。

## 验收与烧写

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\verify_lockstep_winusb.ps1 `
  -Target FT601 -ExpectedSerial 000000000001 `
  -ProductExe .\lockstep_ui_preview.exe

powershell.exe -ExecutionPolicy Bypass -File .\verify_lockstep_winusb.ps1 `
  -Target HS2 -ExpectedSerial 210308AAFDCA `
  -OpenOcdExe .\openocd\bin\openocd.exe `
  -OpenOcdScripts .\openocd\share\openocd\scripts

powershell.exe -ExecutionPolicy Bypass -File .\program_zcu102_openocd.ps1 `
  -Bitstream .\noelvmp_zcu102_1024_debug.bit -ExpectedSha256 <sha256> `
  -Hs2Serial 210308AAFDCA
```

验证脚本默认执行功能验收：FT601 必须运行产品 `--usb-status`，HS2 必须扫描出 ZynqMP DAP 与 PS TAP。只有诊断场景显式增加 `-DriverOnly` 才会跳过功能检查。烧写脚本拒绝非 `.bit` 文件、缺少哈希或哈希不匹配，并在下载前执行完整 HS2 扫描。OpenOCD 配置固定为 ZynqMP JTAG chain，不扫描或修改 CPU 外接 CMSIS-DAP。

## 恢复

每次真实安装前，当前 OEM INF 及其文件会导出到 `driver-backup/<target>/<timestamp>/`，成功 JSON 同时记录 `backup_inf`、`installed_inf` 和 `previous_service`。联合安装第二步失败时返回 `status=partial_failure` 和已完成目标的 `recovery` 参数，不会报告整体成功。需要恢复时，以管理员身份执行：

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\restore_lockstep_usb_driver.ps1 `
  -Target HS2 -ExpectedSerial 210308AAFDCA `
  -BackupInf <backup_inf> -InstalledInf <installed_inf> `
  -ExpectedService <previous_service>
```

恢复脚本会先确认同一序列号仍在线且正由记录的 WinUSB INF 驱动，然后用 `pnputil /delete-driver ... /uninstall /force` 删除该 INF、安装备份，并等待原服务恢复；任一后验不成立都会失败，不会把“已添加旧 INF”当作恢复完成。

## 跨机器边界

本包面向普通 Windows 10/11 x64 研发与实验室主机，可自动完成一次性 libwdi 安装，但仍会出现一次 UAC。Zadig 会在目标电脑上生成设备专用 INF/CAT 并建立信任，不应直接复制某台电脑 Driver Store 中的 `oem*.inf`。启用严格 WDAC、HVCI、禁止设备安装策略或企业驱动白名单的主机可能拒绝该流程，需由管理员批准或改用组织签名包。

面向客户的集中静默部署仍需组织签名的固定 INF/CAT 或 Microsoft attestation/WHQL。缺少发布证书时必须标记为受控部署，不得宣称零交互企业安装。

## 开源组件

本 ZIP 是驱动与 JTAG 补充包，不包含完整 `lockstep_ui_preview.exe` 产品。产品应作为独立、完整且带自身哈希的交付物安装。包内固定包含 libusb 运行时、OpenOCD 0.12.0、其直接依赖 DLL、完整 OpenOCD scripts、许可证，以及 OpenOCD、libftdi、libusb、hidapi、libjaylink、capstone 和 libwdi 的对应完整源码归档。全部文件纳入 `SHA256SUMS`，新机无需预装 MSYS2、Vivado 或 Adept。

- libwdi/Zadig: https://github.com/pbatard/libwdi
- libusb: https://libusb.info/
- OpenOCD: https://openocd.org/
