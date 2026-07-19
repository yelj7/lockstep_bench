# /**********************************************************
# * 文件名: verify_ft601_libusbk.ps1
# * 日期: 2026-07-19
# * 版本: 1.0
# * 更新记录: 新建 FT601 libusbK 安装后验证脚本。
# * 描述: 精确验证目标设备驱动，并可调用产品 libusb 自检入口。
# **********************************************************/

param([string]$ProductExe = '')

$ErrorActionPreference = 'Stop'
$enumPath = 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_601F&MI_00'
$instances = if (Test-Path -LiteralPath $enumPath) {
    @(Get-ChildItem -LiteralPath $enumPath)
} else {
    @()
}

if ($instances.Count -ne 1) {
    throw "Expected exactly one FT601 MI_00 driver instance, found $($instances.Count)."
}

$instance = $instances[0]
$device = Get-ItemProperty -LiteralPath $instance.PSPath
$classPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $device.Driver
$driver = Get-ItemProperty -LiteralPath $classPath
$driverOk = $driver.ProviderName -match '^libusbK$' -and $driver.DriverVersion -eq '3.1.0.0'

$result = [ordered]@{
    schema = 'lockstep-ft601-libusbk-verification-v1'
    success = $false
    device_id = 'USB\VID_0403&PID_601F&MI_00\' + $instance.PSChildName
    device_name = $driver.DriverDesc
    driver_provider = $driver.ProviderName
    driver_version = $driver.DriverVersion
    inf_name = $driver.InfPath
    product_usb_status = $null
}

if (-not $driverOk) {
    $result | ConvertTo-Json -Depth 8
    throw "FT601 is not bound to libusbK 3.1.0.0."
}

if (-not [string]::IsNullOrWhiteSpace($ProductExe)) {
    $resolvedExe = (Resolve-Path -LiteralPath $ProductExe).Path
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $resolvedExe
    $startInfo.Arguments = '--usb-status'
    $startInfo.WorkingDirectory = Split-Path -Parent $resolvedExe
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "Product --usb-status failed: exit=$($process.ExitCode) stderr=$stderr"
    }
    $usbStatus = $stdout | ConvertFrom-Json
    if (-not $usbStatus.success -or
        $usbStatus.selected_usb_device.interface_number -ne 1 -or
        $usbStatus.selected_usb_device.out_endpoint -ne '0x02' -or
        $usbStatus.selected_usb_device.in_endpoint -ne '0x82') {
        throw "Product USB contract verification failed: $stdout"
    }
    $result.product_usb_status = $usbStatus
}

$result.success = $true
$result | ConvertTo-Json -Depth 8
