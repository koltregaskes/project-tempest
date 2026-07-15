[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ViewerDirectory,
    [string]$MilesStubPath = "build/win32/_deps/miles-build/Release/mss32.dll"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Resolve-ProjectPath {
    param([string]$Path)

    $candidate = if ([IO.Path]::IsPathRooted($Path)) {
        $Path
    } else {
        Join-Path $repositoryRoot $Path
    }
    return [IO.Path]::GetFullPath($candidate)
}

$resolvedViewerDirectory = (Resolve-Path (Resolve-ProjectPath $ViewerDirectory)).Path
$viewerPath = Join-Path $resolvedViewerDirectory "W3DViewV.exe"
if (-not (Test-Path -LiteralPath $viewerPath -PathType Leaf)) {
    throw "W3DViewV.exe was not found in '$resolvedViewerDirectory'."
}

$d3d8To9Version = "v1.15.1"
$d3d8To9Commit = "65870f2302e9c496cd6d873d6095961d5c777668"
$d3d8To9Sha256 = "ab6bf7a9a9f4b3e66a75ca038d8d10289c88acbfe8d52c3b5a8a9a259cb26cd5"
$d3d8To9Url = "https://github.com/crosire/d3d8to9/releases/download/$d3d8To9Version/d3d8.dll"
$cacheDirectory = Join-Path $env:LOCALAPPDATA "ProjectTempest/Tools/d3d8to9/$d3d8To9Version"
$cachedD3d8Path = Join-Path $cacheDirectory "d3d8.dll"

New-Item -ItemType Directory -Force -Path $cacheDirectory | Out-Null
if (-not (Test-Path -LiteralPath $cachedD3d8Path -PathType Leaf)) {
    Invoke-WebRequest -Uri $d3d8To9Url -OutFile $cachedD3d8Path
}

$actualD3d8Hash = (Get-FileHash -LiteralPath $cachedD3d8Path -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualD3d8Hash -ne $d3d8To9Sha256) {
    throw "d3d8to9 hash mismatch: expected $d3d8To9Sha256, actual $actualD3d8Hash"
}

$resolvedMilesStubPath = Resolve-ProjectPath $MilesStubPath
if (-not (Test-Path -LiteralPath $resolvedMilesStubPath -PathType Leaf)) {
    throw "The GPL Miles stub was not found at '$resolvedMilesStubPath'. Build a win32 preset or pass -MilesStubPath."
}

function Copy-DependencyIfNeeded {
    param([string]$Source, [string]$Destination)

    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
        if ($sourceHash -eq $destinationHash) {
            return
        }
    }
    Copy-Item -Force -LiteralPath $Source -Destination $Destination
}

Copy-DependencyIfNeeded $cachedD3d8Path (Join-Path $resolvedViewerDirectory "d3d8.dll")
Copy-DependencyIfNeeded $resolvedMilesStubPath (Join-Path $resolvedViewerDirectory "mss32.dll")

[pscustomobject]@{
    Viewer = $viewerPath
    D3D8To9Version = $d3d8To9Version
    D3D8To9Commit = $d3d8To9Commit
    D3D8To9License = "BSD-2-Clause"
    D3D8To9Sha256 = $actualD3d8Hash
    MilesStubCommit = "6e32700d7ba4b4713a03bf1f5ffc3b0ac8d17264"
    MilesStubLicense = "GPL-3.0"
    MilesStubSha256 = (Get-FileHash -LiteralPath $resolvedMilesStubPath -Algorithm SHA256).Hash.ToLowerInvariant()
}
