# /**********************************************************
# * 文件名: windows_usb_bootstrap_test.ps1
# * 日期: 2026-07-20
# * 版本: 1.1
# * 更新记录: 覆盖序列号、功能验收、强制哈希、供应链锁定和临时文件清理。
# * 描述: 验证 FT601、HS2 的精确匹配和未知目标拒绝行为。
# **********************************************************/

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$installer = Join-Path $scriptRoot 'install_lockstep_winusb.ps1'
$allInstaller = Join-Path $scriptRoot 'install_lockstep_all_winusb.ps1'
$restorer = Join-Path $scriptRoot 'restore_lockstep_usb_driver.ps1'
$verifier = Join-Path $scriptRoot 'verify_lockstep_winusb.ps1'
$programmer = Join-Path $scriptRoot 'program_zcu102_openocd.ps1'
$builder = Join-Path $scriptRoot 'build_lockstep_winusb_bundle.ps1'

function Read-Target([string]$name) {
    $json = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $installer `
        -Target $name -DescribeTarget
    if ($LASTEXITCODE -ne 0) { throw "DescribeTarget failed for $name" }
    return $json | ConvertFrom-Json
}

$ft601 = Read-Target 'FT601'
if ($ft601.vid -ne '0403' -or $ft601.pid -ne '601F' -or $ft601.mi -ne '00' -or
    $ft601.driver -ne 'WinUSB' -or $ft601.registry_path -notmatch 'MI_00$') {
    throw 'FT601 WinUSB target contract mismatch.'
}

$hs2 = Read-Target 'HS2'
if ($hs2.vid -ne '0403' -or $hs2.pid -ne '6014' -or $hs2.mi -ne '' -or
    $hs2.driver -ne 'WinUSB' -or $hs2.registry_path -match '&MI_') {
    throw 'HS2 WinUSB target contract mismatch.'
}

$config = Get-Content -Raw -LiteralPath (Join-Path $scriptRoot 'openocd\lockstep-zcu102-hs2.cfg')
if ($config -notmatch 'ftdi vid_pid 0x0403 0x6014' -or
    $config -notmatch 'target/xilinx_zynqmp.cfg' -or
    $config -notmatch 'pld device virtex2 uscale.ps 1') {
    throw 'OpenOCD HS2/ZynqMP contract mismatch.'
}

function Invoke-ExpectedFailure([scriptblock]$command, [string]$message) {
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $command *> $null
        if ($LASTEXITCODE -eq 0) { throw $message }
    } finally {
        $ErrorActionPreference = $previousPreference
    }
}

$notBitstream = Join-Path $env:TEMP ("lockstep-not-a-bitstream-{0}.txt" -f [guid]::NewGuid())
try {
    Set-Content -LiteralPath $notBitstream -Value 'invalid' -Encoding ASCII
    Invoke-ExpectedFailure {
        & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $programmer `
            -Bitstream $notBitstream `
            -ExpectedSha256 ('0' * 64) -Hs2Serial TEST-SERIAL
    } 'Non-bitstream input was not rejected before programming.'

    Invoke-ExpectedFailure {
        & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $installer `
            -Target 'UNKNOWN' -DescribeTarget
    } 'Unknown USB target was not rejected.'

    Invoke-ExpectedFailure {
        & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $installer `
            -Target HS2 -DryRun
    } 'HS2 installer accepted selection without ExpectedSerial.'

    Invoke-ExpectedFailure {
        & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $installer `
            -Target FT601 -DryRun
    } 'FT601 installer accepted selection without ExpectedSerial.'
} finally {
    Remove-Item -LiteralPath $notBitstream -Force -ErrorAction SilentlyContinue
}

$installerText = Get-Content -Raw -Encoding UTF8 -LiteralPath $installer
$allInstallerText = Get-Content -Raw -Encoding UTF8 -LiteralPath $allInstaller
$restorerText = Get-Content -Raw -Encoding UTF8 -LiteralPath $restorer
$verifierText = Get-Content -Raw -Encoding UTF8 -LiteralPath $verifier
$programmerText = Get-Content -Raw -Encoding UTF8 -LiteralPath $programmer
if ($installerText -notmatch '-not \$DryRun -and -not \(Test-Administrator\)' -or
    $installerText -notmatch 'ExpectedSerial is required for \$normalizedTarget') {
    throw 'Installer DryRun/serial safety contract mismatch.'
}
if ($allInstallerText -notmatch "schema = 'lockstep-winusb-install-all-v1'") {
    throw 'Combined single-UAC installer contract mismatch.'
}
if ($allInstallerText -notmatch "'partial_failure'" -or
    $allInstallerText -notmatch 'lockstep-winusb-result-' -or
    $allInstallerText -notmatch 'Get-Content -Raw -Encoding UTF8 -LiteralPath \$resultFile' -or
    $allInstallerText -notmatch 'WriteAllText\(\$temporaryResult' -or
    $restorerText -notmatch '/delete-driver \$InstalledInf /uninstall /force' -or
    $restorerText -notmatch 'did not return to service') {
    throw 'Partial-failure recovery contract mismatch.'
}
if ($verifierText -notmatch '\[switch\]\$DriverOnly' -or
    $verifierText -notmatch 'ProductExe is required' -or
    $verifierText -notmatch 'OpenOcdExe and OpenOcdScripts are required') {
    throw 'Verifier default functional-check contract mismatch.'
}
if ($programmerText -notmatch 'Mandatory = \$true.*ExpectedSha256' -or
    $programmerText -notmatch 'Mandatory = \$true.*Hs2Serial') {
    throw 'Programmer hash/serial mandatory contract mismatch.'
}
if (Test-Path -LiteralPath $builder -PathType Leaf) {
    $builderText = Get-Content -Raw -Encoding UTF8 -LiteralPath $builder
    foreach ($hash in @(
        '746547aaf927cae44c75512d763941805928427f4ba4df3dbb40c3f7f561821e',
        '39a8be2a8c628c2a6146a3a1a85758a5f7fe44045fe425c9bf5897a11ea1b46c',
        '98996227972cdbebc13abccbdae440314739fd628aaa7dda520a8350ff421d2c',
        'cb16c8e27517117e0bf5079371f49b322fdf0f3896fac831ae10f99d113fa017',
        '4b270591b4d920b70b814b04a382507f32f487d47eecd1ad3c7d9f7d449a8e28',
        'ae845f7c3de21bb4ded414135b8348482be6048e68f86557390d1f4df2969e27'
    )) {
        if ($builderText -notmatch $hash) { throw "Builder is missing pinned supply-chain hash $hash" }
    }
}

Write-Output 'PASS windows_usb_bootstrap_test'
