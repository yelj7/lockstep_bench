<!--
/**********************************************************
* 文件名: FT601_LIBUSBK_DISTRIBUTION.md
* 日期: 2026-07-19
* 版本: 1.0
* 更新记录: 新建 FT601 libusbK Windows 可分发包说明。
* 描述: 说明驱动来源、安装、验证、许可和正式签名边界。
**********************************************************/
-->

# FT601 libusbK Windows 分发包

本包不包含自研内核驱动。Windows 内核驱动为开源 libusbK 3.1.0.0，产品仅通过 `libusb-1.0.dll` 调用 libusb API，不加载或分发 D3XX。

## 安装

1. 连接 FT601，并关闭其他可能占用 FT601 的程序。
2. 以管理员 PowerShell 运行：

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\install_ft601_libusbk.ps1
```

安装脚本只接受以下目标，任一字段不匹配都会停止：

- 设备名称：`FTDI SuperSpeed-FIFO Bridge (Interface 0)`
- VID/PID：`0403:601F`
- MI：`00`
- 目标驱动：`libusbK`

脚本明确排除 Digilent JTAG `0403:6014`、CMSIS-DAP 和 CP2108。驱动安装是一次性部署动作，正常启动和 STOP 恢复不会重复安装、reset 或 cycle USB port。

## 验证

仅验证驱动绑定：

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\verify_ft601_libusbk.ps1
```

同时验证产品 libusb 接口：

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\verify_ft601_libusbk.ps1 `
  -ProductExe C:\path\to\lockstep_ui_preview.exe
```

通过条件是驱动 Provider 为 libusbK、版本为 3.1.0.0，并且产品 `--usb-status` 能发现 interface 1 和 bulk `0x02/0x82`。

## 文件说明

- `install_ft601_libusbk.ps1`：已在当前 FT601 上验证的精确绑定助手。
- `verify_ft601_libusbk.ps1`：安装后驱动和产品入口验证。
- `raw/zadig-2.9.exe`：Akeo 官方签名的 Zadig 2.9。
- `raw/zadig.ini`：启用复合设备枚举并把 libusbK 设为默认目标的已验证配置。
- `runtime/libusb-1.0.dll`：应与 `lockstep_ui_preview.exe` 放在同一目录的用户态运行库。
- `sources/libwdi-v1.5.1.zip`：Zadig/libwdi 对应源码归档。
- `references/libusbK-3.1.0.0-bin.7z`：libusbK 官方二进制参考包。
- `licenses/`：libwdi/libusbK 许可文本。
- `SHA256SUMS`：包内文件 SHA-256。

## 签名边界

Zadig 可在目标电脑上生成并安装与该 FT601 实例匹配的驱动包，适合研发、实验室和受控部署。Zadig 本身带有效的 Akeo EV 代码签名。

如果客户环境要求组织发布者、集中软件分发、Windows Hardware Dev Center attestation/WHQL 或严格 Secure Boot 策略，必须用组织证书为固定 INF/CAT 建立正式发布流水线。不得把本机 Driver Store 中的 `oem74.inf` 直接复制给其他电脑，也不得静默安装测试根证书。

源码与项目主页：

- https://github.com/pbatard/libwdi
- https://github.com/mcuee/libusbk
- https://libusb.info/
