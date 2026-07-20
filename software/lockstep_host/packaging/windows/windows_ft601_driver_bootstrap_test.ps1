# /**********************************************************
# * 文件名: windows_ft601_driver_bootstrap_test.ps1
# * 日期: 2026-07-20
# * 版本: 1.0
# * 更新记录: 新增 FT601 libusbK 自动绑定与 Vivado JTAG 隔离合同测试。
# * 描述: 离线验证设备范围、提权、自检、资源打包和禁止修改 HS2 的门禁。
# **********************************************************/

param([string]$ProductExe = '')

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceRoot = (Resolve-Path (Join-Path $scriptRoot '..\..')).Path
$helper = Join-Path $sourceRoot 'resources\windows\ft601_driver\ensure_ft601_libusbk.ps1'
$contractJson = & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass `
    -File $helper -ExpectedSerial TEST-SERIAL -DescribeContract
if ($LASTEXITCODE -ne 0) { throw 'FT601 bootstrap contract description failed.' }
$contract = $contractJson | ConvertFrom-Json
if ($contract.vid -ne '0403' -or $contract.pid -ne '601F' -or $contract.mi -ne '00' -or
    $contract.service -ne 'libusbK' -or $contract.driver_version -ne '3.1.0.0' -or
    $contract.jtag_policy -ne 'factory_driver_untouched') {
    throw 'FT601/libusbK/JTAG isolation contract mismatch.'
}

$helperText = Get-Content -Raw -Encoding UTF8 -LiteralPath $helper
$uiText = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $sourceRoot 'src\apps\ui_preview_main.cpp')
$bootstrapText = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $sourceRoot 'src\apps\ft601_driver_bootstrap.cpp')
$cmakeText = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $sourceRoot 'src\apps\CMakeLists.txt')
$vivadoText = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $scriptRoot 'program_zcu102_vivado.ps1')
$vivadoTclText = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $scriptRoot 'program_zcu102_vivado.tcl')
if ($helperText -notmatch 'VID_0403&PID_601F&MI_00' -or
    $helperText -notmatch "Service -eq 'libusbK'" -or
    $helperText -match "-Verb RunAs" -or
    $helperText -notmatch "status='elevation_required'" -or
    $helperText -notmatch "Arguments = '--usb-status'" -or
    $helperText -notmatch '4ecaa95df3da3621486a043aef8b3050b8bafe7c901402871e816229ef82039b') {
    throw 'FT601 bootstrap implementation contract mismatch.'
}
if ($uiText -notmatch 'ensureFt601LibusbK' -or
    $uiText -notmatch 'runElevatedFt601LibusbKBootstrap' -or
    $bootstrapText -notmatch ':/ft601_driver/zadig-2.9.exe' -or
    $bootstrapText -notmatch 'QTemporaryDir' -or
    $bootstrapText -notmatch 'CreateDirectoryW' -or
    $bootstrapText -notmatch 'ConvertStringSecurityDescriptorToSecurityDescriptorW' -or
    $bootstrapText -notmatch 'GetFinalPathNameByHandleW' -or
    $bootstrapText -notmatch 'GetDriveTypeW\(volumeRoot.c_str\(\)\) != DRIVE_FIXED' -or
    $bootstrapText -notmatch 'QueryDosDeviceW' -or
    $bootstrapText -notmatch 'D:P\(A;OICI;FA;;;SY\)\(A;OICI;FA;;;BA\)' -or
    $bootstrapText -match 'icacls' -or
    $bootstrapText -notmatch 'ShellExecuteExW' -or
    $bootstrapText -match 'LOCKSTEP_SKIP_FT601_DRIVER_BOOTSTRAP' -or
    $cmakeText -notmatch 'qt_add_resources\(lockstep_ui_preview ft601_driver_bootstrap_resources' -or
    $cmakeText -notmatch 'qt5_add_resources\(LOCKSTEP_FT601_RESOURCE_SOURCES') {
    throw 'Product startup/resource integration is missing.'
}
if ($vivadoText -notmatch "Service -ne 'FTDIBUS'" -or $vivadoText -notmatch "ProviderName -notmatch '\^FTDI\$'" -or
    $vivadoText -notmatch 'ExpectedSha256' -or $vivadoText -notmatch 'tclargs.*\$Hs2Serial' -or
    $vivadoTclText -notmatch 'string match.*\$hs2_serial' -or
    $vivadoTclText -notmatch 'get_hw_devices -quiet -of_objects \$target') {
    throw 'Vivado programming factory-driver/hash gate mismatch.'
}
foreach ($retired in @('program_zcu102_openocd.ps1', 'install_lockstep_all_winusb.ps1',
    'install_lockstep_winusb.ps1', 'build_lockstep_winusb_bundle.ps1', 'verify_lockstep_winusb.ps1')) {
    if (Test-Path -LiteralPath (Join-Path $scriptRoot $retired)) {
        throw "Legacy HS2 driver-changing path still exists: $retired"
    }
}

if (-not [string]::IsNullOrWhiteSpace($ProductExe)) {
    $exe = (Resolve-Path -LiteralPath $ProductExe).Path
    if ((Get-Item -LiteralPath $exe).Length -lt 10000000) {
        throw 'Product executable is too small to contain the embedded FT601 driver bootstrap.'
    }
}

Write-Output 'PASS windows_ft601_driver_bootstrap_test'
