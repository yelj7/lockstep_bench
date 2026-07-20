<!--
/**********************************************************
* 文件名: LIBUSBK_DRIVER_PACKAGE.md
* 日期: 2026-07-19
* 版本: 1.1
* 更新记录: 增加已验证的受控部署 ZIP，并保留组织签名发布边界。
* 描述: 约束一次性自动部署所需的签名、匹配范围和验收证据。
**********************************************************/
-->

# Windows libusbK 驱动包门禁

产品不得嵌入或调用闭源 FTDI USB 运行时。Windows 自动部署只能在取得经过目标 Windows 10/11 与 Secure Boot 验证的 libusbK 驱动包后启用，不得以测试证书或静默加入受信任根代替正式签名。

驱动包必须包含可审计版本的 INF、CAT、libusbK 内核驱动、许可证和对应源码地址，并精确匹配 FT601 `0403:601F` 的复合接口。安装程序必须在变更前再次核对 VID/PID、设备实例和产品名称，明确排除 Digilent `0403:6014`、CMSIS-DAP 和 CP2108。

正式接入产品前必须证明：

1. 首次安装仅触发一次 UAC，签名和发布者可验证。
2. 重启、重插和更换 USB 口后保持绑定。
3. `--usb-status` 能 claim 控制 interface 0 和采集 interface 1，并报告 `0x02/0x82`。
4. 卸载只移除目标驱动包，不影响 JTAG、CMSIS-DAP 或其他 FTDI 设备。
5. STOP 和重连不执行 USB port reset/cycle。

当前受控部署包为 `software/lockstep_host/dist/lockstep-ft601-libusbk-windows-1.2.zip`，包含已实测安装助手、验证脚本、官方签名 Zadig、开源许可与源码归档。该包适合研发、实验室和用户确认 UAC 的安装流程。

组织发布者、集中静默分发、attestation/WHQL 或严格 Secure Boot 环境仍需独立完成固定 INF/CAT 的正式签名，不得把本机 Driver Store 中的 OEM INF 直接作为通用发布包。
