# /**********************************************************
# * 文件名: install_lockstep_all_winusb.ps1
# * 日期: 2026-07-20
# * 版本: 1.0
# * 更新记录: 新增 FT601 与 HS2 单次 UAC 联合部署入口。
# * 描述: 一次提权后按已知序列号依次安装两个 WinUSB 目标。
# **********************************************************/

param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$Ft601Serial,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$Hs2Serial,
    [string]$ZadigPath = '',
    [switch]$DryRun,
    [string]$ResultPath = ''
)

$ErrorActionPreference = 'Stop'

function Test-Administrator {
    $principal = [Security.Principal.WindowsPrincipal]::new(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if ($ZadigPath.Contains('"')) { throw 'ZadigPath must not contain a quote character.' }
if (-not $DryRun -and -not (Test-Administrator)) {
    $resultRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    $resultFile = Join-Path $resultRoot ("lockstep-winusb-result-{0}.json" -f [guid]::NewGuid())
    $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" +
        " -Ft601Serial `"$Ft601Serial`" -Hs2Serial `"$Hs2Serial`" -ResultPath `"$resultFile`""
    if (-not [string]::IsNullOrWhiteSpace($ZadigPath)) {
        $arguments += " -ZadigPath `"$ZadigPath`""
    }
    try {
        $elevated = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
        if (Test-Path -LiteralPath $resultFile -PathType Leaf) {
            Get-Content -Raw -Encoding UTF8 -LiteralPath $resultFile
        } else {
            Write-Error 'Elevated installer did not return a structured result.'
        }
        exit $elevated.ExitCode
    } finally {
        Remove-Item -LiteralPath $resultFile -Force -ErrorAction SilentlyContinue
    }
}

if (-not [string]::IsNullOrWhiteSpace($ResultPath)) {
    $resultRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    $resolvedResult = [IO.Path]::GetFullPath($ResultPath)
    if (-not $resolvedResult.StartsWith($resultRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedResult) -notmatch '^lockstep-winusb-result-[0-9a-f-]+\.json$') {
        throw 'ResultPath must be a bootstrap-generated file under the current TEMP directory.'
    }
    $ResultPath = $resolvedResult
}

$installer = Join-Path $PSScriptRoot 'install_lockstep_winusb.ps1'
$common = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $installer)
$results = @()
$failure = $null
foreach ($entry in @(
    [ordered]@{ target='FT601'; serial=$Ft601Serial },
    [ordered]@{ target='HS2'; serial=$Hs2Serial }
)) {
    $arguments = @($common + @('-Target', $entry.target, '-ExpectedSerial', $entry.serial))
    if ($DryRun) { $arguments += '-DryRun' }
    if (-not [string]::IsNullOrWhiteSpace($ZadigPath)) { $arguments += @('-ZadigPath', $ZadigPath) }
    $output = & powershell.exe @arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        $failure = [ordered]@{ target=$entry.target; detail=($output | Out-String).Trim() }
        break
    }
    $results += ($output | ConvertFrom-Json)
}

$recovery = @($results | Where-Object { $_.changed } | ForEach-Object {
    [ordered]@{
        target = $_.target
        expected_serial = $_.device_instance
        backup_inf = $_.backup_inf
        installed_inf = $_.installed_inf
        expected_service = $_.previous_service
    }
})
$response = [ordered]@{
    schema = 'lockstep-winusb-install-all-v1'
    success = $null -eq $failure
    status = if ($null -eq $failure) { 'complete' } elseif ($results.Count) { 'partial_failure' } else { 'failed' }
    dry_run = [bool]$DryRun
    uac_count = if ($DryRun) { 0 } else { 1 }
    targets = $results
    failure = $failure
    recovery = $recovery
}
$responseJson = $response | ConvertTo-Json -Depth 8
if (-not [string]::IsNullOrWhiteSpace($ResultPath)) {
    $temporaryResult = $ResultPath + '.tmp'
    [IO.File]::WriteAllText($temporaryResult, $responseJson, [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporaryResult -Destination $ResultPath -Force
}
$responseJson
if ($null -ne $failure) { exit 1 }
