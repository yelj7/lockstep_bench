# /**********************************************************
# * 文件名: build.ps1
# * 日期: 2026-07-20
# * 版本: v1.0
# * 更新记录: 初版，构建 ELF、SREC、反汇编和 SHA256
# * 描述: 使用开源 RISC-V GNU 工具链构建八协议 benchmark 固件
# **********************************************************/

param(
    [string]$ToolchainBin = "D:\Program Files\msys64\ucrt64\bin",
    [ValidateSet(0, 1)][int]$EnableSpi = 1,
    [ValidateSet(0, 1)][int]$EnableCan = 1,
    [ValidateSet(0, 1)][int]$EnableI2c = 1,
    [ValidateSet(0, 1)][int]$EnableEth = 0,
    [ValidateSet(0, 1)][int]$EnableUsb = 0
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = "D:\tmp\lockstep\firmware-build\eight_protocol"
New-Item -ItemType Directory -Force -Path $build | Out-Null

$prefix = Join-Path $ToolchainBin "riscv64-unknown-elf-"
$gcc = "${prefix}gcc.exe"
$objcopy = "${prefix}objcopy.exe"
$objdump = "${prefix}objdump.exe"
foreach ($tool in @($gcc, $objcopy, $objdump)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "缺少 RISC-V 工具: $tool" }
}

$source = Join-Path $root "noelv_eight_protocol_benchmark.c"
$linker = Join-Path $root "linker.ld"
$elf = Join-Path $build "noelv_eight_protocol_benchmark.elf"
$map = Join-Path $build "noelv_eight_protocol_benchmark.map"
$listing = Join-Path $build "noelv_eight_protocol_benchmark.lst"
$srec = Join-Path $root "noelv_eight_protocol_benchmark.srec"

& $gcc -mcmodel=medany -static -std=gnu99 -O2 -march=rv64ima -mabi=lp64 `
    -Wall -Wextra -Werror `
    "-DBENCH_ENABLE_SPI=$EnableSpi" "-DBENCH_ENABLE_CAN=$EnableCan" `
    "-DBENCH_ENABLE_I2C=$EnableI2c" "-DBENCH_ENABLE_ETH=$EnableEth" `
    "-DBENCH_ENABLE_USB=$EnableUsb" `
    -ffreestanding -fno-builtin -nostdlib -nostartfiles `
    "-Wl,-T,$linker" "-Wl,-Map,$map" $source -lgcc -o $elf
if ($LASTEXITCODE -ne 0) { throw "固件链接失败" }

& $objcopy --srec-len=16 --srec-forceS3 --gap-fill=0 `
    --remove-section=.comment --remove-section=.riscv.attributes -O srec $elf $srec
if ($LASTEXITCODE -ne 0) { throw "SREC 生成失败" }

& $objdump --disassemble-all --disassemble-zeroes $elf | Set-Content -Encoding ascii $listing
Get-FileHash -Algorithm SHA256 $elf, $srec | Format-Table -AutoSize
