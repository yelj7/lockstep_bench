<!--
/**********************************************************
* 文件名: FT601_LIBUSBK_DISTRIBUTION.md
* 日期: 2026-07-19
* 版本: 1.1
* 更新记录: 改为随上位机启动的序列号锁定自动绑定与自检。
* 描述: 说明驱动来源、安装、验证、许可和正式签名边界。
**********************************************************/
-->

# FT601 libusbK Windows 分发包

本包不包含自研内核驱动。Windows 内核驱动为开源 libusbK 3.1.0.0，产品仅通过 `libusb-1.0.dll` 调用 libusb API，不加载或分发 D3XX。

## 自动安装

连接 FT601 后启动 `lockstep_ui_preview.exe`。助手和 Zadig 已嵌入 EXE；产品只在需要改变驱动时提升自身，并从仅 SYSTEM/Administrators 可写的受保护目录执行绑定。包内单独的 `ensure_ft601_libusbk.ps1` 仅用于源码审计和非提权合同检查，不应作为管理员脚本直接运行。

安装脚本只接受以下目标，任一字段不匹配都会停止：

- 设备名称：`FTDI SuperSpeed-FIFO Bridge (Interface 0)`
- VID/PID：`0403:601F`
- MI：`00`
- 序列号：`000000000001`
- 目标驱动：`libusbK`

脚本没有 Digilent JTAG `0403:6014`、CMSIS-DAP 或 CP2108 的安装路径。HS2 必须保持 FTDI 原厂驱动并由 Vivado 使用。正确绑定后普通启动只执行驱动和 USB 自检，不重复安装、reset 或 cycle USB port。

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

- `ensure_ft601_libusbk.ps1`：嵌入上位机的精确绑定逻辑源码；由产品受控特权模式调用。
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
