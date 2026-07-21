# /**********************************************************
# * 文件名: generate_source_lock.ps1
# * 日期: 2026-07-19
# * 版本: 1.0
# * 更新记录: 新增基线和覆盖层输入摘要生成脚本。
# * 描述: 将 Vivado source manifest 转换为相对路径 SHA-256 锁文件。
# **********************************************************/

param(
    [string]$BaselineRoot = '',
    [switch]$ReuseBaselineFromLock,
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $PSScriptRoot 'source_lock.csv'
}
$overlay = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$manifestPath = Join-Path $PSScriptRoot 'baseline_source_manifest.txt'
$rows = [System.Collections.Generic.List[object]]::new()
$baselineRows = @{}
if ($ReuseBaselineFromLock) {
    if (-not (Test-Path -LiteralPath $OutputPath -PathType Leaf)) {
        throw "source lock does not exist for baseline reuse: $OutputPath"
    }
    foreach ($row in Import-Csv -LiteralPath $OutputPath -Encoding UTF8) {
        if ($row.ownership -eq 'baseline') { $baselineRows[$row.relative_path] = $row.sha256 }
    }
} else {
    if ([string]::IsNullOrWhiteSpace($BaselineRoot)) { throw 'BaselineRoot is required without ReuseBaselineFromLock.' }
    $baseline = (Resolve-Path -LiteralPath $BaselineRoot).Path
}

foreach ($line in Get-Content -LiteralPath $manifestPath -Encoding UTF8) {
    if ([string]::IsNullOrWhiteSpace($line) -or $line.Contains('=')) { continue }
    $normalized = $line.Replace('\', '/')
    $marker = '/hardware_source/'
    $markerIndex = $normalized.IndexOf($marker, [System.StringComparison]::OrdinalIgnoreCase)
    if ($markerIndex -lt 0) { throw "manifest path has no hardware_source marker: $line" }
    $relative = $normalized.Substring($markerIndex + $marker.Length)
    if ($ReuseBaselineFromLock) {
        if (-not $baselineRows.ContainsKey($relative)) {
            throw "baseline source is missing from existing lock: $relative"
        }
        $baselineHash = $baselineRows[$relative]
    } else {
        $candidate = Join-Path $baseline $relative
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "baseline source missing: $relative"
        }
        $baselineHash = (Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $rows.Add([pscustomobject]@{
        ownership = 'baseline'
        relative_path = $relative
        sha256 = $baselineHash
    })
}

foreach ($file in Get-ChildItem -LiteralPath $overlay -Recurse -File | Where-Object {
    $_.FullName -notlike "$PSScriptRoot*" -and
    $_.Extension -in '.v', '.vh', '.vhd', '.xdc', '.tcl', '.ps1', '.sh', '.md', '.lst'
}) {
    $relative = $file.FullName.Substring($overlay.Length + 1).Replace('\', '/')
    $rows.Add([pscustomobject]@{
        ownership = 'overlay'
        relative_path = $relative
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    })
}

$rows | Sort-Object ownership, relative_path | Export-Csv -LiteralPath $OutputPath -NoTypeInformation -Encoding UTF8
$digestInput = ($rows | Sort-Object ownership, relative_path | ForEach-Object {
    "$($_.ownership) $($_.relative_path) $($_.sha256)`n"
}) -join ''
$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $digest = [System.BitConverter]::ToString(
        $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($digestInput))).Replace('-', '').ToLowerInvariant()
} finally {
    $sha.Dispose()
}
Set-Content -LiteralPath (Join-Path (Split-Path -Parent $OutputPath) 'source_digest.txt') -Value $digest -Encoding ASCII
Write-Output $digest
