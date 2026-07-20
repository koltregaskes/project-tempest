[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RuntimeDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [string]$ExecutableName = "ProjectTempestDemo.exe",

    [string]$SourceRevision = "",

    [long]$SourceDateEpoch = 0,

    [switch]$TestFixture,

    [string]$TestFixtureRuntimeDependencySha256 = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "build"))
$resolvedRuntimeDirectory = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$resolvedOutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$buildPrefix = $buildRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) +
    [IO.Path]::DirectorySeparatorChar

if (-not $resolvedOutputDirectory.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Package output must remain under the repository build directory '$buildRoot': '$resolvedOutputDirectory'."
}

if ($ExecutableName -notmatch '^ProjectTempestDemo\.exe$') {
    throw "Only the governed release executable name ProjectTempestDemo.exe can be packaged."
}

$sourceTreeState = "clean"
if ($TestFixture) {
    $fixturePrefix = [IO.Path]::GetFullPath((Join-Path $buildRoot "package-contract-test")).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    $runtimeFixturePath = [IO.Path]::GetFullPath($resolvedRuntimeDirectory)
    if (-not $runtimeFixturePath.StartsWith($fixturePrefix, [StringComparison]::OrdinalIgnoreCase) -or
        -not $resolvedOutputDirectory.StartsWith($fixturePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "TestFixture is restricted to inputs and outputs under '$fixturePrefix'."
    }
    if ($SourceRevision -notmatch '^[0-9a-fA-F]{40}$' -or
        $SourceDateEpoch -eq 0 -or
        $TestFixtureRuntimeDependencySha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw "TestFixture requires an explicit SourceRevision, SourceDateEpoch, and runtime-dependency SHA-256."
    }
    $sourceTreeState = "fixture"
}
else {
    if ($TestFixtureRuntimeDependencySha256) {
        throw "TestFixtureRuntimeDependencySha256 is valid only with TestFixture."
    }
    $headRevision = (git -C $repositoryRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $headRevision -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Could not determine the repository HEAD revision from Git."
    }
    $headRevision = $headRevision.ToLowerInvariant()

    $sourceStatus = @(git -C $repositoryRoot status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) {
        throw "Could not verify whether the repository source tree is clean."
    }
    if ($sourceStatus.Count -gt 0) {
        throw "Refusing to package a dirty source tree. Commit the exact reviewed source first: $($sourceStatus -join ', ')."
    }

    if ($SourceRevision -and $SourceRevision.ToLowerInvariant() -ne $headRevision) {
        throw "SourceRevision must match the clean repository HEAD '$headRevision'."
    }
    $SourceRevision = $headRevision

    $commitEpochText = (git -C $repositoryRoot show -s --format=%ct $SourceRevision).Trim()
    $commitEpoch = 0L
    if ($LASTEXITCODE -ne 0 -or -not [long]::TryParse($commitEpochText, [ref]$commitEpoch)) {
        throw "Could not determine a source timestamp for revision '$SourceRevision'."
    }
    if ($SourceDateEpoch -ne 0 -and $SourceDateEpoch -ne $commitEpoch) {
        throw "SourceDateEpoch must match the clean source commit timestamp '$commitEpoch'."
    }
    $SourceDateEpoch = $commitEpoch
}
$SourceRevision = $SourceRevision.ToLowerInvariant()

if ($SourceDateEpoch -lt 315532800) {
    throw "SourceDateEpoch must be on or after 1980-01-01 for portable ZIP timestamps."
}
$packageTimestamp = [DateTimeOffset]::FromUnixTimeSeconds($SourceDateEpoch)

$contractPath = Join-Path $repositoryRoot "ProjectTempest/package-contract.json"
$contract = Get-Content -LiteralPath $contractPath -Raw | ConvertFrom-Json
if ($contract.schema_version -ne 1) {
    throw "Unsupported Project Tempest package contract schema '$($contract.schema_version)'."
}

foreach ($pattern in $contract.forbidden_patterns) {
    $forbidden = @(Get-ChildItem -LiteralPath $resolvedRuntimeDirectory -File -Recurse -Filter $pattern)
    if ($forbidden.Count -gt 0) {
        $names = $forbidden | ForEach-Object { $_.FullName }
        throw "Retail or interactive content matching '$pattern' is forbidden in the package input: $($names -join ', ')."
    }
}

foreach ($entry in $contract.runtime_files) {
    $runtimeName = if ($entry.kind -eq "executable") { $ExecutableName } else { [string]$entry.name }
    $runtimePath = Join-Path $resolvedRuntimeDirectory $runtimeName
    if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
        throw "Required runtime input is missing: '$runtimePath'."
    }
    if ($entry.kind -eq "runtime_dependency") {
        $expectedHash = if ($TestFixture) {
            $TestFixtureRuntimeDependencySha256.ToLowerInvariant()
        }
        else {
            ([string]$entry.sha256).ToLowerInvariant()
        }
        if ($expectedHash -notmatch '^[0-9a-f]{64}$') {
            throw "Runtime dependency '$runtimeName' has no valid governed SHA-256 in the package contract."
        }
        $runtimeHash = (Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($runtimeHash -ne $expectedHash) {
            throw "Runtime dependency '$runtimeName' does not match the governed source build: actual=$runtimeHash expected=$expectedHash."
        }
    }
}

if (Test-Path -LiteralPath $resolvedOutputDirectory) {
    $verifiedExistingOutput = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $resolvedOutputDirectory).Path)
    if (-not $verifiedExistingOutput.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean package output outside '$buildRoot': '$verifiedExistingOutput'."
    }
    Remove-Item -LiteralPath $verifiedExistingOutput -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedOutputDirectory -Force | Out-Null

$stageDirectory = Join-Path $resolvedOutputDirectory $contract.package_directory
New-Item -ItemType Directory -Path $stageDirectory -Force | Out-Null

$provenancePath = Join-Path $repositoryRoot "ProjectTempest/asset-provenance.json"
$provenance = Get-Content -LiteralPath $provenancePath -Raw | ConvertFrom-Json
$provenanceByLeaf = @{}
foreach ($asset in $provenance.assets) {
    if (-not $asset.path -or -not $asset.sha256) {
        continue
    }
    $leaf = [IO.Path]::GetFileName([string]$asset.path).ToLowerInvariant()
    if ($provenanceByLeaf.ContainsKey($leaf)) {
        throw "Asset provenance contains a duplicate runtime leaf name '$leaf'."
    }
    $provenanceByLeaf[$leaf] = $asset
}

$manifestFiles = [Collections.Generic.List[object]]::new()

function Add-PackageFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourcePath,
        [Parameter(Mandatory = $true)]
        [string]$PackageName,
        [Parameter(Mandatory = $true)]
        [string]$Kind,
        [string]$ProvenanceAssetId = ""
    )

    if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
        throw "Required package input is missing: '$SourcePath'."
    }
    if ([IO.Path]::GetFileName($PackageName) -ne $PackageName) {
        throw "Package entries must be flat file names: '$PackageName'."
    }
    $destination = Join-Path $stageDirectory $PackageName
    Copy-Item -LiteralPath $SourcePath -Destination $destination -Force
    [IO.File]::SetLastWriteTimeUtc($destination, $packageTimestamp.UtcDateTime)
    $file = Get-Item -LiteralPath $destination
    $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifestFiles.Add([ordered]@{
        name = $PackageName
        kind = $Kind
        length = $file.Length
        sha256 = $hash
        provenance_asset_id = $ProvenanceAssetId
    })
}

foreach ($entry in $contract.runtime_files) {
    $runtimeName = if ($entry.kind -eq "executable") { $ExecutableName } else { [string]$entry.name }
    $runtimePath = Join-Path $resolvedRuntimeDirectory $runtimeName
    $provenanceAssetId = ""
    if ($entry.kind -eq "asset") {
        $key = $runtimeName.ToLowerInvariant()
        if (-not $provenanceByLeaf.ContainsKey($key)) {
            throw "Runtime asset '$runtimeName' has no asset-provenance entry."
        }
        $asset = $provenanceByLeaf[$key]
        if ($asset.distribution -ne "internal_development_only") {
            throw "Private demo asset '$runtimeName' has unexpected distribution state '$($asset.distribution)'."
        }
        if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
            throw "Required runtime asset is missing: '$runtimePath'."
        }
        $runtimeHash = (Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($runtimeHash -ne ([string]$asset.sha256).ToLowerInvariant()) {
            throw "Runtime asset '$runtimeName' does not match provenance: actual=$runtimeHash expected=$($asset.sha256)."
        }
        $provenanceAssetId = [string]$asset.asset_id
    }
    Add-PackageFile -SourcePath $runtimePath -PackageName ([string]$entry.name) -Kind ([string]$entry.kind) `
        -ProvenanceAssetId $provenanceAssetId
}

foreach ($entry in $contract.repository_files) {
    Add-PackageFile `
        -SourcePath (Join-Path $repositoryRoot ([string]$entry.path)) `
        -PackageName ([string]$entry.name) `
        -Kind ([string]$entry.kind)
}

$orderedManifestFiles = @($manifestFiles | Sort-Object { $_.name.ToLowerInvariant() })
$manifest = [ordered]@{
    schema_version = 1
    package = [string]$contract.package_directory
    distribution = "private_internal_demo"
    source_repository = "https://github.com/koltregaskes/project-tempest"
    source_revision = $SourceRevision
    source_date_epoch = $SourceDateEpoch
    source_tree = $sourceTreeState
    renderer_execution = "not_performed"
    manual_playthrough_claimed = $false
    files = $orderedManifestFiles
}

$utf8NoBom = [Text.UTF8Encoding]::new($false)
$manifestJson = ($manifest | ConvertTo-Json -Depth 8) -replace "`r`n", "`n"
$manifestJson += "`n"
$manifestPath = Join-Path $stageDirectory "package-manifest.json"
[IO.File]::WriteAllText($manifestPath, $manifestJson, $utf8NoBom)
[IO.File]::SetLastWriteTimeUtc($manifestPath, $packageTimestamp.UtcDateTime)

$hashLines = [Collections.Generic.List[string]]::new()
foreach ($file in @(Get-ChildItem -LiteralPath $stageDirectory -File | Sort-Object Name)) {
    if ($file.Name -eq "SHA256SUMS.txt") {
        continue
    }
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $hashLines.Add("$hash  $($file.Name)")
}
$hashPath = Join-Path $stageDirectory "SHA256SUMS.txt"
[IO.File]::WriteAllText($hashPath, ($hashLines -join "`n") + "`n", $utf8NoBom)
[IO.File]::SetLastWriteTimeUtc($hashPath, $packageTimestamp.UtcDateTime)

$archivePath = Join-Path $resolvedOutputDirectory ([string]$contract.archive_name)
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}
$archive = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in @(Get-ChildItem -LiteralPath $stageDirectory -File | Sort-Object Name)) {
        $entryName = "$($contract.package_directory)/$($file.Name)"
        $zipEntry = $archive.CreateEntry($entryName, [IO.Compression.CompressionLevel]::Optimal)
        $zipEntry.LastWriteTime = $packageTimestamp
        $input = [IO.File]::OpenRead($file.FullName)
        $output = $zipEntry.Open()
        try {
            $input.CopyTo($output)
        }
        finally {
            $output.Dispose()
            $input.Dispose()
        }
    }
}
finally {
    $archive.Dispose()
}

$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "PASS: Project Tempest private package created"
Write-Host "Archive: $archivePath"
Write-Host "SHA256: $archiveHash"
