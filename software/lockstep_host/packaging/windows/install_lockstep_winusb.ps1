# /**********************************************************
# * 文件名: install_lockstep_winusb.ps1
# * 日期: 2026-07-20
# * 版本: 1.1
# * 更新记录: 增加设备序列号门禁，并使 DryRun 保持非提权且不启动 Zadig。
# * 描述: 精确校验设备、备份当前驱动并自动驱动 Zadig 完成 WinUSB 绑定。
# **********************************************************/

param(
    [string]$Target = 'FT601',
    [ValidatePattern('^[A-Za-z0-9._-]*$')][string]$ExpectedSerial = '',
    [switch]$DryRun,
    [switch]$DescribeTarget,
    [string]$ZadigPath = ''
)

$ErrorActionPreference = 'Stop'
$targets = @{
    FT601 = [ordered]@{
        target = 'FT601'
        vid = '0403'
        pid = '601F'
        mi = '00'
        driver = 'WinUSB'
        registry_path = 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_601F&MI_00'
        device_names = @('FTDI SuperSpeed-FIFO Bridge (Interface 0)')
    }
    HS2 = [ordered]@{
        target = 'HS2'
        vid = '0403'
        pid = '6014'
        mi = ''
        driver = 'WinUSB'
        registry_path = 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_6014'
        device_names = @('Digilent USB Device', 'USB Serial Converter')
    }
}

$normalizedTarget = $Target.ToUpperInvariant()
if (-not $targets.ContainsKey($normalizedTarget)) {
    throw "Unsupported USB target: $Target"
}
$spec = $targets[$normalizedTarget]
if ($DescribeTarget) {
    $spec | ConvertTo-Json -Depth 4
    exit 0
}
if ([string]::IsNullOrWhiteSpace($ExpectedSerial)) {
    throw "ExpectedSerial is required for $normalizedTarget; refusing to select a USB device by VID/PID alone."
}
if ($ZadigPath.Contains('"')) { throw 'ZadigPath must not contain a quote character.' }
function Test-Administrator {
    $principal = [Security.Principal.WindowsPrincipal]::new(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not $DryRun -and -not (Test-Administrator)) {
    $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Target $normalizedTarget"
    if (-not [string]::IsNullOrWhiteSpace($ExpectedSerial)) {
        $arguments += " -ExpectedSerial `"$ExpectedSerial`""
    }
    if (-not [string]::IsNullOrWhiteSpace($ZadigPath)) {
        $arguments += " -ZadigPath `"$ZadigPath`""
    }
    $elevated = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    exit $elevated.ExitCode
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($ZadigPath)) {
    $ZadigPath = Join-Path $scriptRoot 'raw\zadig-2.9.exe'
}
$resolvedZadig = (Resolve-Path -LiteralPath $ZadigPath).Path
$signature = Get-AuthenticodeSignature -LiteralPath $resolvedZadig
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Akeo Consulting') {
    throw "Zadig signature verification failed: $($signature.Status)"
}

$registryInstances = if (Test-Path -LiteralPath $spec.registry_path) {
    @(Get-ChildItem -LiteralPath $spec.registry_path)
} else {
    @()
}
$presentInstances = foreach ($instance in $registryInstances) {
    $instanceId = 'USB\VID_{0}&PID_{1}{2}\{3}' -f $spec.vid, $spec.pid,
        $(if ($spec.mi) { '&MI_' + $spec.mi } else { '' }), $instance.PSChildName
    $enumeration = (& pnputil.exe /enum-devices /connected /instanceid $instanceId /ids 2>&1) | Out-String
    if ($enumeration -match [regex]::Escape($instanceId)) { $instance }
}
if (@($presentInstances).Count -ne 1) {
    throw "Expected exactly one connected $normalizedTarget instance, found $(@($presentInstances).Count)."
}
$deviceInstance = @($presentInstances)[0]
$actualSerial = if ($normalizedTarget -eq 'FT601') {
    $interfacePrefix = $deviceInstance.PSChildName -replace '&\d{4}$', ''
    $parentPath = 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_601F'
    $parents = @(Get-ChildItem -LiteralPath $parentPath | Where-Object {
        (Get-ItemProperty -LiteralPath $_.PSPath).ParentIdPrefix -eq $interfacePrefix
    })
    if ($parents.Count -ne 1) {
        throw "Unable to resolve the FT601 composite parent serial for $($deviceInstance.PSChildName)."
    }
    $parents[0].PSChildName
} else {
    $deviceInstance.PSChildName
}
if (-not [string]::IsNullOrWhiteSpace($ExpectedSerial) -and $actualSerial -ine $ExpectedSerial) {
    throw "Connected $normalizedTarget serial mismatch: expected=$ExpectedSerial actual=$actualSerial"
}
$deviceProperties = Get-ItemProperty -LiteralPath $deviceInstance.PSPath
$driverProperties = $null
if (-not [string]::IsNullOrWhiteSpace($deviceProperties.Driver)) {
    $driverProperties = Get-ItemProperty -LiteralPath (
        'HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $deviceProperties.Driver)
}
$backupRequired = $deviceProperties.Service -ne 'WinUSB'
if ($backupRequired -and
    ($null -eq $driverProperties -or $driverProperties.InfPath -notmatch '^oem\d+\.inf$')) {
    throw "Current $normalizedTarget driver cannot be backed up; refusing to change it."
}
if ($DryRun) {
    [ordered]@{
        schema = 'lockstep-winusb-install-v1'
        success = $true
        target = $normalizedTarget
        changed = $false
        dry_run = $true
        device_instance = $actualSerial
        current_service = $deviceProperties.Service
        planned_driver = 'WinUSB'
        backup_required = $backupRequired
        backup_ready = -not $backupRequired -or $driverProperties.InfPath -match '^oem\d+\.inf$'
        zadig_signature = $signature.Status.ToString()
    } | ConvertTo-Json -Depth 4
    exit 0
}
if ($deviceProperties.Service -eq 'WinUSB') {
    [ordered]@{
        schema = 'lockstep-winusb-install-v1'
        success = $true
        target = $normalizedTarget
        changed = $false
        device_instance = $deviceInstance.PSChildName
        driver = 'WinUSB'
    } | ConvertTo-Json -Depth 4
    exit 0
}

$timestamp = Get-Date -Format 'yyyyMMddTHHmmss'
$logRoot = Join-Path $scriptRoot 'logs'
$backupRoot = Join-Path $scriptRoot "driver-backup\$($normalizedTarget.ToLowerInvariant())\$timestamp"
[void](New-Item -ItemType Directory -Force -Path $logRoot)
[void](New-Item -ItemType Directory -Force -Path $backupRoot)
$logPath = Join-Path $logRoot "winusb-$($normalizedTarget.ToLowerInvariant())-$timestamp.log"
$previousInf = $driverProperties.InfPath
$previousService = $deviceProperties.Service
$backupInf = ''

function Write-Audit([string]$message) {
    Add-Content -LiteralPath $logPath -Encoding UTF8 -Value ('{0} {1}' -f (Get-Date -Format o), $message)
}

if (-not $DryRun) {
    Write-Audit "BACKUP inf=$($driverProperties.InfPath) destination=$backupRoot"
    & pnputil.exe /export-driver $driverProperties.InfPath $backupRoot |
        Out-File -LiteralPath (Join-Path $backupRoot 'pnputil-export.log') -Encoding UTF8
    if ($LASTEXITCODE -ne 0) { throw "Driver backup failed for $($driverProperties.InfPath)." }
    $backupCandidates = @(Get-ChildItem -LiteralPath $backupRoot -Recurse -Filter '*.inf' -File)
    if ($backupCandidates.Count -lt 1) { throw 'Driver export succeeded but no backup INF was produced.' }
    $backupInf = $backupCandidates[0].FullName
}

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class LockstepZadigNative {
    [DllImport("user32.dll", SetLastError=true)]
    public static extern IntPtr GetDlgItem(IntPtr parent, int id);
    [DllImport("user32.dll", EntryPoint="SendMessageA", CharSet=CharSet.Ansi, SetLastError=true)]
    public static extern IntPtr SendMessageAnsi(IntPtr window, uint message, IntPtr wParam, StringBuilder lParam);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
}
'@

function Get-ControlText([IntPtr]$window) {
    if ($window -eq [IntPtr]::Zero) { return '' }
    $text = [Text.StringBuilder]::new(8192)
    [void][LockstepZadigNative]::SendMessageAnsi($window, 0x000D, [IntPtr]$text.Capacity, $text)
    return $text.ToString()
}

function Find-ComboItem([IntPtr]$combo, [string[]]$patterns) {
    $count = [int][LockstepZadigNative]::SendMessage($combo, 0x0146, [IntPtr]::Zero, [IntPtr]::Zero)
    for ($index = 0; $index -lt $count; $index++) {
        $length = [int][LockstepZadigNative]::SendMessage($combo, 0x0149, [IntPtr]$index, [IntPtr]::Zero)
        $item = [Text.StringBuilder]::new($length + 2)
        [void][LockstepZadigNative]::SendMessageAnsi($combo, 0x0148, [IntPtr]$index, $item)
        Write-Audit "CANDIDATE index=$index text=$($item.ToString())"
        foreach ($pattern in $patterns) {
            if ($item.ToString() -eq $pattern -or $item.ToString() -match $pattern) { return $index }
        }
    }
    return -1
}

$IDC_DEVICELIST = 1001
$IDC_VID = 1002
$IDC_PID = 1003
$IDC_INFO = 1004
$IDC_MI = 1005
$IDC_INSTALL = 1009
$IDC_TARGET = 1011
$WM_COMMAND = 0x0111
$CBN_SELCHANGE = 1
$process = Start-Process -FilePath $resolvedZadig -WorkingDirectory $scriptRoot -PassThru
try {
    [void]$process.WaitForInputIdle(15000)
    $deadline = (Get-Date).AddSeconds(30)
    do {
        $process.Refresh()
        $main = $process.MainWindowHandle
        if ($main -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    if ($main -eq [IntPtr]::Zero) { throw 'Zadig main window was not found.' }

    $deviceCombo = [LockstepZadigNative]::GetDlgItem($main, $IDC_DEVICELIST)
    $selected = Find-ComboItem $deviceCombo $spec.device_names
    if ($selected -lt 0) { throw "$normalizedTarget was not found in the Zadig device list." }
    [void][LockstepZadigNative]::SendMessage($deviceCombo, 0x014E, [IntPtr]$selected, [IntPtr]::Zero)
    [void][LockstepZadigNative]::SendMessage(
        $main, $WM_COMMAND, [IntPtr](($CBN_SELCHANGE -shl 16) -bor $IDC_DEVICELIST), $deviceCombo)
    Start-Sleep -Seconds 1

    $driverCombo = [LockstepZadigNative]::GetDlgItem($main, $IDC_TARGET)
    $winUsbIndex = Find-ComboItem $driverCombo @('^WinUSB')
    if ($winUsbIndex -ge 0) {
        [void][LockstepZadigNative]::SendMessage($driverCombo, 0x014E, [IntPtr]$winUsbIndex, [IntPtr]::Zero)
        [void][LockstepZadigNative]::SendMessage(
            $main, $WM_COMMAND, [IntPtr](($CBN_SELCHANGE -shl 16) -bor $IDC_TARGET), $driverCombo)
        Start-Sleep -Milliseconds 500
    }

    $vid = Get-ControlText ([LockstepZadigNative]::GetDlgItem($main, $IDC_VID))
    $pidText = Get-ControlText ([LockstepZadigNative]::GetDlgItem($main, $IDC_PID))
    $mi = Get-ControlText ([LockstepZadigNative]::GetDlgItem($main, $IDC_MI))
    $driver = Get-ControlText $driverCombo
    Write-Audit "VERIFY vid=$vid pid=$pidText mi=$mi driver=$driver selected=$selected"
    if ($vid -notmatch $spec.vid -or $pidText -notmatch $spec.pid -or
        ($spec.mi -and $mi -notmatch $spec.mi) -or $driver -notmatch '^WinUSB') {
        throw "Safety verification failed: VID=$vid PID=$pidText MI=$mi driver=$driver"
    }
    $install = [LockstepZadigNative]::GetDlgItem($main, $IDC_INSTALL)
    [void][LockstepZadigNative]::SendMessage($install, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    Write-Audit 'INSTALL_CLICKED'
    $deadline = (Get-Date).AddMinutes(4)
    do {
        $info = Get-ControlText ([LockstepZadigNative]::GetDlgItem($main, $IDC_INFO))
        if ($info -match 'Driver Installation: SUCCESS') {
            Write-Audit 'SUCCESS'
            $verified = $false
            $verifyDeadline = (Get-Date).AddSeconds(30)
            do {
                Start-Sleep -Milliseconds 500
                $updated = Get-ItemProperty -LiteralPath $deviceInstance.PSPath -ErrorAction SilentlyContinue
                if ($null -ne $updated -and $updated.Service -eq 'WinUSB') {
                    $verified = $true
                    break
                }
            } while ((Get-Date) -lt $verifyDeadline)
            if (-not $verified) {
                throw "Zadig reported success, but $normalizedTarget serial $actualSerial is not bound to WinUSB."
            }
            $installedDriver = Get-ItemProperty -LiteralPath (
                'HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $updated.Driver)
            [ordered]@{ schema='lockstep-winusb-install-v1'; success=$true; target=$normalizedTarget;
                changed=$true; device_instance=$actualSerial; driver='WinUSB';
                previous_service=$previousService; previous_inf=$previousInf; backup=$backupRoot;
                backup_inf=$backupInf; installed_inf=$installedDriver.InfPath; log=$logPath } | ConvertTo-Json -Depth 4
            exit 0
        }
        if ($info -match 'Driver Installation: FAILED|Driver Installation: Cancelled') {
            throw (($info -split "`r?`n") | Select-Object -Last 8 | Out-String)
        }
        Start-Sleep -Milliseconds 500
        $process.Refresh()
        if ($process.HasExited -and $process.ExitCode -ne 0) {
            throw "Zadig exited with code $($process.ExitCode)."
        }
    } while ((Get-Date) -lt $deadline)
    throw 'Timed out waiting for Zadig to install WinUSB.'
} catch {
    Write-Audit "FAILED $($_.Exception.Message)"
    throw
} finally {
    if ($null -ne $process -and -not $process.HasExited) {
        [void]$process.CloseMainWindow()
    }
}
