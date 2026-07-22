# /**********************************************************
# * 文件名: validate_board_evidence.ps1
# * 日期: 2026-07-21
# * 版本: v1.0
# * 更新记录: 初版，固化扩展 benchmark 的真实板级证据门禁
# * 描述: 校验 capture ID、镜像摘要、事件完整性和各协议最低行为数量
# **********************************************************/

param(
    [Parameter(Mandatory = $true)][string]$CaptureRoot,
    [string]$ExpectedSrecSha256 = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $CaptureRoot).Path

function Read-Json([string]$RelativePath) {
    $path = Join-Path $root $RelativePath
    if (-not (Test-Path -LiteralPath $path)) { throw "缺少证据文件: $RelativePath" }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "板级证据门禁失败: $Message" }
}

function Get-ProtocolGroup([object]$Analysis, [string]$Id) {
    $group = @($Analysis.groups | Where-Object { $_.id -eq $Id })
    if ($group.Count -ne 1) { throw "协议组缺失或重复: $Id" }
    return $group[0]
}

if ([string]::IsNullOrWhiteSpace($ExpectedSrecSha256)) {
    $sumPath = Join-Path $PSScriptRoot 'SHA256SUMS'
    $line = Get-Content -LiteralPath $sumPath -Encoding ASCII |
        Where-Object { $_ -match '  firmware/noelv_eight_protocol_benchmark\.srec$' } |
        Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($line)) { throw 'SHA256SUMS 缺少固件 SREC 摘要' }
    $ExpectedSrecSha256 = ($line -split '  ', 2)[0]
}
$ExpectedSrecSha256 = $ExpectedSrecSha256.ToLowerInvariant()

$report = Read-Json 'reports/report.json'
$sidecar = Read-Json 'evidence/capture_sidecar.json'
$events = Read-Json 'evidence/protocol_events.json'
$status = Read-Json 'evidence/capture_status.json'
$analysis = Read-Json 'evidence/protocol_analysis.json'
$benchmarkManifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'benchmark_manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$expectedJtagScans = [int]$benchmarkManifest.board_behavior_matrix.jtag.host_scans
$expectedJtagScansPerType = [int]($expectedJtagScans / 2)

Assert-True ($report.conclusion -eq 'pass') '报告结论不是 pass'
Assert-True ($status.success -eq $true -and $status.phase -eq 'complete' -and $status.status.last_error_code -eq 0) '采集状态不是无错误完成'
Assert-True ($sidecar.stop_reason -eq 0 -and $events.end_reason -eq 0) '采集不是由 program_done 正常结束'
Assert-True ($report.program.sha256 -eq $ExpectedSrecSha256) '报告中的程序 SHA-256 不匹配'
Assert-True ($report.capture_id -eq $sidecar.capture_id -and
             $sidecar.capture_id -eq $events.capture_id -and
             $events.capture_id -eq $status.status.capture_id) 'capture ID 不一致'
Assert-True ($sidecar.actual_sample_count -eq 4096 -and $sidecar.sample_word_bits -eq 1024) `
    '周期窗口不是 4096 x 1024-bit'
Assert-True ($events.accepted_total -eq $events.emitted_total -and
             $events.dropped_total -eq 0 -and $events.overflow_mask -eq 0) '事件丢失或溢出'
Assert-True ($events.design_gap_mask -eq 0x60) 'ETH/USB design_gap mask 不正确'
Assert-True ($analysis.program_done_marker_detected -eq $true -and
             $analysis.uart_tx_text.Contains('PROGRAM_RUN_DONE')) 'UART 结束标志未恢复'

$ahb = Get-ProtocolGroup $analysis 'ahb'
$uart = Get-ProtocolGroup $analysis 'uart'
$spi = Get-ProtocolGroup $analysis 'spi'
$can = Get-ProtocolGroup $analysis 'can'
$iTwoCGroup = Get-ProtocolGroup $analysis 'i2c'
$eth = Get-ProtocolGroup $analysis 'eth'
$usb = Get-ProtocolGroup $analysis 'usb'
$jtag = Get-ProtocolGroup $analysis 'jtag'
$mismatch = Get-ProtocolGroup $analysis 'mismatch'

$counts = [ordered]@{
    ahb = @($ahb.transactions | Where-Object { $_.type -eq 'ahb_transfer' }).Count
    uart = @($uart.transactions | Where-Object { $_.type -eq 'uart_frame' }).Count
    spi = @($spi.transactions | Where-Object { $_.type -eq 'spi_transfer' }).Count
    can = @($can.transactions | Where-Object { $_.type -eq 'raw_line_event' }).Count
    i2c_transfer = @($analysis.protocol_events | Where-Object {
            $_.group_id -eq 'i2c' -and $_.type -eq 'i2c_transfer'
        }).Count
    i2c_repeated_start = @($analysis.protocol_events | Where-Object {
            $_.group_id -eq 'i2c' -and $_.type -eq 'i2c_segment'
        }).Count
    jtag = @($jtag.transactions | Where-Object { $_.type -eq 'jtag_scan' }).Count
}
$spiTxValues = @($spi.transactions | Where-Object { $_.type -eq 'spi_transfer' } |
    ForEach-Object { $_.fields.tx_data })
$repeatedStartSummaries = @($analysis.protocol_events | Where-Object {
        $_.group_id -eq 'i2c' -and $_.type -eq 'i2c_segment' -and
        $_.summary -like 'I2C REPEATED_START*'
    })
$jtagIrScans = @($jtag.transactions | Where-Object {
        $_.type -eq 'jtag_scan' -and $_.fields.register -eq 'ir'
    }).Count
$jtagDrScans = @($jtag.transactions | Where-Object {
        $_.type -eq 'jtag_scan' -and $_.fields.register -eq 'dr'
    }).Count

Assert-True ($counts.ahb -ge 256) 'AHB 完整事务少于 256'
Assert-True ($counts.uart -ge 48) 'UART 重建字节少于 48'
Assert-True ($analysis.uart_tx_text.Contains([char]0x00) -and
             $analysis.uart_tx_text.Contains([char]0xff) -and
             $analysis.uart_tx_text.Contains([char]0xa5) -and
             $analysis.uart_tx_text.Contains([char]0x80) -and
             $analysis.uart_tx_text.Contains([char]0x7f)) 'UART 二进制边界矩阵不完整'
Assert-True ($counts.spi -ge 48) 'SPI 完整 transfer 少于 48'
Assert-True ($spiTxValues -contains '0x00000000' -and
             $spiTxValues -contains '0xFF000000') 'SPI 未捕获 0x00/0xFF 边界 TX 数据'
Assert-True ($can.status -eq 'limited' -and $counts.can -ge 32) `
    'CAN 必须保留 limited 且至少有 32 条真实线级事件'
if ($counts.i2c_transfer -lt 48 -or $counts.i2c_repeated_start -lt 12) {
    throw "板级证据门禁失败: I2C transfer/repeated START 数量不足: $($counts.i2c_transfer)/$($counts.i2c_repeated_start)"
}
Assert-True ($repeatedStartSummaries.Count -ge 12) 'I2C segment 未解析为 REPEATED_START'
Assert-True ($eth.status -eq 'unavailable' -and $usb.status -eq 'unavailable') `
    'ETH/USB 必须保持 unavailable/design_gap'
Assert-True ($counts.jtag -ge $expectedJtagScans) "JTAG 完整 scan 少于 $expectedJtagScans"
Assert-True ($jtagIrScans -ge $expectedJtagScansPerType -and $jtagDrScans -ge $expectedJtagScansPerType) "JTAG IR/DR scan 分别少于 $expectedJtagScansPerType"
Assert-True ($mismatch.status -eq 'complete' -and $mismatch.transactions.Count -eq 0) `
    'Mismatch 不应产生事件'

foreach ($artifact in $report.artifacts) {
    $path = Join-Path $root $artifact.relative_path
    Assert-True (Test-Path -LiteralPath $path) "报告产物缺失: $($artifact.relative_path)"
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-True ($actual -eq $artifact.sha256) "报告产物摘要不匹配: $($artifact.relative_path)"
}

[ordered]@{
    success = $true
    capture_id = $sidecar.capture_id
    program_sha256 = $report.program.sha256
    samples = $sidecar.actual_sample_count
    sample_word_bits = $sidecar.sample_word_bits
    events = $events.emitted_total
    ahb_transfers = $counts.ahb
    uart_frames = $counts.uart
    spi_transfers = $counts.spi
    can_raw_events = $counts.can
    can_status = $can.status
    i2c_transfers = $counts.i2c_transfer
    i2c_repeated_starts = $counts.i2c_repeated_start
    jtag_scans = $counts.jtag
    design_gap_mask = '0x60'
} | ConvertTo-Json -Depth 3
