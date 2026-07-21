[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ArtifactDirectory,

    [Parameter(Mandatory = $true)]
    [ValidateRange(0, 1)]
    [int]$ExpectedPrivatePackageCount
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$contractPath = Join-Path $repositoryRoot "ProjectTempest/package-contract.json"
if (-not (Test-Path -LiteralPath $contractPath -PathType Leaf)) {
    throw "Project Tempest package contract is unavailable: '$contractPath'."
}
if (-not (Test-Path -LiteralPath $ArtifactDirectory -PathType Container)) {
    throw "Artifact directory is unavailable: '$ArtifactDirectory'."
}

$resolvedArtifactDirectory = (Resolve-Path -LiteralPath $ArtifactDirectory).Path
$artifactDirectoryItem = Get-Item -LiteralPath $resolvedArtifactDirectory -Force
if (($artifactDirectoryItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Project Tempest artifact boundary rejects a reparse-point artifact directory."
}

$contract = Get-Content -LiteralPath $contractPath -Raw | ConvertFrom-Json
if ($contract.schema_version -ne 3) {
    throw "Project Tempest package contract schema is not supported by the artifact boundary."
}

$privatePackageName = "ProjectTempestDemo-private.zip"
$governedTempestNames = @(
    $contract.runtime_files |
        ForEach-Object { [string]$_.name } |
        Where-Object { $_ -ne "mss32.dll" }
)

# Walk the exact recursive tree consumed by upload-artifact without following
# reparse-point directories. Inspect every child before it can be queued.
$pendingDirectories = [Collections.Generic.Queue[string]]::new()
$artifactFileList = [Collections.Generic.List[IO.FileInfo]]::new()
$pendingDirectories.Enqueue($resolvedArtifactDirectory)
while ($pendingDirectories.Count -gt 0) {
    $currentDirectory = $pendingDirectories.Dequeue()
    foreach ($child in @(Get-ChildItem -LiteralPath $currentDirectory -Force)) {
        if (($child.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            $kind = if ($child.PSIsContainer) { "directory" } else { "file" }
            throw "Project Tempest artifact boundary rejects reparse-point $kind '$($child.FullName)'."
        }
        if ($child.PSIsContainer) {
            $pendingDirectories.Enqueue($child.FullName)
        }
        else {
            $artifactFileList.Add([IO.FileInfo]$child)
        }
    }
}
$artifactFiles = @($artifactFileList)

$looseTempestPayloads = @(
    $artifactFiles |
        Where-Object {
            $_.Name -ne $privatePackageName -and (
                $_.Name -in $governedTempestNames -or
                $_.Name -match '^(?i:ProjectTempestDemo.*|project_tempest_.+)\.(exe|dll|pdb)$'
            )
        }
)
if ($looseTempestPayloads.Count -gt 0) {
    throw "Loose Project Tempest payloads escaped the governed private ZIP: $($looseTempestPayloads.Name -join ', ')."
}

$privatePackages = @(
    $artifactFiles | Where-Object { $_.Name -eq $privatePackageName }
)
$nestedPrivatePackages = @(
    $privatePackages |
        Where-Object { $_.DirectoryName -ine $resolvedArtifactDirectory }
)
if ($nestedPrivatePackages.Count -gt 0) {
    throw "The governed Project Tempest private ZIP must be staged at the artifact root, not nested: $($nestedPrivatePackages.FullName -join ', ')."
}
if ($privatePackages.Count -ne $ExpectedPrivatePackageCount) {
    throw "The uploaded artifact must contain exactly $ExpectedPrivatePackageCount governed Project Tempest private package(s); found $($privatePackages.Count)."
}

Write-Host "PASS: Project Tempest outer artifact boundary (private packages=$ExpectedPrivatePackageCount, loose payloads=0)"
