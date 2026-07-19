# /**********************************************************
# * 文件名: check_lockstep_reset_mapping.ps1
# * 日期: 2026-07-17
# * 版本: 1.0
# * 更新记录: 新增 FT601 域与采样域复位映射检查。
# * 描述: 防止 FT601 协议域再次被 MIG/处理器系统复位链阻断。
# **********************************************************/

param(
    [string]$TopPath = (Join-Path $PSScriptRoot "../../noelvmp.vhd")
)

$source = Get-Content -LiteralPath $TopPath -Raw -Encoding UTF8
$instance = [regex]::Match(
    $source,
    "lockstep_ft601_capture\s*:\s*lockstep_ft601_external_sample_top(?s:.*?)\);"
)

if (-not $instance.Success) {
    throw "未找到 lockstep_ft601_capture 实例"
}
if ($instance.Value -notmatch "rst_n\s*=>\s*resetn") {
    throw "FT601 协议域必须连接板级原始 resetn"
}
if ($instance.Value -notmatch "sample_rst_n\s*=>\s*rstn") {
    throw "采样域必须连接 NOEL-V 系统 rstn"
}

Write-Output "PASS check_lockstep_reset_mapping"
