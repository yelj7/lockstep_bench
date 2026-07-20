# /**********************************************************
# * 文件名: refresh_hashes.ps1
# * 日期: 2026-07-20
# * 版本: v1.0
# * 更新记录: 初版，刷新 benchmark 关键产物 SHA-256
# * 描述: 生成可移植的 SHA256SUMS
# **********************************************************/

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$relativePaths = @(
    "benchmark_manifest.json",
    "expected_protocols.json",
    "firmware/noelv_eight_protocol_benchmark.c",
    "firmware/noelv_eight_protocol_benchmark.srec",
    "golden/waveform/capture.vcd",
    "golden/waveform/capture_schema.json",
    "golden/evidence/capture_sidecar.json",
    "golden/evidence/protocol_analysis.json"
)

$lines = foreach ($relativePath in $relativePaths) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path)) { throw "缺少 benchmark 产物: $relativePath" }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $($relativePath.Replace('\', '/'))"
}
$lines | Set-Content -LiteralPath (Join-Path $root "SHA256SUMS") -Encoding ascii
