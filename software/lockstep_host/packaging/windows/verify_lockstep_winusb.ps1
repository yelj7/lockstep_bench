# /**********************************************************
# * 文件名: verify_lockstep_winusb.ps1
# * 日期: 2026-07-20
# * 版本: 1.1
# * 更新记录: 默认强制功能验收，增加显式 DriverOnly 和序列号门禁。
# * 描述: 验证驱动服务，并可调用产品 USB 诊断或 OpenOCD JTAG 扫描。
# **********************************************************/

param(
    [string]$Target = 'FT601',
    [ValidatePattern('^[A-Za-z0-9._-]*$')][string]$ExpectedSerial = '',
    [switch]$DriverOnly,
    [string]$ProductExe = '',
    [string]$OpenOcdExe = '',
    [string]$OpenOcdScripts = ''
)

$ErrorActionPreference = 'Stop'
$targets = @{
    FT601 = [ordered]@{ path='HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_601F&MI_00'; mi='00' }
    HS2 = [ordered]@{ path='HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_6014'; mi='' }
}
$normalizedTarget = $Target.ToUpperInvariant()
if (-not $targets.ContainsKey($normalizedTarget)) { throw "Unsupported USB target: $Target" }
$spec = $targets[$normalizedTarget]
if ([string]::IsNullOrWhiteSpace($ExpectedSerial)) {
    throw "ExpectedSerial is required for $normalizedTarget verification."
}
if (-not $DriverOnly -and $normalizedTarget -eq 'FT601' -and
    [string]::IsNullOrWhiteSpace($ProductExe)) {
    throw 'ProductExe is required for the default FT601 functional verification; use DriverOnly explicitly to skip it.'
}
if (-not $DriverOnly -and $normalizedTarget -eq 'HS2' -and
    ([string]::IsNullOrWhiteSpace($OpenOcdExe) -or [string]::IsNullOrWhiteSpace($OpenOcdScripts))) {
    throw 'OpenOcdExe and OpenOcdScripts are required for the default HS2 functional verification; use DriverOnly explicitly to skip it.'
}
$instances = if (Test-Path -LiteralPath $spec.path) { @(Get-ChildItem -LiteralPath $spec.path) } else { @() }
$present = foreach ($instance in $instances) {
    $properties = Get-ItemProperty -LiteralPath $instance.PSPath
    $prefix = if ($normalizedTarget -eq 'FT601') {
        'USB\VID_0403&PID_601F&MI_00\'
    } else {
        'USB\VID_0403&PID_6014\'
    }
    $instanceId = $prefix + $instance.PSChildName
    $enumeration = (& pnputil.exe /enum-devices /connected /instanceid $instanceId /ids 2>&1) | Out-String
    if ($enumeration -match [regex]::Escape($instanceId)) {
        [pscustomobject]@{ instance=$instance; properties=$properties }
    }
}
if (@($present).Count -ne 1) {
    throw "Expected exactly one connected $normalizedTarget instance, found $(@($present).Count)."
}
$selected = @($present)[0]
$actualSerial = if ($normalizedTarget -eq 'FT601') {
    $interfacePrefix = $selected.instance.PSChildName -replace '&\d{4}$', ''
    $parentPath = 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_601F'
    $parents = @(Get-ChildItem -LiteralPath $parentPath | Where-Object {
        (Get-ItemProperty -LiteralPath $_.PSPath).ParentIdPrefix -eq $interfacePrefix
    })
    if ($parents.Count -ne 1) { throw 'Unable to resolve the FT601 composite parent serial.' }
    $parents[0].PSChildName
} else {
    $selected.instance.PSChildName
}
if (-not [string]::IsNullOrWhiteSpace($ExpectedSerial) -and
    $actualSerial -ine $ExpectedSerial) {
    throw "Connected $normalizedTarget serial mismatch: expected=$ExpectedSerial actual=$actualSerial"
}
$driver = if ($selected.properties.Driver) {
    Get-ItemProperty -LiteralPath ('HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $selected.properties.Driver)
} else { $null }
$service = $selected.properties.Service
$driverOk = $service -eq 'WinUSB'
$result = [ordered]@{
    schema = 'lockstep-winusb-verification-v1'
    success = $false
    target = $normalizedTarget
    device_instance = $actualSerial
    service = $service
    driver_provider = if ($driver) { $driver.ProviderName } else { '' }
    driver_version = if ($driver) { $driver.DriverVersion } else { '' }
    inf_name = if ($driver) { $driver.InfPath } else { '' }
    functional_check = $null
    driver_only = [bool]$DriverOnly
}
if (-not $driverOk) {
    $result | ConvertTo-Json -Depth 8
    throw "$normalizedTarget is not bound to WinUSB."
}

function Invoke-Captured([string]$fileName, [string]$arguments, [string]$workingDirectory) {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $fileName
    $startInfo.Arguments = $arguments
    $startInfo.WorkingDirectory = $workingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        [void]$process.Start()
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) {
            $process.Kill()
            throw "Timed out running $fileName."
        }
        [Threading.Tasks.Task]::WaitAll(@($stdoutTask, $stderrTask))
        return [pscustomobject]@{
            exit_code = $process.ExitCode
            stdout = $stdoutTask.Result
            stderr = $stderrTask.Result
        }
    } finally {
        $process.Dispose()
    }
}

if (-not $DriverOnly -and $normalizedTarget -eq 'FT601') {
    $exe = (Resolve-Path -LiteralPath $ProductExe).Path
    $check = Invoke-Captured $exe '--usb-status' (Split-Path -Parent $exe)
    if ($check.exit_code -ne 0) { throw "Product USB check failed: $($check.stderr)" }
    $status = $check.stdout | ConvertFrom-Json
    if (-not $status.success -or $status.selected_usb_device.vid -ne '0x0403' -or
        $status.selected_usb_device.pid -ne '0x601f' -or
        $status.selected_usb_device.out_endpoint -ne '0x02' -or
        $status.selected_usb_device.in_endpoint -ne '0x82') {
        throw "Product USB contract mismatch: $($check.stdout)"
    }
    $result.functional_check = $status
}

if (-not $DriverOnly -and $normalizedTarget -eq 'HS2') {
    $openOcd = (Resolve-Path -LiteralPath $OpenOcdExe).Path
    $scripts = (Resolve-Path -LiteralPath $OpenOcdScripts).Path
    $config = Join-Path $PSScriptRoot 'openocd\lockstep-zcu102-hs2.cfg'
    $check = Invoke-Captured $openOcd (
        "-s `"$scripts`" -f `"$config`" -c `"adapter serial $ExpectedSerial; init; scan_chain; shutdown`"") $PSScriptRoot
    $combined = $check.stdout + "`n" + $check.stderr
    if ($check.exit_code -ne 0 -or $combined -notmatch '0x5ba00477' -or
        $combined -notmatch '0x047[0-9a-f]{3}93') {
        throw "OpenOCD ZynqMP scan failed: $combined"
    }
    $result.functional_check = [ordered]@{ exit_code=0; dap_id='0x5ba00477'; ps_tap_detected=$true }
}

$result.success = $true
$result | ConvertTo-Json -Depth 8
