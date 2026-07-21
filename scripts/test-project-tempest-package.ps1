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
$milesEntry = @($contract.runtime_files | Where-Object { $_.name -eq "mss32.dll" })
if ($contract.schema_version -ne 2 -or
    $milesEntry.Count -ne 1 -or
    [string]$milesEntry[0].hash_verification -ne "two_isolated_integrated_release_builds_byte_identical") {
    throw "The package contract does not govern exactly one integrated-build Miles dependency."
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
$milesComparisonIndex = $workflowText.IndexOf('if ($primaryMilesHash -ne $repeatMilesHash)', [StringComparison]::Ordinal)
$firstPackageIndex = $workflowText.IndexOf('./scripts/package-project-tempest-demo.ps1', [StringComparison]::Ordinal)
$expectedHashArguments = [regex]::Matches($workflowText, '(?m)^\s+-ExpectedMilesStubSha256 \$primaryMilesHash\s*$').Count
$cacheClearIndex = $workflowText.IndexOf('name: Clear cached Miles outputs for Project Tempest reproducibility', [StringComparison]::Ordinal)
$configureIndex = $workflowText.IndexOf('name: Configure ${{ inputs.game }} with CMake', [StringComparison]::Ordinal)
$ciExcludeIndex = $workflowText.IndexOf('$ciOwnedExcludes = @("/vcpkg/", "/vcpkg-bincache/")', [StringComparison]::Ordinal)
$sourceStatusIndex = $workflowText.IndexOf('$sourceStatus = @(git status --porcelain=v1 --untracked-files=all)', [StringComparison]::Ordinal)
if ($milesComparisonIndex -lt 0 -or
    $firstPackageIndex -le $milesComparisonIndex -or
    $expectedHashArguments -ne 2 -or
    $cacheClearIndex -lt 0 -or
    $configureIndex -le $cacheClearIndex -or
    $workflowText -notmatch '(?s)name: Clear cached Miles outputs for Project Tempest reproducibility.+?if: \$\{\{ inputs\.game == ''Generals'' && inputs\.preset == ''win32'' \}\}.+?milesBuild\.StartsWith\(\$allowedPrefix.+?resolvedMilesBuild\.StartsWith\(\$allowedPrefix.+?Remove-Item -LiteralPath \$resolvedMilesBuild -Recurse -Force' -or
    $ciExcludeIndex -lt 0 -or
    $sourceStatusIndex -le $ciExcludeIndex) {
    throw "CI must safely clear its governed Miles cache, isolate dependency roots, and compare two integrated Miles DLLs before both packages consume the proven hash."
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

try {
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
                [byte[]](0x4D, 0x5A, 0x50, 0x54, 0x44, 0x45, 0x4D, 0x4F)
            }
            else {
                [byte[]](0x4D, 0x53, 0x53, 0x53, 0x54, 0x55, 0x42)
            }
            [IO.File]::WriteAllBytes($destination, $bytes)
        }
    }

    $revision = "0123456789abcdef0123456789abcdef01234567"
    $epoch = 1760000000
    $fixtureDependencyHash = (Get-FileHash -LiteralPath (Join-Path $runtimeDirectory "mss32.dll") -Algorithm SHA256).Hash

    $missingHashOutput = Join-Path $sessionRoot "missing-dependency-hash"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $missingHashOutput `
            -SourceRevision $revision `
            -SourceDateEpoch $epoch `
            -TestFixture
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
        -SourceDateEpoch $epoch `
        -TestFixture `
        -ExpectedMilesStubSha256 $fixtureDependencyHash
    & $packageScript `
        -RuntimeDirectory $runtimeDirectory `
        -OutputDirectory $secondOutput `
        -SourceRevision $revision `
        -SourceDateEpoch $epoch `
        -TestFixture `
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
        if ($manifest.source_revision -ne $revision -or
            $manifest.source_date_epoch -ne $epoch -or
            $manifest.distribution -ne "test_fixture" -or
            $manifest.source_tree -ne "fixture" -or
            $manifest.package_contract_sha256 -ne (Get-FileHash -LiteralPath $contractPath -Algorithm SHA256).Hash.ToLowerInvariant() -or
            $manifest.asset_provenance_sha256 -ne (Get-FileHash -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/asset-provenance.json") -Algorithm SHA256).Hash.ToLowerInvariant() -or
            $manifest.runtime_dependency_verification.name -ne "mss32.dll" -or
            $manifest.runtime_dependency_verification.sha256 -ne $fixtureDependencyHash.ToLowerInvariant() -or
            $manifest.runtime_dependency_verification.provenance_id -ne [string]$milesEntry[0].provenance_id -or
            $manifest.runtime_dependency_verification.policy -ne [string]$milesEntry[0].hash_verification -or
            $manifest.renderer_execution -ne "not_performed" -or
            $manifest.manual_playthrough_claimed -ne $false) {
            throw "Private package manifest does not preserve the governed source/evidence state."
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
            -SourceDateEpoch $epoch `
            -TestFixture `
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
            -SourceDateEpoch $epoch `
            -TestFixture `
            -ExpectedMilesStubSha256 ("0" * 64)
    }
    catch {
        $caught = $_.Exception.Message -match "independently proven integrated-build hash"
    }
    if (-not $caught -or (Test-Path -LiteralPath $dependencyMismatchOutput)) {
        throw "The package gate did not reject a runtime dependency hash mismatch before staging output."
    }

    $executablePath = Join-Path $runtimeDirectory "ProjectTempestDemo.exe"
    [IO.File]::WriteAllBytes($executablePath, [byte[]](0x4E, 0x4F, 0x54, 0x50, 0x45))
    $invalidExecutableOutput = Join-Path $sessionRoot "invalid-executable"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $invalidExecutableOutput `
            -SourceRevision $revision `
            -SourceDateEpoch $epoch `
            -TestFixture `
            -ExpectedMilesStubSha256 $fixtureDependencyHash
    }
    catch {
        $caught = $_.Exception.Message -match "not a PE image"
    }
    if (-not $caught -or (Test-Path -LiteralPath $invalidExecutableOutput)) {
        throw "The package gate did not reject a non-PE release executable before staging output."
    }

    [IO.File]::WriteAllText($executablePath, "MZPTDEMO", [Text.UTF8Encoding]::new($false))
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
            -SourceDateEpoch $epoch `
            -TestFixture `
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
