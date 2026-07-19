# /**********************************************************
# * 文件名: build_ft601_libusbk_bundle.ps1
# * 日期: 2026-07-19
# * 版本: 1.0
# * 更新记录: 新建 FT601 libusbK Windows 分发包构建脚本。
# * 描述: 校验固定开源输入并生成带许可、源码和哈希的 ZIP。
# **********************************************************/

param([string]$Version = '1.0')

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptRoot '..\..\..\..')).Path
$taskRaw = Join-Path $repoRoot '.codex-tasks\20260719-board-full-flow-validation\raw'
$taskInstaller = Join-Path $repoRoot '.codex-tasks\20260719-board-full-flow-validation\install_ft601_libusbk.ps1'
$productRuntime = Join-Path $repoRoot 'software\lockstep_host\build-codex-trace\src\apps\libusb-1.0.dll'
$distRoot = Join-Path $repoRoot 'software\lockstep_host\dist'
$bundleName = "lockstep-ft601-libusbk-windows-$Version"
$bundleRoot = Join-Path $distRoot $bundleName
$zipPath = Join-Path $distRoot "$bundleName.zip"

$inputs = [ordered]@{
    zadig = Join-Path $taskRaw 'zadig-2.9.exe'
    zadig_ini = Join-Path $taskRaw 'zadig.ini'
    libusbk = Join-Path $taskRaw 'libusbK-3.1.0.0-bin.7z'
    source = Join-Path $taskRaw 'libwdi-v1.5.1.zip'
    copying = Join-Path $taskRaw 'libwdi-source\libwdi-1.5.1\COPYING'
    copying_lgpl = Join-Path $taskRaw 'libwdi-source\libwdi-1.5.1\COPYING-LGPL'
    installer = $taskInstaller
    runtime = $productRuntime
}
foreach ($entry in $inputs.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
        throw "Missing bundle input $($entry.Key): $($entry.Value)"
    }
}

$expectedHashes = @{
    zadig = '4ECAA95DF3DA3621486A043AEF8B3050B8BAFE7C901402871E816229EF82039B'
    libusbk = '38605D8D5A86F408A4B7BEC60F6D4A096050EEE72F89A63A8D5BE125252D3FE7'
}
foreach ($name in $expectedHashes.Keys) {
    $actual = (Get-FileHash -LiteralPath $inputs[$name] -Algorithm SHA256).Hash
    if ($actual -ne $expectedHashes[$name]) {
        throw "Unexpected $name SHA256: $actual"
    }
}
$signature = Get-AuthenticodeSignature -LiteralPath $inputs.zadig
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Akeo Consulting') {
    throw "Zadig signature verification failed: $($signature.Status)"
}
if ((Test-Path -LiteralPath $bundleRoot) -or (Test-Path -LiteralPath $zipPath)) {
    throw "Bundle output already exists: $bundleRoot or $zipPath"
}

foreach ($directory in @(
    $bundleRoot,
    (Join-Path $bundleRoot 'raw'),
    (Join-Path $bundleRoot 'runtime'),
    (Join-Path $bundleRoot 'sources'),
    (Join-Path $bundleRoot 'references'),
    (Join-Path $bundleRoot 'licenses')
)) {
    [void](New-Item -ItemType Directory -Path $directory)
}

Copy-Item -LiteralPath $inputs.installer -Destination (Join-Path $bundleRoot 'install_ft601_libusbk.ps1')
Copy-Item -LiteralPath (Join-Path $scriptRoot 'verify_ft601_libusbk.ps1') -Destination $bundleRoot
Copy-Item -LiteralPath (Join-Path $scriptRoot 'FT601_LIBUSBK_DISTRIBUTION.md') -Destination (Join-Path $bundleRoot 'README.md')
Copy-Item -LiteralPath $inputs.zadig -Destination (Join-Path $bundleRoot 'raw\zadig-2.9.exe')
Copy-Item -LiteralPath $inputs.zadig_ini -Destination (Join-Path $bundleRoot 'raw\zadig.ini')
Copy-Item -LiteralPath $inputs.runtime -Destination (Join-Path $bundleRoot 'runtime\libusb-1.0.dll')
Copy-Item -LiteralPath $inputs.source -Destination (Join-Path $bundleRoot 'sources\libwdi-v1.5.1.zip')
Copy-Item -LiteralPath $inputs.libusbk -Destination (Join-Path $bundleRoot 'references\libusbK-3.1.0.0-bin.7z')
Copy-Item -LiteralPath $inputs.copying -Destination (Join-Path $bundleRoot 'licenses\COPYING')
Copy-Item -LiteralPath $inputs.copying_lgpl -Destination (Join-Path $bundleRoot 'licenses\COPYING-LGPL')

$checksumPath = Join-Path $bundleRoot 'SHA256SUMS'
$files = Get-ChildItem -LiteralPath $bundleRoot -Recurse -File | Where-Object {
    $_.FullName -ne $checksumPath
} | Sort-Object FullName
$lines = foreach ($file in $files) {
    $relative = $file.FullName.Substring($bundleRoot.Length + 1).Replace('\', '/')
    '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
}
[IO.File]::WriteAllLines($checksumPath, $lines, [Text.Encoding]::ASCII)

Compress-Archive -LiteralPath $bundleRoot -DestinationPath $zipPath -CompressionLevel Optimal
$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output "BUNDLE_PATH=$zipPath"
Write-Output "BUNDLE_SHA256=$zipHash"
