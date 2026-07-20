# /**********************************************************
# * 文件名: windows_ft601_driver_bootstrap_test.ps1
# * 日期: 2026-07-20
# * 版本: 1.2
# * 更新记录: 增加隐藏 Zadig、SetupAPI 故障关闭和特权结果管道回归测试。
# * 描述: 离线验证设备范围、提权、自检、资源打包和禁止修改 HS2 的门禁。
# **********************************************************/

param([string]$ProductExe = '')

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceRoot = (Resolve-Path (Join-Path $scriptRoot '..\..')).Path
$helper = Join-Path $sourceRoot 'resources\windows\ft601_driver\ensure_ft601_libusbk.ps1'
$zadigIni = Join-Path $sourceRoot 'resources\windows\ft601_driver\zadig.ini'
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
$zadigIniText = Get-Content -Raw -Encoding UTF8 -LiteralPath $zadigIni
$uiText = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $sourceRoot 'src\apps\ui_preview_main.cpp')
$bootstrapText = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $sourceRoot 'src\apps\ft601_driver_bootstrap.cpp')
$cmakeText = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $sourceRoot 'src\apps\CMakeLists.txt')
$vivadoText = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $scriptRoot 'program_zcu102_vivado.ps1')
$vivadoTclText = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $scriptRoot 'program_zcu102_vivado.tcl')
if ($helperText -notmatch 'VID_0403&PID_601F&MI_00' -or
    $helperText -notmatch "Service -eq 'libusbK'" -or
    $helperText -match "-Verb RunAs" -or
    $helperText -notmatch "status='elevation_required'" -or
    $helperText -notmatch "status='failed'" -or
    $helperText -notmatch 'WindowStyle Hidden' -or
    $helperText -notmatch "Arguments = '--usb-status'" -or
    $helperText -notmatch '4ecaa95df3da3621486a043aef8b3050b8bafe7c901402871e816229ef82039b') {
    throw 'FT601 bootstrap implementation contract mismatch.'
}
if ($zadigIniText -notmatch 'advanced_mode=true' -or $zadigIniText -notmatch 'list_all=true' -or
    $zadigIniText -notmatch 'default_driver=2') {
    throw 'Embedded Zadig configuration does not expose FT601 MI_00 with libusbK selected.'
}
$requiredPatterns = [ordered]@{
    ui = @('ensureFt601LibusbK', 'runElevatedFt601LibusbKBootstrap')
    bootstrap = @(
        ':/ft601_driver/zadig-2.9.exe', ':/ft601_driver/zadig.ini', 'inspectNativeFt601Driver',
        'enumerationFailed', '(?s)SPDRP_HARDWAREID.*?return NativeDriverState::Error',
        'NativeDriverState::Error',
        'CreateNamedPipeW', 'PIPE_REJECT_REMOTE_CLIENTS', 'GetNamedPipeClientProcessId',
        'CancelIoEx', 'GetOverlappedResult', 'JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE',
        'AssignProcessToJobObject', 'D:P\(A;;GA;;;SY\)\(A;;GA;;;BA\)', 'LockstepFt601-',
        'parseElevatedResult', 'if \(!resultParsed\)', 'post_install_driver_check_failed',
        'QTemporaryDir', 'CreateDirectoryW', 'ConvertStringSecurityDescriptorToSecurityDescriptorW',
        'GetFinalPathNameByHandleW', 'GetDriveTypeW\(volumeRoot.c_str\(\)\) != DRIVE_FIXED',
        'QueryDosDeviceW', 'D:P\(A;OICI;FA;;;SY\)\(A;OICI;FA;;;BA\)', 'ShellExecuteExW'
    )
    cmake = @(
        'qt_add_resources\(lockstep_ui_preview ft601_driver_bootstrap_resources',
        'qt5_add_resources\(LOCKSTEP_FT601_RESOURCE_SOURCES'
    )
}
$patternSources = @{ui=$uiText; bootstrap=$bootstrapText; cmake=$cmakeText}
foreach ($sourceName in $requiredPatterns.Keys) {
    foreach ($pattern in $requiredPatterns[$sourceName]) {
        if ($patternSources[$sourceName] -notmatch $pattern) {
            throw "Product startup/resource integration is missing: source=$sourceName pattern=$pattern"
        }
    }
}
if ($bootstrapText -match 'icacls' -or $bootstrapText -match 'LOCKSTEP_SKIP_FT601_DRIVER_BOOTSTRAP') {
    throw 'Product bootstrap contains a forbidden insecure or bypass path.'
}
$nativeCheckPosition = $bootstrapText.IndexOf('inspectNativeFt601Driver(&result.message)')
$slowPathPosition = $bootstrapText.IndexOf('QTemporaryDir bootstrapDir')
if ($nativeCheckPosition -lt 0 -or $slowPathPosition -lt 0 -or $nativeCheckPosition -gt $slowPathPosition) {
    throw 'SetupAPI fast path must run before resource extraction or PowerShell startup.'
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
