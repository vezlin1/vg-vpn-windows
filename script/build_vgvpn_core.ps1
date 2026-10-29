[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$localBuild = Join-Path $repo '.vg-build'
$deps = Join-Path $localBuild 'deps'
$tools = Join-Path $localBuild 'go-tools'
$output = Join-Path $localBuild 'core'
foreach ($directory in @($deps, $tools, $output)) { New-Item -ItemType Directory -Force $directory | Out-Null }
$go = (Get-Command go -ErrorAction Stop).Source
$archive = Join-Path $deps 'protoc.zip'
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri 'https://github.com/protocolbuffers/protobuf/releases/download/v31.1/protoc-31.1-win64.zip' -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine '70381b116ab0d71cb6a5177d9b17c7c13415866603a0fd40d513dafe32d56c35') {
    throw 'Protobuf compiler checksum mismatch.'
}
$protoc = Join-Path $deps 'protoc/bin/protoc.exe'
if (-not (Test-Path -LiteralPath $protoc)) {
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $deps 'protoc')
}
$savedEnvironment = @{}
foreach ($key in @('GOBIN', 'GOOS', 'GOARCH', 'CGO_ENABLED')) { $savedEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
try {
    $env:GOBIN = $tools
    $env:GOOS = 'windows'
    $env:GOARCH = 'amd64'
    $env:CGO_ENABLED = '0'
    if (-not (Test-Path -LiteralPath (Join-Path $tools 'protoc-gen-go.exe'))) {
        & $go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.36.12
        if ($LASTEXITCODE -ne 0) { throw 'Go protobuf plugin installation failed.' }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $tools 'protoc-gen-go-grpc.exe'))) {
        & $go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@v1.6.2
        if ($LASTEXITCODE -ne 0) { throw 'Go gRPC plugin installation failed.' }
    }
    $generated = Join-Path $repo 'core/gen'
    & $protoc "--proto_path=$generated" "--go_out=$generated" "--go-grpc_out=$generated" "--plugin=protoc-gen-go=$tools/protoc-gen-go.exe" "--plugin=protoc-gen-go-grpc=$tools/protoc-gen-go-grpc.exe" (Join-Path $generated 'libcore.proto')
    if ($LASTEXITCODE -ne 0) { throw 'Core RPC code generation failed.' }
    Push-Location (Join-Path $repo 'core')
    try {
        $singbox = (& $go list -m -f '{{.Version}}' github.com/sagernet/sing-box).Trim()
        if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the pinned sing-box version.' }
        $tags = 'with_clash_api,with_gvisor,with_quic,with_wireguard,with_utls,with_dhcp,with_tailscale,with_openvpn,with_openconnect,badlinkname,tfogo_checklinkname0,with_purego,with_naive_outbound'
        $linkFlags = "-w -s -X github.com/sagernet/sing-box/constant.Version=$singbox -X internal/godebug.defaultGODEBUG=multipathtcp=0 -checklinkname=0"
        & $go build -mod=readonly -trimpath -tags $tags -ldflags $linkFlags -o (Join-Path $output 'ThroneCore.exe') .
        if ($LASTEXITCODE -ne 0) { throw 'VG VPN networking core build failed.' }
    } finally { Pop-Location }
} finally {
    foreach ($key in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($key, $savedEnvironment[$key], 'Process') }
}
Write-Output 'Built the networking core with the VG VPN same-directory parent check.'
