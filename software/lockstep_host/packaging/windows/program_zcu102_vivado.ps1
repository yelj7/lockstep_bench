# /**********************************************************
# * 文件名: program_zcu102_vivado.ps1
# * 日期: 2026-07-20
# * 版本: 1.0
# * 更新记录: 新增原厂 HS2 驱动门禁下的 Vivado Hardware Manager 烧写入口。
# * 描述: 校验 HS2、bitstream 哈希和 Vivado 后执行 ZCU102 PL 配置。
# **********************************************************/

param(
    [Parameter(Mandatory = $true)][string]$Bitstream,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$ExpectedSha256,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$Hs2Serial,
    [string]$VivadoBat = 'D:\Xilinx\Vivado\2022.2\bin\vivado.bat'
)

$ErrorActionPreference = 'Stop'
$bit = (Resolve-Path -LiteralPath $Bitstream).Path
if ([IO.Path]::GetExtension($bit) -ine '.bit') { throw 'Bitstream must use the .bit extension.' }
$actualHash = (Get-FileHash -LiteralPath $bit -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $ExpectedSha256.ToLowerInvariant()) {
    throw "Bitstream SHA256 mismatch: expected=$ExpectedSha256 actual=$actualHash"
}
$vivado = (Resolve-Path -LiteralPath $VivadoBat).Path

$instanceId = 'USB\VID_0403&PID_6014\' + $Hs2Serial
$enumeration = (& pnputil.exe /enum-devices /connected /instanceid $instanceId /ids 2>&1) | Out-String
if ($enumeration -notmatch [regex]::Escape($instanceId)) { throw "HS2 $Hs2Serial is not connected." }
$devicePath = 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_0403&PID_6014\' + $Hs2Serial
$device = Get-ItemProperty -LiteralPath $devicePath
if ($device.Service -ne 'FTDIBUS') { throw "HS2 must use the FTDI factory service; actual=$($device.Service)" }
$driver = Get-ItemProperty -LiteralPath ('HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $device.Driver)
if ($driver.ProviderName -notmatch '^FTDI$') { throw "HS2 driver provider must be FTDI; actual=$($driver.ProviderName)" }

$tcl = Join-Path $PSScriptRoot 'program_zcu102_vivado.tcl'
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $vivado
$info.Arguments = "-mode batch -nojournal -nolog -notrace -source `"$tcl`" -tclargs `"$bit`" `"$Hs2Serial`""
$info.WorkingDirectory = Split-Path -Parent $bit
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
    if (-not $process.WaitForExit(600000)) { $process.Kill(); throw 'Vivado hardware programming timed out.' }
    [Threading.Tasks.Task]::WaitAll(@($stdoutTask, $stderrTask))
    $combined = $stdoutTask.Result + "`n" + $stderrTask.Result
    $combined
    if ($process.ExitCode -ne 0 -or $combined -notmatch 'LOCKSTEP_PROGRAM_SUCCESS') {
        throw "Vivado hardware programming failed with exit code $($process.ExitCode)."
    }
} finally { $process.Dispose() }

[ordered]@{
    schema = 'lockstep-zcu102-vivado-program-v1'
    success = $true
    hs2_serial = $Hs2Serial
    hs2_service = $device.Service
    hs2_provider = $driver.ProviderName
    bitstream = $bit
    sha256 = $actualHash
} | ConvertTo-Json -Depth 4
