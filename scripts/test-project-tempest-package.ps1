[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "build"))
$sessionRoot = Join-Path $buildRoot ("package-contract-test/" + [guid]::NewGuid().ToString("N"))
$runtimeDirectory = Join-Path $sessionRoot "runtime"
$firstOutput = Join-Path $sessionRoot "first"
$secondOutput = Join-Path $sessionRoot "second"
$contractPath = Join-Path $repositoryRoot "ProjectTempest/package-contract.json"
$packageScript = Join-Path $PSScriptRoot "package-project-tempest-demo.ps1"
$contract = Get-Content -LiteralPath $contractPath -Raw | ConvertFrom-Json
$provenancePath = Join-Path $repositoryRoot "ProjectTempest/asset-provenance.json"
$provenance = Get-Content -LiteralPath $provenancePath -Raw | ConvertFrom-Json
$executableEntry = @($contract.runtime_files | Where-Object { $_.name -eq "ProjectTempestDemo.exe" })
$milesEntry = @($contract.runtime_files | Where-Object { $_.name -eq "mss32.dll" })
if ($contract.schema_version -ne 3 -or
    $executableEntry.Count -ne 1 -or
    [string]$executableEntry[0].hash_verification -ne "two_isolated_integrated_release_builds_byte_identical" -or
    [string]$executableEntry[0].source_binding -ne "clean_build_revision_and_reviewed_head_and_governed_integrated_release_output" -or
    $milesEntry.Count -ne 1 -or
    [string]$milesEntry[0].hash_verification -ne "two_isolated_integrated_release_builds_byte_identical") {
    throw "The package contract does not govern exactly one source-bound executable and one integrated-build Miles dependency."
}
$milesDependency = @(
    $provenance.runtime_dependencies |
        Where-Object { [string]$_.dependency_id -eq [string]$milesEntry[0].provenance_id }
)
if ($milesDependency.Count -ne 1 -or
    [string]$milesDependency[0].toolchain_family -ne "Microsoft Visual C++ 2022 x86" -or
    @($milesDependency[0].deterministic_compile_options) -notcontains "/Brepro" -or
    @($milesDependency[0].deterministic_link_options) -notcontains "/Brepro" -or
    @($milesDependency[0].deterministic_link_options) -notcontains "/PDBALTPATH:%_PDB%" -or
    [string]$milesDependency[0].verification_policy -ne [string]$milesEntry[0].hash_verification) {
    throw "Miles source, build procedure, package contract, and runtime-dependency provenance do not agree."
}
$provenanceDirectory = Split-Path -Parent $provenancePath
$milesSourcePath = [IO.Path]::GetFullPath((Join-Path $provenanceDirectory ([string]$milesDependency[0].source_record)))
$milesFetchPath = [IO.Path]::GetFullPath((Join-Path $provenanceDirectory ([string]$milesDependency[0].fetch_definition)))
$repositoryPrefix = $repositoryRoot.TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar
) + [IO.Path]::DirectorySeparatorChar
if (-not $milesSourcePath.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    -not $milesFetchPath.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $milesSourcePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $milesFetchPath -PathType Leaf)) {
    throw "Miles provenance paths must resolve to repository files."
}
$milesSourceText = Get-Content -LiteralPath $milesSourcePath -Raw
$milesFetchText = Get-Content -LiteralPath $milesFetchPath -Raw
$milesTargetCreationIndex = $milesFetchText.IndexOf("FetchContent_MakeAvailable(miles)", [StringComparison]::Ordinal)
$milesCompileOptionsIndex = $milesFetchText.IndexOf("target_compile_options(milesstub", [StringComparison]::Ordinal)
$milesLinkOptionsIndex = $milesFetchText.IndexOf("target_link_options(milesstub", [StringComparison]::Ordinal)
if ($milesSourceText -notmatch "(?im)^Pinned commit:\s+$([regex]::Escape([string]$milesDependency[0].source_commit))\s*$" -or
    $milesSourceText -notmatch "(?im)^Binary verification:\s+two isolated integrated Release builds must produce byte-identical mss32\.dll files" -or
    $milesFetchText -notmatch "(?im)^\s*GIT_REPOSITORY\s+$([regex]::Escape([string]$milesDependency[0].source_repository))\s*$" -or
    $milesFetchText -notmatch "(?im)^\s*GIT_TAG\s+$([regex]::Escape([string]$milesDependency[0].source_commit))\s*$" -or
    $milesFetchText -notmatch '(?m)^if\(MSVC AND MSVC_VERSION GREATER_EQUAL 1900 AND TARGET milesstub\)\s*$' -or
    $milesTargetCreationIndex -lt 0 -or
    $milesCompileOptionsIndex -le $milesTargetCreationIndex -or
    $milesLinkOptionsIndex -le $milesTargetCreationIndex -or
    $milesFetchText -notmatch '(?s)target_compile_options\(milesstub.+?\$<\$<CONFIG:Release>:/Brepro>' -or
    $milesFetchText -notmatch '(?s)target_link_options\(milesstub.+?\$<\$<CONFIG:Release>:/Brepro>.+?\$<\$<CONFIG:Release>:/PDBALTPATH:%_PDB%>') {
    throw "Miles provenance paths and deterministic target settings are not authoritative for the fetched source."
}

$workflowPath = Join-Path $repositoryRoot ".github/workflows/build-toolchain.yml"
$workflowText = Get-Content -LiteralPath $workflowPath -Raw
$packageScriptText = Get-Content -LiteralPath $packageScript -Raw
$checkoutParentHistory = [regex]::Matches($workflowText, '(?ms)^\s*- name: Checkout Code\s+uses: actions/checkout@[^\r\n]+\s+with:\s+(?:#[^\r\n]*\s+)*fetch-depth: 2\s*$').Count
$primaryRuntimePolicyIndex = $packageScriptText.IndexOf('build/win32/ProjectTempest/Release', [StringComparison]::Ordinal)
$repeatRuntimePolicyIndex = $packageScriptText.IndexOf('build/win32-tempest-repro/ProjectTempest/Release', [StringComparison]::Ordinal)
if ($checkoutParentHistory -ne 1 -or
    $primaryRuntimePolicyIndex -lt 0 -or
    $repeatRuntimePolicyIndex -lt 0 -or
    $packageScriptText -notmatch 'Production packaging is restricted to the two governed integrated Release runtime directories' -or
    $packageScriptText -notmatch 'Production packaging rejects a reparse-point runtime directory' -or
    $packageScriptText -notmatch 'Runtime package inputs may not be reparse points' -or
    $packageScriptText -notmatch 'rev-list --parents -n 1' -or
    $packageScriptText -notmatch 'reviewed_source_revision' -or
    $packageScriptText -notmatch 'must be the clean build revision or one of its direct parents') {
    throw "Production packaging must reject arbitrary or stale runtime directories."
}
$executableComparisonIndex = $workflowText.IndexOf('if ($primaryExecutableHash -ne $repeatExecutableHash)', [StringComparison]::Ordinal)
$milesComparisonIndex = $workflowText.IndexOf('if ($primaryMilesHash -ne $repeatMilesHash)', [StringComparison]::Ordinal)
$firstPackageIndex = $workflowText.IndexOf('./scripts/package-project-tempest-demo.ps1', [StringComparison]::Ordinal)
$reviewedSourceArguments = [regex]::Matches($workflowText, '(?m)^\s+-ReviewedSourceRevision \$reviewedSourceRevision\b').Count
$expectedExecutableHashArguments = [regex]::Matches($workflowText, '(?m)^\s+-ExpectedExecutableSha256 \$primaryExecutableHash\b').Count
$expectedHashArguments = [regex]::Matches($workflowText, '(?m)^\s+-ExpectedMilesStubSha256 \$primaryMilesHash\s*$').Count
$cacheClearIndex = $workflowText.IndexOf('name: Clear cached Miles outputs for Project Tempest reproducibility', [StringComparison]::Ordinal)
$configureIndex = $workflowText.IndexOf('name: Configure ${{ inputs.game }} with CMake', [StringComparison]::Ordinal)
$ciExcludeIndex = $workflowText.IndexOf('$ciOwnedExcludes = @("/vcpkg/", "/vcpkg-bincache/")', [StringComparison]::Ordinal)
$sourceStatusIndex = $workflowText.IndexOf('$sourceStatus = @(git status --porcelain=v1 --untracked-files=all)', [StringComparison]::Ordinal)
$artifactMoveIndex = $workflowText.IndexOf('$files | Move-Item -Destination $artifactsDir', [StringComparison]::Ordinal)
$artifactGateIndex = $workflowText.IndexOf('./scripts/assert-project-tempest-artifact-boundary.ps1', [StringComparison]::Ordinal)
$artifactGateCalls = [regex]::Matches($workflowText, '(?m)^\s*\./scripts/assert-project-tempest-artifact-boundary\.ps1\s+`$').Count
$artifactUploadIndex = $workflowText.IndexOf('- name: Upload ${{ inputs.game }}', [StringComparison]::Ordinal)
if ($executableComparisonIndex -lt 0 -or
    $milesComparisonIndex -le $executableComparisonIndex -or
    $firstPackageIndex -le $milesComparisonIndex -or
    $reviewedSourceArguments -ne 2 -or
    $expectedExecutableHashArguments -ne 2 -or
    $expectedHashArguments -ne 2 -or
    $cacheClearIndex -lt 0 -or
    $configureIndex -le $cacheClearIndex -or
    $workflowText -notmatch '(?s)name: Clear cached Miles outputs for Project Tempest reproducibility.+?if: \$\{\{ inputs\.game == ''Generals'' && inputs\.preset == ''win32'' \}\}.+?milesBuild\.StartsWith\(\$allowedPrefix.+?resolvedMilesBuild\.StartsWith\(\$allowedPrefix.+?Remove-Item -LiteralPath \$resolvedMilesBuild -Recurse -Force' -or
    $ciExcludeIndex -lt 0 -or
    $sourceStatusIndex -le $ciExcludeIndex -or
    $workflowText -match '\$artifactSources \+= \$tempestDir' -or
    $workflowText -match '\$looseTempestPayloads\s*=' -or
    $artifactMoveIndex -lt 0 -or
    $artifactGateIndex -le $artifactMoveIndex -or
    $artifactGateCalls -ne 1 -or
    $artifactUploadIndex -le $artifactGateIndex -or
    $workflowText -notmatch '(?m)^\s+-ArtifactDirectory \$artifactsDir\.FullName\s+`$' -or
    $workflowText -notmatch '(?m)^\s+-ExpectedPrivatePackageCount \$expectedPrivatePackageCount\s*$' -or
    $workflowText -notmatch 'expectedPrivatePackageCount') {
    throw "CI must safely isolate its build roots and compare two integrated executables and Miles DLLs before both packages consume the proven hashes."
}

function Get-ZipEntryBytes {
    param(
        [Parameter(Mandatory = $true)]
        [IO.Compression.ZipArchive]$Archive,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )
    $entry = $Archive.GetEntry($Name)
    if ($null -eq $entry) {
        throw "Package archive is missing '$Name'."
    }
    $stream = $entry.Open()
    $memory = [IO.MemoryStream]::new()
    try {
        $stream.CopyTo($memory)
        return $memory.ToArray()
    }
    finally {
        $memory.Dispose()
        $stream.Dispose()
    }
}

function New-TestPe32GuiBytes {
    param([byte]$Marker = 0x31)

    $bytes = [byte[]]::new(512)
    [BitConverter]::GetBytes([uint16]0x5A4D).CopyTo($bytes, 0)
    [BitConverter]::GetBytes([int32]0x80).CopyTo($bytes, 0x3C)
    [BitConverter]::GetBytes([uint32]0x00004550).CopyTo($bytes, 0x80)
    [BitConverter]::GetBytes([uint16]0x014C).CopyTo($bytes, 0x84)
    [BitConverter]::GetBytes([uint16]1).CopyTo($bytes, 0x86)
    [BitConverter]::GetBytes([uint16]0x00E0).CopyTo($bytes, 0x94)
    [BitConverter]::GetBytes([uint16]0x0102).CopyTo($bytes, 0x96)
    [BitConverter]::GetBytes([uint16]0x010B).CopyTo($bytes, 0x98)
    [BitConverter]::GetBytes([uint16]2).CopyTo($bytes, 0xDC)
    $bytes[0x120] = $Marker
    return ,$bytes
}

function New-ArtifactBoundaryFixture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [switch]$IncludePrivatePackage,
        [string]$LoosePayloadName = ""
    )

    $directory = Join-Path $sessionRoot ("artifact-boundary/" + $Name)
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    foreach ($ordinaryName in @("generalsv.exe", "generalsv.pdb", "mss32.dll")) {
        [IO.File]::WriteAllBytes((Join-Path $directory $ordinaryName), [byte[]](0x47, 0x45, 0x4E))
    }
    if ($IncludePrivatePackage) {
        [IO.File]::WriteAllBytes(
            (Join-Path $directory "ProjectTempestDemo-private.zip"),
            [byte[]](0x50, 0x4B, 0x05, 0x06)
        )
    }
    if ($LoosePayloadName.Length -gt 0) {
        [IO.File]::WriteAllBytes((Join-Path $directory $LoosePayloadName), [byte[]](0x50, 0x54))
    }
    return $directory
}

try {
    $cleanArtifactDirectory = New-ArtifactBoundaryFixture -Name "clean-governing" -IncludePrivatePackage
    .\scripts\assert-project-tempest-artifact-boundary.ps1 `
        -ArtifactDirectory $cleanArtifactDirectory `
        -ExpectedPrivatePackageCount 1

    $cleanNonGoverningDirectory = New-ArtifactBoundaryFixture -Name "clean-non-governing"
    .\scripts\assert-project-tempest-artifact-boundary.ps1 `
        -ArtifactDirectory $cleanNonGoverningDirectory `
        -ExpectedPrivatePackageCount 0

    $unexpectedPackageDirectory = New-ArtifactBoundaryFixture -Name "unexpected-non-governing-package" -IncludePrivatePackage
    $caught = $false
    try {
        .\scripts\assert-project-tempest-artifact-boundary.ps1 `
            -ArtifactDirectory $unexpectedPackageDirectory `
            -ExpectedPrivatePackageCount 0
    }
    catch {
        $caught = $_.Exception.Message -match "exactly 0 governed Project Tempest private package"
    }
    if (-not $caught) {
        throw "The shared artifact boundary accepted a private package in a non-governing job."
    }

    $looseArtifactFixtures = @(
        "ProjectTempestDemo.exe",
        "ProjectTempestDemo.pdb",
        "ProjectTempestDemo.dll",
        "ProjectTempestDemo-helper.dll",
        "project_tempest_headless_acceptance.exe",
        "project_tempest_headless_acceptance.pdb",
        "project_tempest_runtime.dll",
        "courier.w3d",
        "pt_alert.wav",
        "EA-Tunable-Colorblindness-NOTICE.txt"
    )
    for ($fixtureIndex = 0; $fixtureIndex -lt $looseArtifactFixtures.Count; $fixtureIndex++) {
        $looseName = $looseArtifactFixtures[$fixtureIndex]
        $looseDirectory = New-ArtifactBoundaryFixture `
            -Name ("loose-" + $fixtureIndex) `
            -IncludePrivatePackage `
            -LoosePayloadName $looseName
        $caught = $false
        try {
            .\scripts\assert-project-tempest-artifact-boundary.ps1 `
                -ArtifactDirectory $looseDirectory `
                -ExpectedPrivatePackageCount 1
        }
        catch {
            $caught = $_.Exception.Message -match "Loose Project Tempest payloads" -and
                $_.Exception.Message -match [regex]::Escape($looseName)
        }
        if (-not $caught) {
            throw "The shared artifact boundary accepted loose payload '$looseName'."
        }
    }

    New-Item -ItemType Directory -Path $runtimeDirectory -Force | Out-Null
    foreach ($entry in $contract.runtime_files) {
        $destination = Join-Path $runtimeDirectory ([string]$entry.name)
        if ($entry.kind -eq "asset") {
            Copy-Item -LiteralPath (Join-Path $repositoryRoot ([string]$entry.repository_path)) `
                -Destination $destination
        }
        elseif ($entry.kind -eq "third_party_notice") {
            $noticeName = ([string]$entry.name) -replace '^EA-Tunable-Colorblindness-', ''
            Copy-Item -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/ThirdParty/ElectronicArtsTunableColorblindness/$noticeName") `
                -Destination $destination
        }
        elseif ($entry.kind -eq "headless_evidence") {
            $fixtureAcceptance = [ordered]@{
                schema_version = 1
                mode = "headless_deterministic_acceptance"
                manual_playthrough_claimed = $false
                fresh_launches = 3
                scenarios = @(
                    [ordered]@{ outcome = "victory"; ticks = $entry.victory_ticks; final_checksum = $entry.victory_final_checksum; trace_checksum = $entry.victory_trace_checksum; territory_capture = $true; construction = $true; production = $true; faction_abilities = $true; result_flow = $true; restart_flow = $true },
                    [ordered]@{ outcome = "defeat"; ticks = $entry.defeat_ticks; final_checksum = $entry.defeat_final_checksum; trace_checksum = $entry.defeat_trace_checksum; result_flow = $true; restart_flow = $true },
                    [ordered]@{ outcome = "victory"; ticks = $entry.victory_ticks; final_checksum = $entry.victory_final_checksum; trace_checksum = $entry.victory_trace_checksum; territory_capture = $true; construction = $true; production = $true; faction_abilities = $true; result_flow = $true; restart_flow = $true }
                )
            }
            $fixtureJson = ($fixtureAcceptance | ConvertTo-Json -Depth 6) -replace "`r`n", "`n"
            [IO.File]::WriteAllText($destination, $fixtureJson + "`n", [Text.UTF8Encoding]::new($false))
        }
        else {
            $bytes = if ($entry.kind -eq "executable") {
                New-TestPe32GuiBytes
            }
            else {
                [byte[]](0x4D, 0x53, 0x53, 0x53, 0x54, 0x55, 0x42)
            }
            [IO.File]::WriteAllBytes($destination, $bytes)
        }
    }

    $revision = "0123456789abcdef0123456789abcdef01234567"
    $epoch = 1760000000
    $fixtureExecutablePath = Join-Path $runtimeDirectory "ProjectTempestDemo.exe"
    $fixtureExecutableHash = (Get-FileHash -LiteralPath $fixtureExecutablePath -Algorithm SHA256).Hash
    $fixtureDependencyHash = (Get-FileHash -LiteralPath (Join-Path $runtimeDirectory "mss32.dll") -Algorithm SHA256).Hash

    $arbitraryProductionOutput = Join-Path $sessionRoot "arbitrary-production-runtime"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $arbitraryProductionOutput `
            -ExpectedExecutableSha256 $fixtureExecutableHash `
            -ExpectedMilesStubSha256 $fixtureDependencyHash
    }
    catch {
        $caught = $_.Exception.Message -match "restricted to the two governed integrated Release runtime directories"
    }
    if (-not $caught -or (Test-Path -LiteralPath $arbitraryProductionOutput)) {
        throw "The production package gate accepted an arbitrary or stale runtime directory."
    }

    $missingReviewedSourceOutput = Join-Path $sessionRoot "missing-reviewed-source"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $missingReviewedSourceOutput `
            -SourceRevision $revision `
            -SourceDateEpoch $epoch `
            -TestFixture `
            -ExpectedExecutableSha256 $fixtureExecutableHash `
            -ExpectedMilesStubSha256 $fixtureDependencyHash
    }
    catch {
        $caught = $_.Exception.Message -match "ReviewedSourceRevision"
    }
    if (-not $caught -or (Test-Path -LiteralPath $missingReviewedSourceOutput)) {
        throw "The package gate allowed the reviewed source identity to be omitted."
    }

    $missingExecutableHashOutput = Join-Path $sessionRoot "missing-executable-hash"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $missingExecutableHashOutput `
            -SourceRevision $revision `
            -ReviewedSourceRevision $revision `
            -SourceDateEpoch $epoch `
            -TestFixture `
            -ExpectedMilesStubSha256 $fixtureDependencyHash
    }
    catch {
        $caught = $_.Exception.Message -match "ExpectedExecutableSha256"
    }
    if (-not $caught -or (Test-Path -LiteralPath $missingExecutableHashOutput)) {
        throw "The package gate allowed a caller to bypass independent executable hash verification."
    }

    $missingHashOutput = Join-Path $sessionRoot "missing-dependency-hash"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $missingHashOutput `
            -SourceRevision $revision `
            -ReviewedSourceRevision $revision `
            -SourceDateEpoch $epoch `
            -TestFixture `
            -ExpectedExecutableSha256 $fixtureExecutableHash
    }
    catch {
        $caught = $_.Exception.Message -match "ExpectedMilesStubSha256"
    }
    if (-not $caught -or (Test-Path -LiteralPath $missingHashOutput)) {
        throw "The package gate allowed a caller to bypass independent Miles hash verification."
    }

    & $packageScript `
        -RuntimeDirectory $runtimeDirectory `
        -OutputDirectory $firstOutput `
        -SourceRevision $revision `
        -ReviewedSourceRevision $revision `
        -SourceDateEpoch $epoch `
        -TestFixture `
        -ExpectedExecutableSha256 $fixtureExecutableHash `
        -ExpectedMilesStubSha256 $fixtureDependencyHash
    & $packageScript `
        -RuntimeDirectory $runtimeDirectory `
        -OutputDirectory $secondOutput `
        -SourceRevision $revision `
        -ReviewedSourceRevision $revision `
        -SourceDateEpoch $epoch `
        -TestFixture `
        -ExpectedExecutableSha256 $fixtureExecutableHash `
        -ExpectedMilesStubSha256 $fixtureDependencyHash

    $firstArchive = Join-Path $firstOutput ([string]$contract.archive_name)
    $secondArchive = Join-Path $secondOutput ([string]$contract.archive_name)
    $firstHash = (Get-FileHash -LiteralPath $firstArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    $secondHash = (Get-FileHash -LiteralPath $secondArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($firstHash -ne $secondHash) {
        throw "Private demo package is not reproducible: first=$firstHash second=$secondHash."
    }

    $archive = [IO.Compression.ZipFile]::OpenRead($firstArchive)
    try {
        $prefix = "$($contract.package_directory)/"
        $entryNames = @($archive.Entries | ForEach-Object { $_.FullName })
        foreach ($entry in @($contract.runtime_files) + @($contract.repository_files)) {
            $expectedName = "$prefix$($entry.name)"
            if ($expectedName -notin $entryNames) {
                throw "Private package is missing governed entry '$expectedName'."
            }
        }
        foreach ($requiredMetadata in @("package-manifest.json", "SHA256SUMS.txt")) {
            if ("$prefix$requiredMetadata" -notin $entryNames) {
                throw "Private package is missing metadata '$requiredMetadata'."
            }
        }
        foreach ($pattern in $contract.forbidden_patterns) {
            if ($entryNames | Where-Object { [IO.Path]::GetFileName($_) -like $pattern }) {
                throw "Private package contains forbidden content matching '$pattern'."
            }
        }

        $utf8 = [Text.UTF8Encoding]::new($false)
        $manifestText = $utf8.GetString((Get-ZipEntryBytes -Archive $archive -Name "${prefix}package-manifest.json"))
        $manifest = $manifestText | ConvertFrom-Json
        if ($manifest.schema_version -ne 2 -or
            $manifest.source_revision -ne $revision -or
            $manifest.reviewed_source_revision -ne $revision -or
            $manifest.source_date_epoch -ne $epoch -or
            $manifest.distribution -ne "test_fixture" -or
            $manifest.source_tree -ne "fixture" -or
            $manifest.package_contract_sha256 -ne (Get-FileHash -LiteralPath $contractPath -Algorithm SHA256).Hash.ToLowerInvariant() -or
            $manifest.asset_provenance_sha256 -ne (Get-FileHash -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/asset-provenance.json") -Algorithm SHA256).Hash.ToLowerInvariant() -or
            $manifest.executable_verification.name -ne "ProjectTempestDemo.exe" -or
            $manifest.executable_verification.sha256 -ne $fixtureExecutableHash.ToLowerInvariant() -or
            $manifest.executable_verification.policy -ne [string]$executableEntry[0].hash_verification -or
            $manifest.executable_verification.source_binding -ne [string]$executableEntry[0].source_binding -or
            $manifest.executable_verification.source_revision -ne $revision -or
            $manifest.executable_verification.reviewed_source_revision -ne $revision -or
            $manifest.executable_verification.runtime_input_policy -ne "restricted_test_fixture" -or
            $manifest.runtime_dependency_verification.name -ne "mss32.dll" -or
            $manifest.runtime_dependency_verification.sha256 -ne $fixtureDependencyHash.ToLowerInvariant() -or
            $manifest.runtime_dependency_verification.provenance_id -ne [string]$milesEntry[0].provenance_id -or
            $manifest.runtime_dependency_verification.policy -ne [string]$milesEntry[0].hash_verification -or
            $manifest.renderer_execution -ne "not_performed" -or
            $manifest.manual_playthrough_claimed -ne $false) {
            throw "Private package manifest does not preserve the governed source/evidence state."
        }

        $manifestExecutable = @($manifest.files | Where-Object { $_.name -eq "ProjectTempestDemo.exe" })
        if ($manifestExecutable.Count -ne 1 -or
            $manifestExecutable[0].sha256 -ne $fixtureExecutableHash.ToLowerInvariant()) {
            throw "Private package manifest does not bind the exact proven executable hash to the reviewed source revision."
        }

        $manifestMiles = @($manifest.files | Where-Object { $_.name -eq "mss32.dll" })
        if ($manifestMiles.Count -ne 1 -or
            $manifestMiles[0].sha256 -ne $fixtureDependencyHash.ToLowerInvariant() -or
            $manifestMiles[0].provenance_asset_id -ne [string]$milesEntry[0].provenance_id) {
            throw "Private package manifest does not bind the exact proven Miles hash to its source provenance."
        }

        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            foreach ($file in $manifest.files) {
                $bytes = Get-ZipEntryBytes -Archive $archive -Name "$prefix$($file.name)"
                $actualHash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant()
                if ($actualHash -ne $file.sha256 -or $bytes.LongLength -ne $file.length) {
                    throw "Manifest verification failed for '$($file.name)'."
                }
            }
        }
        finally {
            $sha.Dispose()
        }
    }
    finally {
        $archive.Dispose()
    }

    $forbiddenFixture = Join-Path $runtimeDirectory "AudioZH.big"
    [IO.File]::WriteAllBytes($forbiddenFixture, [byte[]](0x45, 0x41))
    $rejectedOutput = Join-Path $sessionRoot "rejected"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $rejectedOutput `
            -SourceRevision $revision `
            -ReviewedSourceRevision $revision `
            -SourceDateEpoch $epoch `
            -TestFixture `
            -ExpectedExecutableSha256 $fixtureExecutableHash `
            -ExpectedMilesStubSha256 $fixtureDependencyHash
    }
    catch {
        $caught = $_.Exception.Message -match "forbidden"
    }
    if (-not $caught -or (Test-Path -LiteralPath $rejectedOutput)) {
        throw "The package gate did not reject a retail BIG archive before staging output."
    }

    Remove-Item -LiteralPath $forbiddenFixture -Force
    $dependencyMismatchOutput = Join-Path $sessionRoot "dependency-mismatch"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $dependencyMismatchOutput `
            -SourceRevision $revision `
            -ReviewedSourceRevision $revision `
            -SourceDateEpoch $epoch `
            -TestFixture `
            -ExpectedExecutableSha256 $fixtureExecutableHash `
            -ExpectedMilesStubSha256 ("0" * 64)
    }
    catch {
        $caught = $_.Exception.Message -match "independently proven integrated-build hash"
    }
    if (-not $caught -or (Test-Path -LiteralPath $dependencyMismatchOutput)) {
        throw "The package gate did not reject a runtime dependency hash mismatch before staging output."
    }

    $executablePath = $fixtureExecutablePath
    [IO.File]::WriteAllBytes($executablePath, [byte[]](0x4D, 0x5A, 0x4E, 0x4F, 0x54, 0x50, 0x45))
    $invalidExecutableOutput = Join-Path $sessionRoot "invalid-executable"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $invalidExecutableOutput `
            -SourceRevision $revision `
            -ReviewedSourceRevision $revision `
            -SourceDateEpoch $epoch `
            -TestFixture `
            -ExpectedExecutableSha256 $fixtureExecutableHash `
            -ExpectedMilesStubSha256 $fixtureDependencyHash
    }
    catch {
        $caught = $_.Exception.Message -match "not a valid PE32 x86 GUI image"
    }
    if (-not $caught -or (Test-Path -LiteralPath $invalidExecutableOutput)) {
        throw "The package gate did not reject a malformed MZ-prefixed release executable before staging output."
    }

    [IO.File]::WriteAllBytes($executablePath, (New-TestPe32GuiBytes -Marker 0x72))
    $forgedExecutableOutput = Join-Path $sessionRoot "forged-executable"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $forgedExecutableOutput `
            -SourceRevision $revision `
            -ReviewedSourceRevision $revision `
            -SourceDateEpoch $epoch `
            -TestFixture `
            -ExpectedExecutableSha256 $fixtureExecutableHash `
            -ExpectedMilesStubSha256 $fixtureDependencyHash
    }
    catch {
        $caught = $_.Exception.Message -match "independently proven integrated-build hash"
    }
    if (-not $caught -or (Test-Path -LiteralPath $forgedExecutableOutput)) {
        throw "The package gate accepted a structurally valid old or forged executable that was not produced by the proven builds."
    }

    [IO.File]::WriteAllBytes($executablePath, (New-TestPe32GuiBytes))
    $acceptancePath = Join-Path $runtimeDirectory "headless-acceptance.json"
    $invalidAcceptance = Get-Content -LiteralPath $acceptancePath -Raw | ConvertFrom-Json
    $invalidAcceptance.manual_playthrough_claimed = $true
    [IO.File]::WriteAllText(
        $acceptancePath,
        (($invalidAcceptance | ConvertTo-Json -Depth 6) -replace "`r`n", "`n") + "`n",
        [Text.UTF8Encoding]::new($false)
    )
    $invalidAcceptanceOutput = Join-Path $sessionRoot "invalid-acceptance"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $invalidAcceptanceOutput `
            -SourceRevision $revision `
            -ReviewedSourceRevision $revision `
            -SourceDateEpoch $epoch `
            -TestFixture `
            -ExpectedExecutableSha256 $fixtureExecutableHash `
            -ExpectedMilesStubSha256 $fixtureDependencyHash
    }
    catch {
        $caught = $_.Exception.Message -match "does not prove three deterministic"
    }
    if (-not $caught -or (Test-Path -LiteralPath $invalidAcceptanceOutput)) {
        throw "The package gate did not reject invalid headless acceptance evidence before staging output."
    }

    Write-Host "PASS: Project Tempest package contract and reproducibility"
    Write-Host "Fixture archive SHA256: $firstHash"
}
finally {
    if (Test-Path -LiteralPath $sessionRoot) {
        $resolvedSessionRoot = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $sessionRoot).Path)
        $buildPrefix = $buildRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) +
            [IO.Path]::DirectorySeparatorChar
        if (-not $resolvedSessionRoot.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean package test output outside '$buildRoot': '$resolvedSessionRoot'."
        }
        Remove-Item -LiteralPath $resolvedSessionRoot -Recurse -Force
    }
}
