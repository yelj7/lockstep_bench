# /**********************************************************
# * 文件名: ensure_ft601_libusbk.ps1
# * 日期: 2026-07-20
# * 版本: 1.2
# * 更新记录: 增加 libusbK Zadig 隐藏执行、非提权判定和结构化错误返回。
# * 描述: 仅处理专用 FT601 MI_00，自动提权、备份、绑定 libusbK 并运行产品自检。
# **********************************************************/

param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$ExpectedSerial,
    [string]$ZadigPath = '',
    [string]$ProductExe = '',
    [switch]$DryRun,
    [switch]$DescribeContract,
    [switch]$AlreadyElevated,
    [string]$ResultPath = ''
)

$ErrorActionPreference = 'Stop'
$enumPath = 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_601F&MI_00'
$parentPath = 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_601F'

if ($DescribeContract) {
    [ordered]@{
        schema = 'lockstep-ft601-bootstrap-contract-v1'
        vid = '0403'
        pid = '601F'
        mi = '00'
        service = 'libusbK'
        provider = 'libusbK'
        driver_version = '3.1.0.0'
        jtag_vid_pid = '0403:6014'
        jtag_policy = 'factory_driver_untouched'
    } | ConvertTo-Json -Depth 4
    exit 0
}

function Test-Administrator {
    $principal = [Security.Principal.WindowsPrincipal]::new(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-Ft601State {
    $present = @()
    if (Test-Path -LiteralPath $enumPath) {
        $present = @(Get-ChildItem -LiteralPath $enumPath | Where-Object {
            $instanceId = 'USB\VID_0403&PID_601F&MI_00\' + $_.PSChildName
            $enumeration = (& pnputil.exe /enum-devices /connected /instanceid $instanceId /ids 2>&1) | Out-String
            $enumeration -match [regex]::Escape($instanceId)
        })
    }
    if ($present.Count -eq 0) { return $null }
    if ($present.Count -ne 1) { throw "Expected exactly one connected FT601 MI_00 instance, found $($present.Count)." }
    $instance = $present[0]
    $interfacePrefix = $instance.PSChildName -replace '&\d{4}$', ''
    $parents = @(Get-ChildItem -LiteralPath $parentPath | Where-Object {
        (Get-ItemProperty -LiteralPath $_.PSPath).ParentIdPrefix -eq $interfacePrefix
    })
    if ($parents.Count -ne 1) { throw 'Unable to resolve the FT601 composite parent serial.' }
    $serial = $parents[0].PSChildName
    if ($serial -ine $ExpectedSerial) {
        throw "Connected FT601 serial mismatch: expected=$ExpectedSerial actual=$serial"
    }
    $device = Get-ItemProperty -LiteralPath $instance.PSPath
    $driver = if ($device.Driver) {
        Get-ItemProperty -LiteralPath ('HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $device.Driver)
    } else { $null }
    [pscustomobject]@{ instance=$instance; serial=$serial; device=$device; driver=$driver }
}

function Invoke-ProductCheck([string]$exePath) {
    if ([string]::IsNullOrWhiteSpace($exePath)) { return $null }
    $resolvedExe = (Resolve-Path -LiteralPath $exePath).Path
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $resolvedExe
    $info.Arguments = '--usb-status'
    $info.WorkingDirectory = Split-Path -Parent $resolvedExe
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        [void]$process.Start()
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) { $process.Kill(); throw 'Product USB self-check timed out.' }
        [Threading.Tasks.Task]::WaitAll(@($stdoutTask, $stderrTask))
        if ($process.ExitCode -ne 0) { throw "Product USB self-check failed: $($stderrTask.Result)" }
        $status = $stdoutTask.Result | ConvertFrom-Json
        if (-not $status.success -or $status.selected_usb_device.vid -ne '0x0403' -or
            $status.selected_usb_device.pid -ne '0x601f' -or
            $status.selected_usb_device.out_endpoint -ne '0x02' -or
            $status.selected_usb_device.in_endpoint -ne '0x82') {
            throw "Product USB contract mismatch: $($stdoutTask.Result)"
        }
        return $status
    } finally { $process.Dispose() }
}

function Write-Result([object]$value, [int]$exitCode) {
    $json = $value | ConvertTo-Json -Depth 10
    if (-not [string]::IsNullOrWhiteSpace($ResultPath)) {
        $resultRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
        $resolved = [IO.Path]::GetFullPath($ResultPath)
        if (-not $resolved.StartsWith($resultRoot, [StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetFileName($resolved) -notmatch '^lockstep-ft601-result-[0-9a-f-]+\.json$') {
            throw 'ResultPath is outside the controlled TEMP result area.'
        }
        $temporary = $resolved + '.tmp'
        [IO.File]::WriteAllText($temporary, $json, [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $temporary -Destination $resolved -Force
    }
    $json
    exit $exitCode
}

trap {
    Write-Result ([ordered]@{schema='lockstep-ft601-bootstrap-v1';success=$false;
        status='failed';changed=$false;error=$_.Exception.Message}) 50
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($ZadigPath)) { $ZadigPath = Join-Path $scriptRoot 'raw\zadig-2.9.exe' }
if ($ZadigPath.Contains('"') -or $ProductExe.Contains('"')) { throw 'Executable paths must not contain quote characters.' }

$state = Get-Ft601State
if ($null -eq $state) {
    Write-Result ([ordered]@{schema='lockstep-ft601-bootstrap-v1'; success=$false; status='not_connected'; changed=$false}) 4
}
$driverReady = $state.device.Service -eq 'libusbK' -and $null -ne $state.driver -and
    $state.driver.ProviderName -match '^libusbK$' -and $state.driver.DriverVersion -eq '3.1.0.0'
if ($driverReady) {
    $productStatus = Invoke-ProductCheck $ProductExe
    Write-Result ([ordered]@{schema='lockstep-ft601-bootstrap-v1'; success=$true; status='ready'; changed=$false;
        serial=$state.serial; service=$state.device.Service; provider=$state.driver.ProviderName;
        driver_version=$state.driver.DriverVersion; product_usb_status=$productStatus}) 0
}
if ($DryRun) {
    Write-Result ([ordered]@{schema='lockstep-ft601-bootstrap-v1'; success=$true; status='binding_required'; changed=$false;
        serial=$state.serial; current_service=$state.device.Service; planned_service='libusbK'}) 0
}

$resolvedZadig = (Resolve-Path -LiteralPath $ZadigPath).Path
$zadigHash = (Get-FileHash -LiteralPath $resolvedZadig -Algorithm SHA256).Hash.ToLowerInvariant()
if ($zadigHash -ne '4ecaa95df3da3621486a043aef8b3050b8bafe7c901402871e816229ef82039b') {
    throw "Unexpected Zadig SHA256: $zadigHash"
}
$signature = Get-AuthenticodeSignature -LiteralPath $resolvedZadig
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Akeo Consulting') {
    throw "Zadig signature verification failed: $($signature.Status)"
}

if (-not (Test-Administrator)) {
    Write-Result ([ordered]@{schema='lockstep-ft601-bootstrap-v1';success=$false;
        status='elevation_required';changed=$false;serial=$state.serial}) 5
}
if (-not $AlreadyElevated) {
    throw 'Administrator execution is accepted only from the product elevated bootstrap mode.'
}

if ($null -eq $state.driver -or $state.driver.InfPath -notmatch '^oem\d+\.inf$') {
    throw 'Current FT601 driver cannot be backed up; refusing to change it.'
}
$timestamp = Get-Date -Format 'yyyyMMddTHHmmss'
$backupRoot = Join-Path $scriptRoot "driver-backup\ft601\$timestamp"
[void](New-Item -ItemType Directory -Force -Path $backupRoot)
& pnputil.exe /export-driver $state.driver.InfPath $backupRoot | Out-File (Join-Path $backupRoot 'export.log') -Encoding UTF8
if ($LASTEXITCODE -ne 0 -or -not (Get-ChildItem $backupRoot -Recurse -Filter '*.inf' -File)) {
    throw 'FT601 driver backup failed.'
}

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class LockstepFt601Zadig {
    [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr GetDlgItem(IntPtr parent, int id);
    [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr SendMessage(IntPtr w, uint m, IntPtr a, IntPtr b);
    [DllImport("user32.dll", EntryPoint="SendMessageA", CharSet=CharSet.Ansi, SetLastError=true)]
    public static extern IntPtr SendMessageText(IntPtr w, uint m, IntPtr a, StringBuilder b);
}
'@
function Get-ControlText([IntPtr]$window) {
    $text = [Text.StringBuilder]::new(8192)
    [void][LockstepFt601Zadig]::SendMessageText($window, 0x000D, [IntPtr]$text.Capacity, $text)
    return $text.ToString()
}
function Find-ComboItem([IntPtr]$combo, [string]$pattern) {
    $count = [int][LockstepFt601Zadig]::SendMessage($combo, 0x0146, [IntPtr]::Zero, [IntPtr]::Zero)
    for ($index=0; $index -lt $count; $index++) {
        $length = [int][LockstepFt601Zadig]::SendMessage($combo, 0x0149, [IntPtr]$index, [IntPtr]::Zero)
        $text = [Text.StringBuilder]::new($length + 2)
        [void][LockstepFt601Zadig]::SendMessageText($combo, 0x0148, [IntPtr]$index, $text)
        if ($text.ToString() -match $pattern) { return $index }
    }
    return -1
}

$process = Start-Process -FilePath $resolvedZadig -WorkingDirectory $scriptRoot -WindowStyle Hidden -PassThru
try {
    [void]$process.WaitForInputIdle(15000)
    $deadline=(Get-Date).AddSeconds(30); $main=[IntPtr]::Zero
    do { $process.Refresh(); $main=$process.MainWindowHandle; if($main -ne [IntPtr]::Zero){break}; Start-Sleep -Milliseconds 200 } while((Get-Date)-lt $deadline)
    if($main -eq [IntPtr]::Zero){throw 'Zadig main window was not found.'}
    $deviceCombo=[LockstepFt601Zadig]::GetDlgItem($main,1001)
    $deviceIndex=Find-ComboItem $deviceCombo '^FTDI SuperSpeed-FIFO Bridge \(Interface 0\)$'
    if($deviceIndex -lt 0){throw 'FT601 MI_00 was not found in Zadig.'}
    [void][LockstepFt601Zadig]::SendMessage($deviceCombo,0x014E,[IntPtr]$deviceIndex,[IntPtr]::Zero)
    [void][LockstepFt601Zadig]::SendMessage($main,0x0111,[IntPtr]((1 -shl 16)-bor 1001),$deviceCombo)
    Start-Sleep -Seconds 1
    $driverCombo=[LockstepFt601Zadig]::GetDlgItem($main,1011)
    $driverIndex=Find-ComboItem $driverCombo '^libusbK'
    if($driverIndex -lt 0){throw 'libusbK is unavailable in Zadig.'}
    [void][LockstepFt601Zadig]::SendMessage($driverCombo,0x014E,[IntPtr]$driverIndex,[IntPtr]::Zero)
    [void][LockstepFt601Zadig]::SendMessage($main,0x0111,[IntPtr]((1 -shl 16)-bor 1011),$driverCombo)
    Start-Sleep -Milliseconds 500
    $vid=Get-ControlText ([LockstepFt601Zadig]::GetDlgItem($main,1002))
    $pid=Get-ControlText ([LockstepFt601Zadig]::GetDlgItem($main,1003))
    $mi=Get-ControlText ([LockstepFt601Zadig]::GetDlgItem($main,1005))
    $driverName=Get-ControlText $driverCombo
    if($vid -notmatch '0403' -or $pid -notmatch '601F' -or $mi -notmatch '00' -or $driverName -notmatch '^libusbK') {
        throw "Zadig safety check failed: VID=$vid PID=$pid MI=$mi driver=$driverName"
    }
    $preinstallState = Get-Ft601State
    if ($null -eq $preinstallState -or $preinstallState.instance.PSChildName -ne $state.instance.PSChildName) {
        throw 'FT601 changed after selection; refusing to install a driver.'
    }
    [void][LockstepFt601Zadig]::SendMessage([LockstepFt601Zadig]::GetDlgItem($main,1009),0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    $deadline=(Get-Date).AddMinutes(4)
    do {
        $info=Get-ControlText ([LockstepFt601Zadig]::GetDlgItem($main,1004))
        if($info -match 'Driver Installation: FAILED|Driver Installation: Cancelled'){throw $info}
        if($info -match 'Driver Installation: SUCCESS'){break}
        Start-Sleep -Milliseconds 500; $process.Refresh()
        if($process.HasExited -and $process.ExitCode -eq 0){break}
        if($process.HasExited){throw "Zadig exited with code $($process.ExitCode)."}
    } while((Get-Date)-lt $deadline)
} finally { if($null -ne $process -and -not $process.HasExited){[void]$process.CloseMainWindow()} }

$verified=$false; $deadline=(Get-Date).AddSeconds(30)
do {
    Start-Sleep -Milliseconds 500
    $state=Get-Ft601State
    if($null -ne $state -and $state.device.Service -eq 'libusbK' -and $state.driver.ProviderName -match '^libusbK$' -and
        $state.driver.DriverVersion -eq '3.1.0.0'){$verified=$true;break}
} while((Get-Date)-lt $deadline)
if(-not $verified){throw 'FT601 did not bind to libusbK 3.1.0.0.'}
$productStatus=Invoke-ProductCheck $ProductExe
Write-Result ([ordered]@{schema='lockstep-ft601-bootstrap-v1';success=$true;status='ready';changed=$true;
    serial=$state.serial;service=$state.device.Service;provider=$state.driver.ProviderName;
    driver_version=$state.driver.DriverVersion;backup=$backupRoot;product_usb_status=$productStatus}) 0
