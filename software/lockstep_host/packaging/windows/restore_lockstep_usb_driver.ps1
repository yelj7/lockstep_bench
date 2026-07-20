# /**********************************************************
# * 文件名: restore_lockstep_usb_driver.ps1
# * 日期: 2026-07-20
# * 版本: 1.1
# * 更新记录: 收窄为当前 HS2 遗留 WinUSB 到固定 FTDI 原厂包的一次性恢复。
# * 描述: 只恢复 210308AAFDCA，拒绝其他设备、服务、提供者或备份输入。
# **********************************************************/

param(
    [Parameter(Mandatory = $true)][string]$BackupInf,
    [Parameter(Mandatory = $true)][ValidatePattern('^oem\d+\.inf$')][string]$InstalledInf
)

$ErrorActionPreference = 'Stop'
$ExpectedSerial = '210308AAFDCA'
$ExpectedService = 'FTDIBUS'
$backup = (Resolve-Path -LiteralPath $BackupInf).Path
if ([IO.Path]::GetExtension($backup) -ine '.inf') { throw 'BackupInf must reference an INF file.' }
$backupHash = (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash.ToLowerInvariant()
if ($backupHash -ne '9c8c5f39953965e7ac205826346898f974bd0916d487528c5d7925ba122b98a2') {
    throw "Unexpected FTDI backup INF SHA256: $backupHash"
}

$principal = [Security.Principal.WindowsPrincipal]::new(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Driver restore must run from an elevated PowerShell session.'
}

$normalizedTarget = 'HS2'
$path = 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_6014'
$instances = @(Get-ChildItem -LiteralPath $path | Where-Object {
    $properties = Get-ItemProperty -LiteralPath $_.PSPath
    if ($properties.Service -ne 'WinUSB') { return $false }
    $prefix = 'USB\VID_0403&PID_6014\'
    $instanceId = $prefix + $_.PSChildName
    $enumeration = (& pnputil.exe /enum-devices /connected /instanceid $instanceId /ids 2>&1) | Out-String
    return $enumeration -match [regex]::Escape($instanceId)
})
if ($instances.Count -ne 1) { throw "Expected exactly one WinUSB-bound $normalizedTarget instance." }
$instance = $instances[0]
$actualSerial = $instance.PSChildName
if ($actualSerial -ine $ExpectedSerial) {
    throw "Connected $normalizedTarget serial mismatch: expected=$ExpectedSerial actual=$actualSerial"
}
$current = Get-ItemProperty -LiteralPath $instance.PSPath
$currentDriver = Get-ItemProperty -LiteralPath (
    'HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $current.Driver)
if ($currentDriver.InfPath -ine $InstalledInf) {
    throw "Installed WinUSB INF mismatch: expected=$InstalledInf actual=$($currentDriver.InfPath)"
}
if ($currentDriver.ProviderName -notmatch '^libwdi$') {
    throw "Refusing recovery because current HS2 provider is not the known libwdi residue: $($currentDriver.ProviderName)"
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
$restoredDriver = Get-ItemProperty -LiteralPath (
    'HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $updated.Driver)
if ($restoredDriver.ProviderName -notmatch '^FTDI$') {
    throw "HS2 service recovered but provider is not FTDI: $($restoredDriver.ProviderName)"
}

[ordered]@{
    schema = 'lockstep-usb-driver-restore-v1'
    success = $true
    target = $normalizedTarget
    device_instance = $actualSerial
    service = $updated.Service
    provider = $restoredDriver.ProviderName
    restored_inf = $backup
    removed_inf = $InstalledInf
} | ConvertTo-Json -Depth 4
