# /**********************************************************
# * 文件名: restore_lockstep_usb_driver.ps1
# * 日期: 2026-07-20
# * 版本: 1.0
# * 更新记录: 新增按序列号删除 WinUSB 并恢复安装前 OEM 驱动的流程。
# * 描述: 校验恢复证据、删除本次 WinUSB INF、安装备份 INF 并后验原服务。
# **********************************************************/

param(
    [Parameter(Mandatory = $true)][ValidateSet('FT601', 'HS2')][string]$Target,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$ExpectedSerial,
    [Parameter(Mandatory = $true)][string]$BackupInf,
    [Parameter(Mandatory = $true)][ValidatePattern('^oem\d+\.inf$')][string]$InstalledInf,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$ExpectedService
)

$ErrorActionPreference = 'Stop'
$backup = (Resolve-Path -LiteralPath $BackupInf).Path
if ([IO.Path]::GetExtension($backup) -ine '.inf') { throw 'BackupInf must reference an INF file.' }

$principal = [Security.Principal.WindowsPrincipal]::new(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Driver restore must run from an elevated PowerShell session.'
}

$normalizedTarget = $Target.ToUpperInvariant()
$path = if ($normalizedTarget -eq 'FT601') {
    'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_601F&MI_00'
} else {
    'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_6014'
}
$instances = @(Get-ChildItem -LiteralPath $path | Where-Object {
    $properties = Get-ItemProperty -LiteralPath $_.PSPath
    if ($properties.Service -ne 'WinUSB') { return $false }
    $prefix = if ($normalizedTarget -eq 'FT601') {
        'USB\VID_0403&PID_601F&MI_00\'
    } else {
        'USB\VID_0403&PID_6014\'
    }
    $instanceId = $prefix + $_.PSChildName
    $enumeration = (& pnputil.exe /enum-devices /connected /instanceid $instanceId /ids 2>&1) | Out-String
    return $enumeration -match [regex]::Escape($instanceId)
})
if ($instances.Count -ne 1) { throw "Expected exactly one WinUSB-bound $normalizedTarget instance." }
$instance = $instances[0]
$actualSerial = if ($normalizedTarget -eq 'FT601') {
    $prefix = $instance.PSChildName -replace '&\d{4}$', ''
    $parents = @(Get-ChildItem -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_601F' | Where-Object {
        (Get-ItemProperty -LiteralPath $_.PSPath).ParentIdPrefix -eq $prefix
    })
    if ($parents.Count -ne 1) { throw 'Unable to resolve the FT601 composite parent serial.' }
    $parents[0].PSChildName
} else {
    $instance.PSChildName
}
if ($actualSerial -ine $ExpectedSerial) {
    throw "Connected $normalizedTarget serial mismatch: expected=$ExpectedSerial actual=$actualSerial"
}
$current = Get-ItemProperty -LiteralPath $instance.PSPath
$currentDriver = Get-ItemProperty -LiteralPath (
    'HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $current.Driver)
if ($currentDriver.InfPath -ine $InstalledInf) {
    throw "Installed WinUSB INF mismatch: expected=$InstalledInf actual=$($currentDriver.InfPath)"
}

& pnputil.exe /delete-driver $InstalledInf /uninstall /force
if ($LASTEXITCODE -ne 0) { throw "Failed to uninstall $InstalledInf." }
& pnputil.exe /add-driver $backup /install
if ($LASTEXITCODE -ne 0) { throw "Failed to restore $backup." }

$restored = $false
$deadline = (Get-Date).AddSeconds(30)
do {
    Start-Sleep -Milliseconds 500
    $updated = Get-ItemProperty -LiteralPath $instance.PSPath -ErrorAction SilentlyContinue
    if ($null -ne $updated -and $updated.Service -eq $ExpectedService) {
        $restored = $true
        break
    }
} while ((Get-Date) -lt $deadline)
if (-not $restored) { throw "$normalizedTarget did not return to service $ExpectedService." }

[ordered]@{
    schema = 'lockstep-usb-driver-restore-v1'
    success = $true
    target = $normalizedTarget
    device_instance = $actualSerial
    service = $updated.Service
    restored_inf = $backup
    removed_inf = $InstalledInf
} | ConvertTo-Json -Depth 4
