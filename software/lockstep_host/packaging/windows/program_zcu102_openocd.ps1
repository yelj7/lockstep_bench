# /**********************************************************
# * 文件名: program_zcu102_openocd.ps1
# * 日期: 2026-07-20
# * 版本: 1.1
# * 更新记录: 将 bitstream 哈希和 HS2 序列号改为强制输入。
# * 描述: 校验 bitstream 哈希和 HS2 WinUSB 后，通过 OpenOCD 配置 FPGA。
# **********************************************************/

param(
    [Parameter(Mandatory = $true)][string]$Bitstream,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$ExpectedSha256,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$Hs2Serial,
    [string]$OpenOcdExe = '',
    [string]$OpenOcdScripts = ''
)

$ErrorActionPreference = 'Stop'
$bit = (Resolve-Path -LiteralPath $Bitstream).Path
if ([IO.Path]::GetExtension($bit) -ine '.bit') { throw 'Bitstream must use the .bit extension.' }
$actualHash = (Get-FileHash -LiteralPath $bit -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $ExpectedSha256.ToLowerInvariant()) {
    throw "Bitstream SHA256 mismatch: expected=$ExpectedSha256 actual=$actualHash"
}
if ([string]::IsNullOrWhiteSpace($OpenOcdExe)) {
    $bundledOpenOcd = Join-Path $PSScriptRoot 'openocd\bin\openocd.exe'
    $OpenOcdExe = if (Test-Path -LiteralPath $bundledOpenOcd -PathType Leaf) {
        $bundledOpenOcd
    } else {
        'openocd.exe'
    }
}
$openOcd = (Get-Command $OpenOcdExe -ErrorAction Stop).Source
if ([string]::IsNullOrWhiteSpace($OpenOcdScripts)) {
    $bundledScripts = Join-Path $PSScriptRoot 'openocd\share\openocd\scripts'
    $OpenOcdScripts = if (Test-Path -LiteralPath $bundledScripts -PathType Container) {
        $bundledScripts
    } else {
        Join-Path (Split-Path -Parent (Split-Path -Parent $openOcd)) 'share\openocd\scripts'
    }
}
$scripts = (Resolve-Path -LiteralPath $OpenOcdScripts).Path
$config = Join-Path $PSScriptRoot 'openocd\lockstep-zcu102-hs2.cfg'

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'verify_lockstep_winusb.ps1') `
    -Target HS2 -ExpectedSerial $Hs2Serial -OpenOcdExe $openOcd -OpenOcdScripts $scripts
if ($LASTEXITCODE -ne 0) { throw 'HS2 WinUSB verification failed.' }

$command = "adapter serial $Hs2Serial; init; pld load 0 {$($bit.Replace('\', '/'))}; shutdown"
$output = & $openOcd -s $scripts -f $config -c $command 2>&1
$exitCode = $LASTEXITCODE
$output
if ($exitCode -ne 0 -or ($output -join "`n") -notmatch 'loaded file') {
    throw "OpenOCD FPGA programming failed with exit code $exitCode."
}
[ordered]@{
    schema = 'lockstep-zcu102-openocd-program-v1'
    success = $true
    bitstream = $bit
    sha256 = $actualHash
} | ConvertTo-Json -Depth 4
