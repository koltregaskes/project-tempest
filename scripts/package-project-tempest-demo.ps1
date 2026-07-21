[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RuntimeDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [string]$ExecutableName = "ProjectTempestDemo.exe",

    [string]$SourceRevision = "",

    [long]$SourceDateEpoch = 0,

    [string]$ExpectedExecutableSha256 = "",

    [string]$ExpectedMilesStubSha256 = "",

    [switch]$TestFixture
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

if ($ExpectedExecutableSha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw "ExpectedExecutableSha256 is required and must be the SHA-256 proven by two byte-identical integrated Release builds."
}
$ExpectedExecutableSha256 = $ExpectedExecutableSha256.ToLowerInvariant()

if ($ExpectedMilesStubSha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw "ExpectedMilesStubSha256 is required and must be the SHA-256 proven by two byte-identical integrated Release builds."
}
$ExpectedMilesStubSha256 = $ExpectedMilesStubSha256.ToLowerInvariant()

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
    if ($SourceRevision -notmatch '^[0-9a-fA-F]{40}$' -or $SourceDateEpoch -eq 0) {
        throw "TestFixture requires an explicit SourceRevision and SourceDateEpoch."
    }
    $sourceTreeState = "fixture"
}
else {
    $governedRuntimeDirectories = @(
        [IO.Path]::GetFullPath((Join-Path $repositoryRoot "build/win32/ProjectTempest/Release")),
        [IO.Path]::GetFullPath((Join-Path $repositoryRoot "build/win32-tempest-repro/ProjectTempest/Release"))
    )
    if (-not ($governedRuntimeDirectories | Where-Object {
        $_.Equals($resolvedRuntimeDirectory, [StringComparison]::OrdinalIgnoreCase)
    })) {
        throw "Production packaging is restricted to the two governed integrated Release runtime directories. Received '$resolvedRuntimeDirectory'."
    }
    $runtimeDirectoryItem = Get-Item -LiteralPath $resolvedRuntimeDirectory -Force
    if (($runtimeDirectoryItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Production packaging rejects a reparse-point runtime directory: '$resolvedRuntimeDirectory'."
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
if ($contract.schema_version -ne 3) {
    throw "Unsupported Project Tempest package contract schema '$($contract.schema_version)'."
}
$contractHash = (Get-FileHash -LiteralPath $contractPath -Algorithm SHA256).Hash.ToLowerInvariant()

$provenancePath = Join-Path $repositoryRoot "ProjectTempest/asset-provenance.json"
$provenance = Get-Content -LiteralPath $provenancePath -Raw | ConvertFrom-Json
$provenanceHash = (Get-FileHash -LiteralPath $provenancePath -Algorithm SHA256).Hash.ToLowerInvariant()
$executableEntries = @($contract.runtime_files | Where-Object { $_.name -eq "ProjectTempestDemo.exe" })
if ($executableEntries.Count -ne 1 -or
    [string]$executableEntries[0].kind -ne "executable" -or
    [string]$executableEntries[0].hash_verification -ne "two_isolated_integrated_release_builds_byte_identical" -or
    [string]$executableEntries[0].source_binding -ne "clean_repository_head_and_governed_integrated_release_output") {
    throw "The package contract does not bind exactly one Project Tempest executable to the reviewed source and two integrated Release builds."
}
$milesEntries = @($contract.runtime_files | Where-Object { $_.name -eq "mss32.dll" })
if ($milesEntries.Count -ne 1 -or
    [string]$milesEntries[0].hash_verification -ne "two_isolated_integrated_release_builds_byte_identical" -or
    [string]$milesEntries[0].provenance_id -notmatch '^PT-[A-Z0-9-]+$') {
    throw "The package contract does not require integrated two-build verification for one governed Miles stub."
}
$milesDependencies = @(
    $provenance.runtime_dependencies |
        Where-Object { [string]$_.dependency_id -eq [string]$milesEntries[0].provenance_id }
)
if ($milesDependencies.Count -ne 1 -or
    [string]$milesDependencies[0].name -ne "mss32.dll" -or
    [string]$milesDependencies[0].source_commit -notmatch '^[0-9a-f]{40}$' -or
    [string]$milesDependencies[0].toolchain_family -ne "Microsoft Visual C++ 2022 x86" -or
    @($milesDependencies[0].deterministic_compile_options) -notcontains "/Brepro" -or
    @($milesDependencies[0].deterministic_link_options) -notcontains "/Brepro" -or
    @($milesDependencies[0].deterministic_link_options) -notcontains "/PDBALTPATH:%_PDB%" -or
    [string]$milesDependencies[0].verification_policy -ne [string]$milesEntries[0].hash_verification) {
    throw "Miles source, build procedure, package contract, and runtime-dependency provenance do not agree."
}

function Resolve-ProvenanceFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,
        [Parameter(Mandatory = $true)]
        [string]$FieldName
    )

    if ([IO.Path]::IsPathRooted($RelativePath)) {
        throw "Miles provenance field '$FieldName' must be repository-relative: '$RelativePath'."
    }
    $provenanceDirectory = Split-Path -Parent $provenancePath
    $candidate = [IO.Path]::GetFullPath((Join-Path $provenanceDirectory $RelativePath))
    $repositoryPrefix = $repositoryRoot.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Miles provenance field '$FieldName' does not resolve to a repository file: '$RelativePath'."
    }
    return (Resolve-Path -LiteralPath $candidate).Path
}

$milesSourcePath = Resolve-ProvenanceFile `
    -RelativePath ([string]$milesDependencies[0].source_record) `
    -FieldName "source_record"
$milesFetchPath = Resolve-ProvenanceFile `
    -RelativePath ([string]$milesDependencies[0].fetch_definition) `
    -FieldName "fetch_definition"
$milesSourceText = Get-Content -LiteralPath $milesSourcePath -Raw
$milesFetchText = Get-Content -LiteralPath $milesFetchPath -Raw
if ($milesSourceText -notmatch "(?im)^Pinned commit:\s+$([regex]::Escape([string]$milesDependencies[0].source_commit))\s*$" -or
    $milesSourceText -notmatch "(?im)^Toolchain scope:\s+Microsoft Visual C\+\+ 2022 x86;" -or
    $milesSourceText -notmatch "(?im)^Binary verification:\s+two isolated integrated Release builds must produce byte-identical mss32\.dll files" -or
    $milesFetchText -notmatch "(?im)^\s*GIT_REPOSITORY\s+$([regex]::Escape([string]$milesDependencies[0].source_repository))\s*$" -or
    $milesFetchText -notmatch "(?im)^\s*GIT_TAG\s+$([regex]::Escape([string]$milesDependencies[0].source_commit))\s*$" -or
    $milesFetchText -notmatch "(?im)^\s*FetchContent_MakeAvailable\(miles\)\s*$") {
    throw "Miles source, build procedure, package contract, and runtime-dependency provenance do not agree."
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
    $runtimeItem = Get-Item -LiteralPath $runtimePath -Force
    if (($runtimeItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Runtime package inputs may not be reparse points: '$runtimePath'."
    }
    if ($entry.kind -eq "executable") {
        $stream = [IO.File]::OpenRead($runtimePath)
        $reader = [IO.BinaryReader]::new($stream)
        try {
            if ($stream.Length -lt 0x100 -or $reader.ReadUInt16() -ne 0x5A4D) {
                throw "Release executable '$runtimeName' is not a valid PE32 x86 GUI image."
            }
            $stream.Position = 0x3C
            $peOffset = $reader.ReadInt32()
            if ($peOffset -lt 0x40 -or $peOffset -gt ($stream.Length - 94)) {
                throw "Release executable '$runtimeName' is not a valid PE32 x86 GUI image."
            }
            $stream.Position = $peOffset
            $signature = $reader.ReadUInt32()
            $machine = $reader.ReadUInt16()
            $numberOfSections = $reader.ReadUInt16()
            $stream.Position = $peOffset + 20
            $optionalHeaderSize = $reader.ReadUInt16()
            $optionalHeaderOffset = $peOffset + 24
            if ($signature -ne 0x00004550 -or $machine -ne 0x014C -or $numberOfSections -lt 1 -or
                $optionalHeaderSize -lt 70 -or ($optionalHeaderOffset + $optionalHeaderSize) -gt $stream.Length) {
                throw "Release executable '$runtimeName' is not a valid PE32 x86 GUI image."
            }
            $stream.Position = $optionalHeaderOffset
            $optionalMagic = $reader.ReadUInt16()
            $stream.Position = $optionalHeaderOffset + 68
            $subsystem = $reader.ReadUInt16()
            if ($optionalMagic -ne 0x010B -or $subsystem -ne 2) {
                throw "Release executable '$runtimeName' is not a valid PE32 x86 GUI image."
            }
        }
        finally {
            $reader.Dispose()
        }
        $runtimeHash = (Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($runtimeHash -ne $ExpectedExecutableSha256) {
            throw "Release executable '$runtimeName' does not match the independently proven integrated-build hash: actual=$runtimeHash expected=$ExpectedExecutableSha256."
        }
    }
    if ($entry.kind -eq "runtime_dependency") {
        $runtimeHash = (Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($runtimeHash -ne $ExpectedMilesStubSha256) {
            throw "Runtime dependency '$runtimeName' does not match the independently proven integrated-build hash: actual=$runtimeHash expected=$ExpectedMilesStubSha256."
        }
    }
    if ($entry.kind -eq "headless_evidence") {
        $acceptance = Get-Content -LiteralPath $runtimePath -Raw | ConvertFrom-Json
        $outcomes = @($acceptance.scenarios | ForEach-Object { $_.outcome })
        $allFlowsPass = @($acceptance.scenarios | Where-Object { $_.result_flow -ne $true -or $_.restart_flow -ne $true }).Count -eq 0
        $victoryCoveragePass = @($acceptance.scenarios | Where-Object {
            $_.outcome -eq "victory" -and
            ($_.territory_capture -ne $true -or $_.construction -ne $true -or
                $_.production -ne $true -or $_.faction_abilities -ne $true)
        }).Count -eq 0
        if ($acceptance.schema_version -ne 1 -or
            $acceptance.mode -ne "headless_deterministic_acceptance" -or
            $acceptance.manual_playthrough_claimed -ne $false -or
            $acceptance.fresh_launches -ne 3 -or
            $outcomes.Count -ne 3 -or
            ($outcomes -join ",") -ne "victory,defeat,victory" -or
            -not $allFlowsPass -or
            -not $victoryCoveragePass -or
            $acceptance.scenarios[0].ticks -ne $entry.victory_ticks -or
            $acceptance.scenarios[0].final_checksum -ne $entry.victory_final_checksum -or
            $acceptance.scenarios[0].trace_checksum -ne $entry.victory_trace_checksum -or
            $acceptance.scenarios[1].ticks -ne $entry.defeat_ticks -or
            $acceptance.scenarios[1].final_checksum -ne $entry.defeat_final_checksum -or
            $acceptance.scenarios[1].trace_checksum -ne $entry.defeat_trace_checksum -or
            $acceptance.scenarios[2].ticks -ne $entry.victory_ticks -or
            $acceptance.scenarios[0].final_checksum -ne $acceptance.scenarios[2].final_checksum -or
            $acceptance.scenarios[0].trace_checksum -ne $acceptance.scenarios[2].trace_checksum) {
            throw "Headless acceptance evidence '$runtimeName' does not prove three deterministic terminal result/restart flows."
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
    elseif ($entry.kind -eq "runtime_dependency") {
        $provenanceAssetId = [string]$entry.provenance_id
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
    schema_version = 2
    package = [string]$contract.package_directory
    distribution = if ($TestFixture) { "test_fixture" } else { "private_internal_demo" }
    source_repository = "https://github.com/koltregaskes/project-tempest"
    source_revision = $SourceRevision
    source_date_epoch = $SourceDateEpoch
    source_tree = $sourceTreeState
    package_contract_sha256 = $contractHash
    asset_provenance_sha256 = $provenanceHash
    executable_verification = [ordered]@{
        name = "ProjectTempestDemo.exe"
        sha256 = $ExpectedExecutableSha256
        policy = [string]$executableEntries[0].hash_verification
        source_binding = [string]$executableEntries[0].source_binding
        source_revision = $SourceRevision
        runtime_input_policy = if ($TestFixture) { "restricted_test_fixture" } else { "governed_integrated_release_outputs_only" }
    }
    runtime_dependency_verification = [ordered]@{
        name = "mss32.dll"
        sha256 = $ExpectedMilesStubSha256
        provenance_id = [string]$milesEntries[0].provenance_id
        policy = [string]$milesEntries[0].hash_verification
    }
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
        $inputStream = [IO.File]::OpenRead($file.FullName)
        $output = $zipEntry.Open()
        try {
            $inputStream.CopyTo($output)
        }
        finally {
            $output.Dispose()
            $inputStream.Dispose()
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
