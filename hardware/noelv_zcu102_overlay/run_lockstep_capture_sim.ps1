# /**********************************************************
# * 文件名: run_lockstep_capture_sim.ps1
# * 日期: 2026-07-17
# * 版本: 1.0
# * 更新记录: 新增采集 RTL 仿真摘要和日志 FAIL 门禁。
# * 描述: 编译并运行当前 1024-bit 采集回归，生成 simulation_gate.json。
# **********************************************************/

param(
    [string]$SourceRoot = (Join-Path $PSScriptRoot "rtl/lockstep_capture"),
    [string]$SourceManifest = (Join-Path $PSScriptRoot "rtl/lockstep_capture/lockstep_capture_sources.lst"),
    [string]$OutputDir = (Join-Path $PSScriptRoot "rtl_gate")
)

$ErrorActionPreference = "Stop"
$sourceRootPath = (Resolve-Path -LiteralPath $SourceRoot).Path
$manifestPath = (Resolve-Path -LiteralPath $SourceManifest).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$outputPath = (Resolve-Path -LiteralPath $OutputDir).Path

$sources = Get-Content -LiteralPath $manifestPath -Encoding UTF8 |
    Where-Object { $_ -and -not $_.StartsWith("#") } |
    ForEach-Object { Join-Path $sourceRootPath $_ }
$tests = @(
    "tb_lockstep_capture_arm_delay",
    "tb_lockstep_capture_recovery",
    "tb_lockstep_command_responses",
    "tb_lockstep_ft601_hello",
    "tb_lockstep_protocol_probe_real_only",
    "tb_lockstep_event_capture_core",
    "tb_lockstep_event_async_fifo",
    "tb_lockstep_event_capture_controller",
    "tb_lockstep_protocol_event_encoder",
    "tb_lockstep_event_frame_source",
    "tb_lockstep_event_config_parser",
    "tb_lockstep_capture_stream_arbiter",
    "tb_lockstep_global_timestamp"
)
$testSources = $tests | ForEach-Object { Join-Path $sourceRootPath "tests/$_.v" }
$digestFiles = @($sources) + @($testSources) + @(
    (Join-Path $sourceRootPath "include/lockstep_capture_protocol_v2.vh"),
    (Join-Path $sourceRootPath "include/lockstep_capture_protocol_v3.vh")
)
$digestLines = $digestFiles | Sort-Object | ForEach-Object {
    $absolutePath = (Resolve-Path -LiteralPath $_).Path
    $relativePath = $absolutePath.Substring($sourceRootPath.Length).TrimStart("\", "/").Replace("\", "/")
    "$relativePath|$((Get-FileHash -Algorithm SHA256 -LiteralPath $absolutePath).Hash)"
}
$digestBytes = [System.Text.Encoding]::UTF8.GetBytes(($digestLines -join "`n"))
$digestStream = [System.IO.MemoryStream]::new($digestBytes)
$sourceDigest = (Get-FileHash -Algorithm SHA256 -InputStream $digestStream).Hash.ToLowerInvariant()
$digestStream.Dispose()

$previous = Get-Location
$results = @()
try {
    Set-Location -LiteralPath $outputPath
    & xvlog --include (Join-Path $sourceRootPath "include") @sources @testSources
    if ($LASTEXITCODE -ne 0) { throw "xvlog failed" }
    foreach ($top in $tests) {
        & xelab $top -s $top --debug typical
        if ($LASTEXITCODE -ne 0) { throw "xelab failed: $top" }
        $output = & xsim $top -runall 2>&1
        $logPath = Join-Path $outputPath "$top.log"
        $output | Set-Content -LiteralPath $logPath -Encoding UTF8
        if (($output -join "`n") -match "(?m)^FAIL") {
            throw "simulation reported FAIL: $top"
        }
        if (($output -join "`n") -notmatch "(?m)^PASS $top$") {
            throw "simulation did not report PASS: $top"
        }
        $results += [ordered]@{ name = $top; status = "pass"; log = "$top.log" }
    }
} finally {
    Set-Location -LiteralPath $previous
}

$gate = [ordered]@{
    schema = "lockstep-simulation-gate-v1"
    generated_at = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ss.fffZ")
    source_digest = $sourceDigest
    source_manifest = $manifestPath.Replace("\", "/")
    status = "pass"
    tests = $results
}
$gate | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath "simulation_gate.json") -Encoding UTF8
Write-Output "PASS lockstep capture simulation gate digest=$sourceDigest"
