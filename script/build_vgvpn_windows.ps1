[CmdletBinding()]
param(
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$')][string]$Version = '0.1.0',
    [ValidateRange(1, 32)][int]$Jobs = 6,
    [switch]$PackageOnly,
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$localBuild = Join-Path $repo '.vg-build'
$deps = Join-Path $localBuild 'deps'
$runtime = Join-Path $localBuild 'runtime'
$build = Join-Path $localBuild 'msvc'
foreach ($directory in @($deps, $runtime, $build)) { New-Item -ItemType Directory -Force $directory | Out-Null }

function Get-VerifiedAsset([string]$Url, [string]$Path, [string]$Sha256) {
    if (-not (Test-Path -LiteralPath $Path)) { Invoke-WebRequest -Uri $Url -OutFile $Path }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ine $Sha256) {
        throw "Dependency checksum does not match: $([IO.Path]::GetFileName($Path))"
    }
}

if (-not $PackageOnly) {
    $qtArchive = Join-Path $deps 'qt.7z'
    Get-VerifiedAsset 'https://github.com/throneproj/buildqt/releases/download/Qt_6.11.2/Qt_6.11.2_x64.7z' $qtArchive '57b235b0c6b08ceb79ea516137e3c8aa5cc9ff8f0225fe03ae6ef25deb699273'
    $qt = Join-Path $deps 'Qt'
    if (-not (Test-Path -LiteralPath (Join-Path $qt 'lib/cmake/Qt6/Qt6Config.cmake'))) {
        $sevenZip = Get-Command 7z -ErrorAction SilentlyContinue
        if (-not $sevenZip) { throw '7-Zip is required to extract the pinned Qt SDK.' }
        & $sevenZip.Source x $qtArchive "-o$deps" -y -bso0 -bsp0
        if ($LASTEXITCODE -ne 0) { throw 'Qt SDK extraction failed.' }
    }
    $sslArchive = Join-Path $deps 'openssl.7z'
    Get-VerifiedAsset 'https://github.com/throneproj/env_windows_legacy/releases/download/latest/openssl_x64.7z' $sslArchive '0cc10ca8bd88a08f1f4275edf8c3f285c5988803e693d4dc8a12a85a9e544412'
    $ssl = Join-Path $deps 'openssl'
    if (-not (Test-Path -LiteralPath (Join-Path $ssl 'include/openssl/ssl.h'))) {
        $sevenZip = Get-Command 7z -ErrorAction Stop
        & $sevenZip.Source x $sslArchive "-o$deps" -y -bso0 -bsp0
        if ($LASTEXITCODE -ne 0) { throw 'OpenSSL extraction failed.' }
    }
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ Build Tools 2022 are required.' }
    $vsRoot = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
    if (-not $vsRoot) { throw 'The MSVC x64 toolchain is missing.' }
    $developerCommand = Join-Path $vsRoot 'Common7/Tools/VsDevCmd.bat'
    $environmentCommand = '"' + $developerCommand + '" -no_logo -arch=x64 -host_arch=x64 >nul && set'
    foreach ($line in (& $env:ComSpec /d /s /c $environmentCommand)) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    $cmake = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    $ninja = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
    if (-not (Test-Path -LiteralPath $cmake) -or -not (Test-Path -LiteralPath $ninja)) { throw 'Install the CMake tools component of Visual Studio Build Tools.' }
    # Ninja must recognize localized /showIncludes lines to track changed sources.
    $probe = Join-Path $localBuild 'include-prefix.cpp'
    Set-Content -LiteralPath $probe -Value '#include <stddef.h>' -Encoding ascii
    $probeObject = Join-Path $localBuild 'include-prefix.obj'
    $probeLines = & cl.exe /nologo /showIncludes /c "/Fo$probeObject" $probe 2>&1
    if ($LASTEXITCODE -ne 0) { throw 'MSVC dependency-prefix probe failed.' }
    $includePrefix = $null
    foreach ($probeLine in $probeLines) {
        if ([string]$probeLine -match '^(.+?)([A-Za-z]:[\\/].*)$') {
            $includePrefix = $Matches[1].TrimEnd() + ' '
            break
        }
    }
    if (-not $includePrefix) { throw 'Cannot detect the localized MSVC include prefix.' }
    Copy-Item -LiteralPath (Join-Path $repo 'res/vgvpn/srslist.h') -Destination (Join-Path $build 'srslist.h')
    & $cmake -S $repo -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" '-DCMAKE_C_COMPILER=cl.exe' '-DCMAKE_CXX_COMPILER=cl.exe' '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_PREFIX_PATH=$qt/lib/cmake" "-DOPENSSL_ROOT_DIR=$ssl" "-DINPUT_VERSION=$Version" "-DVG_MSVC_SHOWINCLUDES_PREFIX=$includePrefix" '-DNKR_PACKAGE=ON'
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    if ($Clean) { & $cmake --build $build --clean-first --parallel $Jobs }
    else { & $cmake --build $build --parallel $Jobs }
    if ($LASTEXITCODE -ne 0) { throw 'Windows compilation failed.' }
    & (Join-Path $PSScriptRoot 'build_vgvpn_core.ps1')
}

$upstreamZip = Join-Path $runtime 'upstream.zip'
Get-VerifiedAsset 'https://github.com/throneproj/Throne/releases/download/1.3.2/Throne-1.3.2-windows64.zip' $upstreamZip '8792f4303212a5fb2e7509d87f3cf969638d3939d8c99d288da25ec97fe362fc'
$upstream = Join-Path $runtime 'upstream/Throne'
if (-not (Test-Path -LiteralPath (Join-Path $upstream 'ThroneCore.exe'))) {
    Expand-Archive -LiteralPath $upstreamZip -DestinationPath (Join-Path $runtime 'upstream')
}
$dist = Join-Path $repo "dist/VG-VPN-$Version-windows64"
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'VGVPN.exe') -Destination $dist
# The core is rebuilt because it verifies the exact parent GUI executable name.
Copy-Item -LiteralPath (Join-Path $localBuild 'core/ThroneCore.exe') -Destination $dist
Copy-Item -LiteralPath (Join-Path $upstream 'libcronet.dll') -Destination $dist
foreach ($name in @('LICENSE', 'README.md', 'README.upstream.md', 'THIRD_PARTY_NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $dist
}
$allowedFiles = @('VGVPN.exe', 'ThroneCore.exe', 'libcronet.dll', 'LICENSE', 'README.md', 'README.upstream.md', 'THIRD_PARTY_NOTICES.md', 'build-manifest.json')
foreach ($entry in Get-ChildItem -LiteralPath $dist -Force) {
    if ($entry.PSIsContainer -or $entry.Name -notin $allowedFiles) {
        throw 'The distributable directory contains an unexpected file; use a fresh output directory.'
    }
}
$manifest = [ordered]@{
    product = 'VG VPN'; version = $Version; platform = 'windows-x64'; upstream = 'Throne 1.3.2'
    sourceCommit = (& git -C $repo rev-parse HEAD).Trim()
    sourceModified = [bool](& git -C $repo status --porcelain --untracked-files=normal)
    files = @{}
}
Get-ChildItem -LiteralPath $dist -File | ForEach-Object { $manifest.files[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $dist 'build-manifest.json') -Encoding utf8
$zip = Join-Path $repo "dist/VG-VPN-$Version-windows64.zip"
Compress-Archive -LiteralPath $dist -DestinationPath $zip -Force
Write-Output "Built: $zip"
