# /**********************************************************
# * 文件名: build_lockstep_winusb_bundle.ps1
# * 日期: 2026-07-20
# * 版本: 1.1
# * 更新记录: 锁定全部供应链输入并加入完整 OpenOCD 便携运行时。
# * 描述: 校验开源输入、许可和脚本后生成带 SHA256SUMS 的 ZIP。
# **********************************************************/

param(
    [string]$Version = '2.0',
    [string]$ZadigPath = 'D:\tool\zadig-2.9.exe',
    [string]$OpenOcdRoot = 'D:\Program Files\msys64\ucrt64'
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptRoot '..\..\..\..')).Path
$taskRaw = Join-Path $repoRoot '.codex-tasks\20260719-board-full-flow-validation\raw'
$runtime = Join-Path $repoRoot 'software\lockstep_host\build-codex-event\src\apps\libusb-1.0.dll'
$openOcdBin = Join-Path $OpenOcdRoot 'bin'
$openOcdShare = Join-Path $OpenOcdRoot 'share'
$openOcdSourceRoot = Join-Path $taskRaw 'openocd-sources'

$inputs = [ordered]@{
    zadig = $ZadigPath
    source = Join-Path $taskRaw 'libwdi-v1.5.1.zip'
    copying = Join-Path $taskRaw 'libwdi-source\libwdi-1.5.1\COPYING'
    copying_lgpl = Join-Path $taskRaw 'libwdi-source\libwdi-1.5.1\COPYING-LGPL'
    runtime = $runtime
    openocd = Join-Path $openOcdBin 'openocd.exe'
    libhidapi = Join-Path $openOcdBin 'libhidapi-0.dll'
    libjaylink = Join-Path $openOcdBin 'libjaylink-0.dll'
    libftdi = Join-Path $openOcdBin 'libftdi1.dll'
    openocd_libusb = Join-Path $openOcdBin 'libusb-1.0.dll'
    libcapstone = Join-Path $openOcdBin 'libcapstone.dll'
    source_openocd = Join-Path $openOcdSourceRoot 'mingw-w64-openocd-0.12.0-4.src.tar.zst'
    source_libftdi = Join-Path $openOcdSourceRoot 'mingw-w64-libftdi-1.5-13.src.tar.zst'
    source_libusb = Join-Path $openOcdSourceRoot 'mingw-w64-libusb-1.0.28-1.src.tar.zst'
    source_hidapi = Join-Path $openOcdSourceRoot 'mingw-w64-hidapi-0.15.0-1.src.tar.zst'
    source_libjaylink = Join-Path $openOcdSourceRoot 'mingw-w64-libjaylink-0.4.0-2.src.tar.zst'
    source_capstone = Join-Path $openOcdSourceRoot 'mingw-w64-capstone-5.0.7-1.src.tar.zst'
}
foreach ($entry in $inputs.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
        throw "Missing bundle input $($entry.Key): $($entry.Value)"
    }
}
function Assert-FileHash([string]$name, [string]$path, [string]$expected) {
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw "Unexpected $name SHA256: expected=$expected actual=$actual" }
}
$expectedHashes = [ordered]@{
    zadig = '4ecaa95df3da3621486a043aef8b3050b8bafe7c901402871e816229ef82039b'
    source = '746547aaf927cae44c75512d763941805928427f4ba4df3dbb40c3f7f561821e'
    copying = '8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903'
    copying_lgpl = 'da7eabb7bafdf7d3ae5e9f223aa5bdc1eece45ac569dc21b3b037520b4464768'
    runtime = '39a8be2a8c628c2a6146a3a1a85758a5f7fe44045fe425c9bf5897a11ea1b46c'
    openocd = '98996227972cdbebc13abccbdae440314739fd628aaa7dda520a8350ff421d2c'
    libhidapi = '416947126ed934364497e6dfb917c9f199ed2203af9f0c1b064bc583296f93c9'
    libjaylink = 'af4b1db79926b9554bcdb57d52f32fe9aaa6a5021987a2192c9502587643c639'
    libftdi = '087f308f805714588fa6ea1dd3914f0ef0a042af4e750b9ba086a315100f7668'
    openocd_libusb = '39a8be2a8c628c2a6146a3a1a85758a5f7fe44045fe425c9bf5897a11ea1b46c'
    libcapstone = '39d18a222d77ffcb2a158264f4f137f182dcf7bf86f8cbafb3f0bf9b18f873b5'
    source_openocd = '4b270591b4d920b70b814b04a382507f32f487d47eecd1ad3c7d9f7d449a8e28'
    source_libftdi = 'e99dbedcc9c777a66f90a758e7cea1c67aba06078109334d78c49a09fc264eea'
    source_libusb = '948681b702e19cb0f90a4ced1a60a286514f9f5cbb825886e6623134b5eced05'
    source_hidapi = '1883773cdc3e96ee1b2e65b7a0bfa48cd942d5bbeb456880f52f0328b581921a'
    source_libjaylink = '86c9ddd0f9f887e4125a34c97f8e850b909117c79bc229d87f81176a858bd619'
    source_capstone = 'ae845f7c3de21bb4ded414135b8348482be6048e68f86557390d1f4df2969e27'
}
foreach ($entry in $expectedHashes.GetEnumerator()) {
    Assert-FileHash $entry.Key $inputs[$entry.Key] $entry.Value
}
$signature = Get-AuthenticodeSignature -LiteralPath $inputs.zadig
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Akeo Consulting') {
    throw "Zadig signature verification failed: $($signature.Status)"
}

$openOcdScripts = Join-Path $openOcdShare 'openocd\scripts'
if (-not (Test-Path -LiteralPath $openOcdScripts -PathType Container)) {
    throw "Missing OpenOCD scripts: $openOcdScripts"
}
$scriptManifest = Get-ChildItem -LiteralPath $openOcdScripts -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($openOcdScripts.Length + 1).Replace('\', '/')
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
}
$manifestBytes = [Text.Encoding]::UTF8.GetBytes(($scriptManifest -join "`n") + "`n")
$manifestHasher = [Security.Cryptography.SHA256]::Create()
$scriptTreeHash = ([BitConverter]::ToString($manifestHasher.ComputeHash($manifestBytes))).Replace('-', '').ToLowerInvariant()
if ($scriptTreeHash -ne 'cb16c8e27517117e0bf5079371f49b322fdf0f3896fac831ae10f99d113fa017') {
    throw "Unexpected OpenOCD scripts tree SHA256: $scriptTreeHash"
}

$licenseHashes = [ordered]@{
    'openocd\COPYING' = '1b8f7e37ee5afbbf95c2a4d62b12b25232e29538692663b434318503a9a88419'
    'libftdi\COPYING-CMAKE-SCRIPTS' = '46cde7dc11e64c78d650b4851b88f6704b4665ff60f22a1caf68ceb15e217e5b'
    'libftdi\COPYING.GPL' = 'ab15fd526bd8dd18a9e77ebc139656bf4d33e97fc7238cd11bf60e2b9b8666c6'
    'libftdi\COPYING.LIB' = 'b7993225104d90ddd8024fd838faf300bea5e83d91203eab98e29512acebd69c'
    'libusb\COPYING' = '5df07007198989c622f5d41de8d703e7bef3d0e79d62e24332ee739a452af62a'
    'hidapi\LICENSE-bsd.txt' = '30eb1bef29b46f8ba7ab8b416035dbd93cb034a45481dd97815b944284582cd2'
    'hidapi\LICENSE-gpl3.txt' = '8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903'
    'hidapi\LICENSE-orig.txt' = 'fb5436aa63d1b71a8dfbf74ecaf1a5b4e1ec4df7f80074d11fec99284f69ca5f'
    'hidapi\LICENSE.txt' = '7d3b087c34f35d4d538e3bcddd1ff8f66e92f9ef336881999482800ddf840913'
    'libjaylink\COPYING' = '8177f97513213526df2cf6184d8ff986c675afb514d4e68a404010521b880643'
    'capstone\LICENSE.TXT' = '65e9ed46a59976eda8f5bd1ea79a680dea38dd299c760bc9a8d87a764ef5029b'
}
foreach ($entry in $licenseHashes.GetEnumerator()) {
    Assert-FileHash "license/$($entry.Key)" (Join-Path (Join-Path $openOcdShare 'licenses') $entry.Key) $entry.Value
}

$distRoot = Join-Path $repoRoot 'software\lockstep_host\dist'
$bundleName = "lockstep-winusb-windows-$Version"
$bundleRoot = Join-Path $distRoot $bundleName
$zipPath = Join-Path $distRoot "$bundleName.zip"
if ((Test-Path -LiteralPath $bundleRoot) -or (Test-Path -LiteralPath $zipPath)) {
    throw "Bundle output already exists: $bundleRoot or $zipPath"
}
foreach ($directory in @($bundleRoot, (Join-Path $bundleRoot 'raw'),
    (Join-Path $bundleRoot 'runtime'), (Join-Path $bundleRoot 'sources'),
    (Join-Path $bundleRoot 'licenses'), (Join-Path $bundleRoot 'openocd'),
    (Join-Path $bundleRoot 'openocd\bin'), (Join-Path $bundleRoot 'openocd\share\openocd'))) {
    [void](New-Item -ItemType Directory -Path $directory)
}

$files = @(
    'install_lockstep_winusb.ps1', 'install_lockstep_all_winusb.ps1',
    'restore_lockstep_usb_driver.ps1', 'verify_lockstep_winusb.ps1',
    'program_zcu102_openocd.ps1', 'windows_usb_bootstrap_test.ps1', 'zadig.ini'
)
foreach ($file in $files) {
    Copy-Item -LiteralPath (Join-Path $scriptRoot $file) -Destination $bundleRoot
}
Copy-Item -LiteralPath (Join-Path $scriptRoot 'LOCKSTEP_WINUSB_DISTRIBUTION.md') `
    -Destination (Join-Path $bundleRoot 'README.md')
Copy-Item -LiteralPath (Join-Path $scriptRoot 'openocd\lockstep-zcu102-hs2.cfg') `
    -Destination (Join-Path $bundleRoot 'openocd\lockstep-zcu102-hs2.cfg')
Copy-Item -LiteralPath $inputs.zadig -Destination (Join-Path $bundleRoot 'raw\zadig-2.9.exe')
Copy-Item -LiteralPath $inputs.runtime -Destination (Join-Path $bundleRoot 'runtime\libusb-1.0.dll')
Copy-Item -LiteralPath $inputs.source -Destination (Join-Path $bundleRoot 'sources\libwdi-v1.5.1.zip')
Copy-Item -LiteralPath $inputs.copying -Destination (Join-Path $bundleRoot 'licenses\COPYING')
Copy-Item -LiteralPath $inputs.copying_lgpl -Destination (Join-Path $bundleRoot 'licenses\COPYING-LGPL')
foreach ($name in @('source_openocd', 'source_libftdi', 'source_libusb',
    'source_hidapi', 'source_libjaylink', 'source_capstone')) {
    Copy-Item -LiteralPath $inputs[$name] -Destination (Join-Path $bundleRoot 'sources')
}
foreach ($name in @('openocd', 'libhidapi', 'libjaylink', 'libftdi', 'openocd_libusb', 'libcapstone')) {
    Copy-Item -LiteralPath $inputs[$name] -Destination (Join-Path $bundleRoot 'openocd\bin')
}
Copy-Item -LiteralPath $openOcdScripts -Destination (Join-Path $bundleRoot 'openocd\share\openocd\scripts') -Recurse
foreach ($relative in $licenseHashes.Keys) {
    $destination = Join-Path $bundleRoot (Join-Path 'licenses\openocd-runtime' $relative)
    [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination))
    Copy-Item -LiteralPath (Join-Path (Join-Path $openOcdShare 'licenses') $relative) -Destination $destination
}
$utf8NoBom = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllLines((Join-Path $bundleRoot 'openocd\SCRIPTS_SHA256SUMS'), $scriptManifest, $utf8NoBom)

$checksumPath = Join-Path $bundleRoot 'SHA256SUMS'
$payloadFiles = Get-ChildItem -LiteralPath $bundleRoot -Recurse -File | Where-Object {
    $_.FullName -ne $checksumPath
} | Sort-Object FullName
$lines = foreach ($file in $payloadFiles) {
    $relative = $file.FullName.Substring($bundleRoot.Length + 1).Replace('\', '/')
    '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
}
[IO.File]::WriteAllLines($checksumPath, $lines, $utf8NoBom)
Compress-Archive -LiteralPath $bundleRoot -DestinationPath $zipPath -CompressionLevel Optimal
$verificationRoot = Join-Path $distRoot ('.verify-' + [guid]::NewGuid())
try {
    Expand-Archive -LiteralPath $zipPath -DestinationPath $verificationRoot
    $expandedRoot = Join-Path $verificationRoot $bundleName
    $mismatches = @()
    Get-Content -Encoding UTF8 -LiteralPath (Join-Path $expandedRoot 'SHA256SUMS') | ForEach-Object {
        if ($_ -notmatch '^([0-9a-f]{64})  (.+)$') {
            $mismatches += "invalid-entry:$_"
            return
        }
        $payload = Join-Path $expandedRoot $Matches[2].Replace('/', '\')
        $payloadHash = Get-FileHash -LiteralPath $payload -Algorithm SHA256 -ErrorAction SilentlyContinue
        if ($null -eq $payloadHash -or $payloadHash.Hash.ToLowerInvariant() -ne $Matches[1]) {
            $mismatches += $Matches[2]
        }
    }
    if ($mismatches.Count -ne 0) {
        throw "Expanded bundle checksum verification failed: $($mismatches -join ', ')"
    }
    & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File `
        (Join-Path $expandedRoot 'windows_usb_bootstrap_test.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Expanded bundle bootstrap test failed.' }

    $openOcdInfo = [Diagnostics.ProcessStartInfo]::new()
    $openOcdInfo.FileName = Join-Path $expandedRoot 'openocd\bin\openocd.exe'
    $openOcdInfo.Arguments = '--version'
    $openOcdInfo.WorkingDirectory = Join-Path $expandedRoot 'openocd\bin'
    $openOcdInfo.UseShellExecute = $false
    $openOcdCheck = [Diagnostics.Process]::Start($openOcdInfo)
    $openOcdCheck.WaitForExit()
    if ($openOcdCheck.ExitCode -ne 0) { throw 'Expanded portable OpenOCD failed to start.' }
} finally {
    Remove-Item -LiteralPath $verificationRoot -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Output "BUNDLE_PATH=$zipPath"
Write-Output "BUNDLE_SHA256=$((Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant())"
