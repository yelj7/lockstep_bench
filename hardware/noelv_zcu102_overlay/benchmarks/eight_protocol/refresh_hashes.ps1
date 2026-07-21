# /**********************************************************
# * 文件名: refresh_hashes.ps1
# * 日期: 2026-07-20
# * 版本: v1.1
# * 更新记录: 将板级证据门禁脚本纳入关键产物摘要
# * 描述: 生成可移植的 SHA256SUMS
# **********************************************************/

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$relativePaths = @(
    "benchmark_manifest.json",
    "expected_protocols.json",
    "validate_board_evidence.ps1",
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
