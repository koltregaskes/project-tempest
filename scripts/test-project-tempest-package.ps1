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
$packageVerifier = Join-Path $PSScriptRoot "assert-project-tempest-private-package.ps1"
$packageVerifierText = Get-Content -LiteralPath $packageVerifier -Raw
if ($packageVerifierText -notmatch '\[Array\]::Sort\(\$orderedArchiveNames, \[StringComparer\]::Ordinal\)' -or
    $packageVerifierText -notmatch '\$receiptJson\s*=\s*\(ConvertTo-DeterministicJson -Value \$receipt\)' -or
    $packageVerifierText -match '\$receipt\s*\|\s*ConvertTo-Json') {
    throw "The private-package consumer receipt must use one ordinal order and a PowerShell-version-independent serializer."
}
if ($packageVerifierText -notmatch '\$reviewedSpec\s*=\s*"\$ExpectedReviewedSourceRevision' -or
    $packageVerifierText -notmatch 'git -C \$repositoryRoot rev-parse \$reviewedSpec' -or
    $packageVerifierText -notmatch '\[IO\.File\]::WriteAllBytes\(\$packagedBlobPath, \$entryBytes\[\$packageName\]\)' -or
    $packageVerifierText -match '\$canonicalPackageText') {
    throw "Packaged repository files must be byte-exact Git blobs from the reviewed revision."
}
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
$ciWorkflowPath = Join-Path $repositoryRoot ".github/workflows/ci.yml"
$ciWorkflowText = Get-Content -LiteralPath $ciWorkflowPath -Raw
$artifactGatePathFilters = [regex]::Matches(
    $ciWorkflowText,
    "(?m)^\s+- 'scripts/assert-project-tempest-artifact-boundary\.ps1'\s*$"
).Count
$privatePackageVerifierPathFilters = [regex]::Matches(
    $ciWorkflowText,
    "(?m)^\s+- 'scripts/assert-project-tempest-private-package\.ps1'\s*$"
).Count
$workflowSurfacePathFilters = [regex]::Matches(
    $ciWorkflowText,
    "(?m)^\s+- '\.github/workflows/(?:ci|build-toolchain)\.yml'\s*$"
).Count
$tempestFilterIndex = $ciWorkflowText.IndexOf("            tempest:", [StringComparison]::Ordinal)
$ciWorkflowPathFilterIndex = $ciWorkflowText.IndexOf("              - '.github/workflows/ci.yml'", [StringComparison]::Ordinal)
$buildWorkflowPathFilterIndex = $ciWorkflowText.IndexOf("              - '.github/workflows/build-toolchain.yml'", [StringComparison]::Ordinal)
$artifactGatePathFilterIndex = $ciWorkflowText.IndexOf("              - 'scripts/assert-project-tempest-artifact-boundary.ps1'", [StringComparison]::Ordinal)
$privatePackageVerifierPathFilterIndex = $ciWorkflowText.IndexOf("              - 'scripts/assert-project-tempest-private-package.ps1'", [StringComparison]::Ordinal)
$changesSummaryIndex = $ciWorkflowText.IndexOf("      - name: Changes Summary", [StringComparison]::Ordinal)
$externalExecutableOutputWrites = [regex]::Matches(
    $workflowText,
    '(?m)^\s*"tempest_executable_sha256=\$primaryExecutableHash" \| Out-File -FilePath \$env:GITHUB_OUTPUT\b'
).Count
$externalMilesOutputWrites = [regex]::Matches(
    $workflowText,
    '(?m)^\s*"tempest_miles_sha256=\$primaryMilesHash" \| Out-File -FilePath \$env:GITHUB_OUTPUT\b'
).Count
if ($artifactGatePathFilters -ne 1 -or
    $privatePackageVerifierPathFilters -ne 1 -or
    $workflowSurfacePathFilters -ne 2 -or
    $tempestFilterIndex -lt 0 -or
    $ciWorkflowPathFilterIndex -le $tempestFilterIndex -or
    $buildWorkflowPathFilterIndex -le $ciWorkflowPathFilterIndex -or
    $artifactGatePathFilterIndex -le $tempestFilterIndex -or
    $privatePackageVerifierPathFilterIndex -le $artifactGatePathFilterIndex -or
    $changesSummaryIndex -le $privatePackageVerifierPathFilterIndex -or
    $ciWorkflowText -notmatch '(?s)validate-project-tempest-assets:.+?needs\.detect-changes\.outputs\.tempest == ''true''' -or
    $ciWorkflowText -notmatch '(?s)build-generals:.+?needs\.detect-changes\.outputs\.tempest == ''true''' -or
    $ciWorkflowText -notmatch '(?s)verify-project-tempest-private-install:.+?needs:.+?build-generals.+?actions/download-artifact@.+?name: Generals-win32\+t\+e.+?EXPECTED_TEMPEST_EXECUTABLE_SHA256: \$\{\{ needs\.build-generals\.outputs\.tempest_executable_sha256 \}\}.+?EXPECTED_TEMPEST_MILES_SHA256: \$\{\{ needs\.build-generals\.outputs\.tempest_miles_sha256 \}\}.+?assert-project-tempest-private-package\.ps1.+?-ExpectedExecutableSha256 \$expectedExecutableSha256.+?-ExpectedMilesStubSha256 \$expectedMilesSha256' -or
    $workflowText -notmatch '(?m)^\s*tempest_executable_sha256:\s*\$\{\{ steps\.tempest-package\.outputs\.tempest_executable_sha256 \}\}\s*$' -or
    $workflowText -notmatch '(?m)^\s*tempest_miles_sha256:\s*\$\{\{ steps\.tempest-package\.outputs\.tempest_miles_sha256 \}\}\s*$' -or
    $externalExecutableOutputWrites -ne 1 -or
    $externalMilesOutputWrites -ne 1 -or
    $ciWorkflowText -notmatch '(?m)^\s*\$expectedInstalledFileCount = @\(\$contract\.runtime_files\)\.Count \+ @\(\$contract\.repository_files\)\.Count \+ 2\s*$' -or
    $ciWorkflowText -notmatch '(?m)^\s*\$receipt\.installed_file_count -ne \$expectedInstalledFileCount\) \{\s*$' -or
    $ciWorkflowText -match '(?m)^\s*\$receipt\.installed_file_count -ne \d+') {
    throw "The shared Project Tempest artifact boundary must route boundary-only changes into GenCI exactly once."
}
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

function New-MutatedPackageArchive {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourcePath,
        [Parameter(Mandatory = $true)]
        [string]$DestinationPath,
        [string]$AddEntryName = "",
        [byte[]]$AddEntryBytes = [byte[]]::new(0),
        [int]$AddEntryExternalAttributes = 0,
        [string]$ReplaceEntryName = "",
        [byte[]]$ReplaceEntryBytes = [byte[]]::new(0)
    )

    $source = [IO.Compression.ZipFile]::OpenRead($SourcePath)
    $destination = [IO.Compression.ZipFile]::Open(
        $DestinationPath,
        [IO.Compression.ZipArchiveMode]::Create
    )
    try {
        foreach ($sourceEntry in $source.Entries) {
            $destinationEntry = $destination.CreateEntry(
                $sourceEntry.FullName,
                [IO.Compression.CompressionLevel]::Optimal
            )
            $destinationEntry.LastWriteTime = $sourceEntry.LastWriteTime
            $destinationEntry.ExternalAttributes = $sourceEntry.ExternalAttributes
            $outputStream = $destinationEntry.Open()
            try {
                if ($sourceEntry.FullName -eq $ReplaceEntryName) {
                    $outputStream.Write($ReplaceEntryBytes, 0, $ReplaceEntryBytes.Length)
                }
                else {
                    $inputStream = $sourceEntry.Open()
                    try {
                        $inputStream.CopyTo($outputStream)
                    }
                    finally {
                        $inputStream.Dispose()
                    }
                }
            }
            finally {
                $outputStream.Dispose()
            }
        }
        if ($AddEntryName) {
            $addedEntry = $destination.CreateEntry(
                $AddEntryName,
                [IO.Compression.CompressionLevel]::Optimal
            )
            if ($AddEntryExternalAttributes -ne 0) {
                $addedEntry.ExternalAttributes = $AddEntryExternalAttributes
            }
            $addedOutput = $addedEntry.Open()
            try {
                $addedOutput.Write($AddEntryBytes, 0, $AddEntryBytes.Length)
            }
            finally {
                $addedOutput.Dispose()
            }
        }
    }
    finally {
        $destination.Dispose()
        $source.Dispose()
    }
}

function Get-TestBytesSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Bytes
    )

    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function New-CoherentlyForgedExecutablePackage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourcePath,
        [Parameter(Mandatory = $true)]
        [string]$DestinationPath,
        [Parameter(Mandatory = $true)]
        [byte[]]$ForgedExecutableBytes
    )

    $prefix = "$($contract.package_directory)/"
    $executableName = "${prefix}ProjectTempestDemo.exe"
    $manifestName = "${prefix}package-manifest.json"
    $sumsName = "${prefix}SHA256SUMS.txt"
    $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $source = [IO.Compression.ZipFile]::OpenRead($SourcePath)
    $destination = $null
    try {
        $manifest = $utf8.GetString((Get-ZipEntryBytes -Archive $source -Name $manifestName)) |
            ConvertFrom-Json
        $manifestExecutable = @($manifest.files | Where-Object { $_.name -eq "ProjectTempestDemo.exe" })
        if ($manifestExecutable.Count -ne 1) {
            throw "Coherent-forgery fixture could not find the manifest executable row."
        }
        $forgedExecutableHash = Get-TestBytesSha256 -Bytes $ForgedExecutableBytes
        $manifestExecutable[0].sha256 = $forgedExecutableHash
        $manifestExecutable[0].length = $ForgedExecutableBytes.LongLength
        $manifest.executable_verification.sha256 = $forgedExecutableHash
        $manifestBytes = $utf8.GetBytes(
            (($manifest | ConvertTo-Json -Depth 8) -replace "`r`n", "`n") + "`n"
        )
        $manifestHash = Get-TestBytesSha256 -Bytes $manifestBytes

        $sumText = $utf8.GetString((Get-ZipEntryBytes -Archive $source -Name $sumsName))
        $rewrittenSumLines = @(
            $sumText.TrimEnd("`n") -split "`n" |
                ForEach-Object {
                    if ($_ -match '^[0-9a-f]{64}  ProjectTempestDemo\.exe$') {
                        "$forgedExecutableHash  ProjectTempestDemo.exe"
                    }
                    elseif ($_ -match '^[0-9a-f]{64}  package-manifest\.json$') {
                        "$manifestHash  package-manifest.json"
                    }
                    else {
                        $_
                    }
                }
        )
        if (@($rewrittenSumLines | Where-Object { $_ -eq "$forgedExecutableHash  ProjectTempestDemo.exe" }).Count -ne 1 -or
            @($rewrittenSumLines | Where-Object { $_ -eq "$manifestHash  package-manifest.json" }).Count -ne 1) {
            throw "Coherent-forgery fixture could not rewrite both governed hash records."
        }
        $sumBytes = $utf8.GetBytes(($rewrittenSumLines -join "`n") + "`n")

        $destination = [IO.Compression.ZipFile]::Open(
            $DestinationPath,
            [IO.Compression.ZipArchiveMode]::Create
        )
        foreach ($sourceEntry in $source.Entries) {
            $destinationEntry = $destination.CreateEntry(
                $sourceEntry.FullName,
                [IO.Compression.CompressionLevel]::Optimal
            )
            $destinationEntry.LastWriteTime = $sourceEntry.LastWriteTime
            $destinationEntry.ExternalAttributes = $sourceEntry.ExternalAttributes
            $bytes = if ($sourceEntry.FullName -eq $executableName) {
                $ForgedExecutableBytes
            }
            elseif ($sourceEntry.FullName -eq $manifestName) {
                $manifestBytes
            }
            elseif ($sourceEntry.FullName -eq $sumsName) {
                $sumBytes
            }
            else {
                Get-ZipEntryBytes -Archive $source -Name $sourceEntry.FullName
            }
            $outputStream = $destinationEntry.Open()
            try {
                $outputStream.Write($bytes, 0, $bytes.Length)
            }
            finally {
                $outputStream.Dispose()
            }
        }
    }
    finally {
        if ($null -ne $destination) {
            $destination.Dispose()
        }
        $source.Dispose()
    }
}

function New-CoherentlyForgedGovernedFilePackage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourcePath,
        [Parameter(Mandatory = $true)]
        [string]$DestinationPath,
        [Parameter(Mandatory = $true)]
        [string]$FileName,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedKind,
        [Parameter(Mandatory = $true)]
        [byte[]]$ForgedFileBytes
    )

    if ($FileName -notmatch '^[^/\\:]+$') {
        throw "Coherent file-forgery fixture requires one flat governed package name."
    }
    $prefix = "$($contract.package_directory)/"
    $fileEntryName = "$prefix$FileName"
    $manifestName = "${prefix}package-manifest.json"
    $sumsName = "${prefix}SHA256SUMS.txt"
    $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $source = [IO.Compression.ZipFile]::OpenRead($SourcePath)
    $destination = $null
    try {
        $manifest = $utf8.GetString((Get-ZipEntryBytes -Archive $source -Name $manifestName)) |
            ConvertFrom-Json
        $manifestFile = @($manifest.files | Where-Object { $_.name -eq $FileName })
        if ($manifestFile.Count -ne 1 -or [string]$manifestFile[0].kind -ne $ExpectedKind) {
            throw "Coherent file-forgery fixture could not find governed '$ExpectedKind' file '$FileName'."
        }
        $forgedFileHash = Get-TestBytesSha256 -Bytes $ForgedFileBytes
        $manifestFile[0].sha256 = $forgedFileHash
        $manifestFile[0].length = $ForgedFileBytes.LongLength
        $manifestBytes = $utf8.GetBytes(
            (($manifest | ConvertTo-Json -Depth 8) -replace "`r`n", "`n") + "`n"
        )
        $manifestHash = Get-TestBytesSha256 -Bytes $manifestBytes

        $sumText = $utf8.GetString((Get-ZipEntryBytes -Archive $source -Name $sumsName))
        $escapedFileName = [regex]::Escape($FileName)
        $rewrittenSumLines = @(
            $sumText.TrimEnd("`n") -split "`n" |
                ForEach-Object {
                    if ($_ -match "^[0-9a-f]{64}  $escapedFileName$") {
                        "$forgedFileHash  $FileName"
                    }
                    elseif ($_ -match '^[0-9a-f]{64}  package-manifest\.json$') {
                        "$manifestHash  package-manifest.json"
                    }
                    else {
                        $_
                    }
                }
        )
        if (@($rewrittenSumLines | Where-Object { $_ -eq "$forgedFileHash  $FileName" }).Count -ne 1 -or
            @($rewrittenSumLines | Where-Object { $_ -eq "$manifestHash  package-manifest.json" }).Count -ne 1) {
            throw "Coherent asset-forgery fixture could not rewrite both governed hash records."
        }
        $sumBytes = $utf8.GetBytes(($rewrittenSumLines -join "`n") + "`n")

        $destination = [IO.Compression.ZipFile]::Open(
            $DestinationPath,
            [IO.Compression.ZipArchiveMode]::Create
        )
        foreach ($sourceEntry in $source.Entries) {
            $destinationEntry = $destination.CreateEntry(
                $sourceEntry.FullName,
                [IO.Compression.CompressionLevel]::Optimal
            )
            $destinationEntry.LastWriteTime = $sourceEntry.LastWriteTime
            $destinationEntry.ExternalAttributes = $sourceEntry.ExternalAttributes
            $bytes = if ($sourceEntry.FullName -eq $fileEntryName) {
                $ForgedFileBytes
            }
            elseif ($sourceEntry.FullName -eq $manifestName) {
                $manifestBytes
            }
            elseif ($sourceEntry.FullName -eq $sumsName) {
                $sumBytes
            }
            else {
                Get-ZipEntryBytes -Archive $source -Name $sourceEntry.FullName
            }
            $outputStream = $destinationEntry.Open()
            try {
                $outputStream.Write($bytes, 0, $bytes.Length)
            }
            finally {
                $outputStream.Dispose()
            }
        }
    }
    finally {
        if ($null -ne $destination) {
            $destination.Dispose()
        }
        $source.Dispose()
    }
}

function Assert-PackageVerifierRejects {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackagePath,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string]$MessagePattern,
        [Parameter(Mandatory = $true)]
        [string]$Revision,
        [Parameter(Mandatory = $true)]
        [string]$ExecutableSha256,
        [Parameter(Mandatory = $true)]
        [string]$MilesSha256,
        [string]$ReviewedRevision = ""
    )

    if ([string]::IsNullOrWhiteSpace($ReviewedRevision)) {
        $ReviewedRevision = $Revision
    }
    $installDirectory = Join-Path $sessionRoot ("consumer-negative/" + $Name + "/install")
    $receiptPath = Join-Path $sessionRoot ("consumer-negative/" + $Name + "/receipt.json")
    New-Item -ItemType Directory -Path (Split-Path -Parent $installDirectory) -Force | Out-Null
    $caught = $false
    try {
        & $packageVerifier `
            -PackagePath $PackagePath `
            -InstallDirectory $installDirectory `
            -ReceiptPath $receiptPath `
            -ExpectedBuildSourceRevision $Revision `
            -ExpectedReviewedSourceRevision $ReviewedRevision `
            -ExpectedExecutableSha256 $ExecutableSha256 `
            -ExpectedMilesStubSha256 $MilesSha256 `
            -ExpectedDistribution test_fixture `
            -ExpectedSourceTree fixture
    }
    catch {
        $caught = $_.Exception.Message -match $MessagePattern
    }
    if (-not $caught -or
        (Test-Path -LiteralPath $installDirectory) -or
        (Test-Path -LiteralPath $receiptPath)) {
        throw "The private-package consumer accepted '$Name' or left staged output behind."
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
        [string]$LoosePayloadRelativePath = ""
    )

    $directory = Join-Path $sessionRoot ("artifact-boundary/" + $Name)
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    foreach ($ordinaryName in @("generalsv.exe", "generalsv.pdb", "mss32.dll")) {
        [IO.File]::WriteAllBytes((Join-Path $directory $ordinaryName), [byte[]](0x47, 0x45, 0x4E))
    }
    $nestedOrdinaryDirectory = Join-Path $directory "nested/ordinary"
    New-Item -ItemType Directory -Path $nestedOrdinaryDirectory -Force | Out-Null
    [IO.File]::WriteAllBytes(
        (Join-Path $nestedOrdinaryDirectory "generals-helper.pdb"),
        [byte[]](0x47, 0x45, 0x4E)
    )
    [IO.File]::WriteAllBytes(
        (Join-Path $nestedOrdinaryDirectory "mss32.dll"),
        [byte[]](0x4D, 0x53, 0x53)
    )
    if ($IncludePrivatePackage) {
        [IO.File]::WriteAllBytes(
            (Join-Path $directory "ProjectTempestDemo-private.zip"),
            [byte[]](0x50, 0x4B, 0x05, 0x06)
        )
    }
    if ($LoosePayloadRelativePath.Length -gt 0) {
        $loosePayloadPath = Join-Path $directory $LoosePayloadRelativePath
        $loosePayloadParent = Split-Path -Parent $loosePayloadPath
        New-Item -ItemType Directory -Path $loosePayloadParent -Force | Out-Null
        [IO.File]::WriteAllBytes($loosePayloadPath, [byte[]](0x50, 0x54))
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

    $nestedPackageDirectory = New-ArtifactBoundaryFixture -Name "nested-governing-package"
    $nestedPackagePath = Join-Path $nestedPackageDirectory "nested/package/ProjectTempestDemo-private.zip"
    New-Item -ItemType Directory -Path (Split-Path -Parent $nestedPackagePath) -Force | Out-Null
    [IO.File]::WriteAllBytes($nestedPackagePath, [byte[]](0x50, 0x4B, 0x05, 0x06))
    $caught = $false
    try {
        .\scripts\assert-project-tempest-artifact-boundary.ps1 `
            -ArtifactDirectory $nestedPackageDirectory `
            -ExpectedPrivatePackageCount 1
    }
    catch {
        $caught = $_.Exception.Message -match "must be staged at the artifact root"
    }
    if (-not $caught) {
        throw "The shared artifact boundary accepted a nested governed private package."
    }

    $looseArtifactFixtures = @(
        "ProjectTempestDemo.exe",
        "nested/demo/ProjectTempestDemo.exe",
        "nested/demo/ProjectTempestDemo.pdb",
        "nested/demo/ProjectTempestDemo.dll",
        "nested/demo/ProjectTempestDemo-helper.dll",
        "nested/tests/project_tempest_headless_acceptance.exe",
        "nested/tests/project_tempest_headless_acceptance.pdb",
        "nested/runtime/project_tempest_runtime.dll",
        "nested/assets/courier.w3d",
        "nested/assets/pt_alert.wav",
        "nested/notices/EA-Tunable-Colorblindness-NOTICE.txt"
    )
    for ($fixtureIndex = 0; $fixtureIndex -lt $looseArtifactFixtures.Count; $fixtureIndex++) {
        $looseRelativePath = $looseArtifactFixtures[$fixtureIndex]
        $looseName = Split-Path -Leaf $looseRelativePath
        $looseDirectory = New-ArtifactBoundaryFixture `
            -Name ("loose-" + $fixtureIndex) `
            -IncludePrivatePackage `
            -LoosePayloadRelativePath $looseRelativePath
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

    $reparseTargetDirectory = Join-Path $sessionRoot "artifact-boundary/reparse-target-directory"
    New-Item -ItemType Directory -Path $reparseTargetDirectory -Force | Out-Null
    [IO.File]::WriteAllBytes(
        (Join-Path $reparseTargetDirectory "ProjectTempestDemo.exe"),
        [byte[]](0x50, 0x54)
    )
    $reparseDirectoryFixture = New-ArtifactBoundaryFixture -Name "reparse-directory" -IncludePrivatePackage
    $junctionPath = Join-Path $reparseDirectoryFixture "nested-junction"
    $reparseDirectoryCreated = $false
    $reparseDirectoryCreationErrors = [Collections.Generic.List[string]]::new()
    $reparseDirectoryTypes = if (
        [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
    ) { @("Junction", "SymbolicLink") } else { @("SymbolicLink") }
    foreach ($reparseDirectoryType in $reparseDirectoryTypes) {
        $caught = $false
        try {
            New-Item `
                -ItemType $reparseDirectoryType `
                -Path $junctionPath `
                -Target $reparseTargetDirectory `
                -ErrorAction Stop | Out-Null
            $reparseDirectoryCreated = $true
            break
        }
        catch {
            $reparseDirectoryCreationErrors.Add("${reparseDirectoryType}: $($_.Exception.Message)")
        }
    }
    if ($reparseDirectoryCreated) {
        try {
            $caught = $false
            try {
                .\scripts\assert-project-tempest-artifact-boundary.ps1 `
                    -ArtifactDirectory $reparseDirectoryFixture `
                    -ExpectedPrivatePackageCount 1
            }
            catch {
                $caught = $_.Exception.Message -match "reparse-point directory"
            }
        }
        finally {
            if (Test-Path -LiteralPath $junctionPath) {
                Remove-Item -LiteralPath $junctionPath -Force
            }
        }
        if (-not $caught) {
            throw "The shared artifact boundary accepted a nested reparse-point directory."
        }
    }
    else {
        Write-Host "SKIP: Platform did not permit the reparse-point directory fixture: $($reparseDirectoryCreationErrors -join ' | ')"
    }

    $reparseFileFixture = New-ArtifactBoundaryFixture -Name "reparse-file" -IncludePrivatePackage
    $reparseFileTarget = Join-Path $sessionRoot "artifact-boundary/reparse-file-target.bin"
    [IO.File]::WriteAllBytes($reparseFileTarget, [byte[]](0x50, 0x54))
    $symbolicLinkPath = Join-Path $reparseFileFixture "ProjectTempestDemo.pdb"
    $symbolicLinkCreated = $false
    try {
        New-Item -ItemType SymbolicLink -Path $symbolicLinkPath -Target $reparseFileTarget -ErrorAction Stop | Out-Null
        $symbolicLinkCreated = $true
        $caught = $false
        try {
            .\scripts\assert-project-tempest-artifact-boundary.ps1 `
                -ArtifactDirectory $reparseFileFixture `
                -ExpectedPrivatePackageCount 1
        }
        catch {
            $caught = $_.Exception.Message -match "reparse-point file"
        }
        if (-not $caught) {
            throw "The shared artifact boundary accepted a nested reparse-point file."
        }
    }
    catch {
        if ($symbolicLinkCreated -or $_.Exception.Message -match "accepted a nested reparse-point file") {
            throw
        }
        Write-Host "SKIP: Windows did not permit the reparse-point file fixture: $($_.Exception.Message)"
    }
    finally {
        if (Test-Path -LiteralPath $symbolicLinkPath) {
            Remove-Item -LiteralPath $symbolicLinkPath -Force
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
                    [ordered]@{ name = "freegrid_victory_a"; outcome = "victory"; ticks = $entry.victory_ticks; final_checksum = $entry.victory_final_checksum; trace_checksum = $entry.victory_trace_checksum; territory_capture = $true; construction = $true; production = $true; faction_abilities = $true; result_flow = $true; restart_flow = $true },
                    [ordered]@{ name = "chorus_defeat"; outcome = "defeat"; ticks = $entry.defeat_ticks; final_checksum = $entry.defeat_final_checksum; trace_checksum = $entry.defeat_trace_checksum; result_flow = $true; restart_flow = $true },
                    [ordered]@{ name = "freegrid_victory_b"; outcome = "victory"; ticks = $entry.victory_ticks; final_checksum = $entry.victory_final_checksum; trace_checksum = $entry.victory_trace_checksum; territory_capture = $true; construction = $true; production = $true; faction_abilities = $true; result_flow = $true; restart_flow = $true }
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

    $consumerInstallA = Join-Path $sessionRoot "consumer-clean/install-a"
    $consumerInstallB = Join-Path $sessionRoot "consumer-clean/install-b"
    $consumerReceiptA = Join-Path $sessionRoot "consumer-clean/receipt-a.json"
    $consumerReceiptB = Join-Path $sessionRoot "consumer-clean/receipt-b.json"
    New-Item -ItemType Directory -Path (Split-Path -Parent $consumerInstallA) -Force | Out-Null
    & $packageVerifier `
        -PackagePath $firstArchive `
        -InstallDirectory $consumerInstallA `
        -ReceiptPath $consumerReceiptA `
        -ExpectedBuildSourceRevision $revision `
        -ExpectedReviewedSourceRevision $revision `
        -ExpectedExecutableSha256 $fixtureExecutableHash `
        -ExpectedMilesStubSha256 $fixtureDependencyHash `
        -ExpectedDistribution test_fixture `
        -ExpectedSourceTree fixture
    & $packageVerifier `
        -PackagePath $firstArchive `
        -InstallDirectory $consumerInstallB `
        -ReceiptPath $consumerReceiptB `
        -ExpectedBuildSourceRevision $revision `
        -ExpectedReviewedSourceRevision $revision `
        -ExpectedExecutableSha256 $fixtureExecutableHash `
        -ExpectedMilesStubSha256 $fixtureDependencyHash `
        -ExpectedDistribution test_fixture `
        -ExpectedSourceTree fixture

    $consumerReceipt = Get-Content -LiteralPath $consumerReceiptA -Raw | ConvertFrom-Json
    $consumerReceiptBObject = Get-Content -LiteralPath $consumerReceiptB -Raw | ConvertFrom-Json
    $consumerInstalledFiles = @(Get-ChildItem -LiteralPath $consumerInstallA -File -Force)
    $consumerReceiptHash = (Get-FileHash -LiteralPath $consumerReceiptA -Algorithm SHA256).Hash.ToLowerInvariant()
    $expectedVerifiedAssetCount = @($contract.runtime_files | Where-Object { $_.kind -eq "asset" }).Count
    $expectedInstalledFileCount = @($contract.runtime_files).Count + @($contract.repository_files).Count + 2
    if ($consumerReceipt.schema_version -ne 1 -or
        [string]$consumerReceipt.verification -ne "verified_without_execution" -or
        [string]$consumerReceipt.archive_sha256 -ne $firstHash -or
        [string]$consumerReceipt.source_revision -ne $revision -or
        [string]$consumerReceipt.reviewed_source_revision -ne $revision -or
        [string]$consumerReceipt.source_tree -ne "fixture" -or
        [string]$consumerReceipt.distribution -ne "test_fixture" -or
        [string]$consumerReceipt.executable_sha256 -ne $fixtureExecutableHash.ToLowerInvariant() -or
        [string]$consumerReceipt.miles_sha256 -ne $fixtureDependencyHash.ToLowerInvariant() -or
        [string]$consumerReceipt.asset_hash_source -ne "reviewed_checkout_and_canonical_provenance" -or
        $consumerReceipt.verified_asset_count -ne $expectedVerifiedAssetCount -or
        [string]$consumerReceipt.renderer_execution -ne "not_performed" -or
        $consumerReceipt.manual_playthrough_claimed -ne $false -or
        $consumerReceipt.installed_file_count -ne $expectedInstalledFileCount -or
        @($consumerReceipt.files).Count -ne $expectedInstalledFileCount -or
        $consumerInstalledFiles.Count -ne $expectedInstalledFileCount -or
        [string]$consumerReceipt.reviewed_contract_canonical_sha256 -notmatch '^[0-9a-f]{64}$' -or
        [string]$consumerReceipt.reviewed_provenance_canonical_sha256 -notmatch '^[0-9a-f]{64}$' -or
        (Get-FileHash -LiteralPath $consumerReceiptB -Algorithm SHA256).Hash.ToLowerInvariant() -ne $consumerReceiptHash -or
        ($consumerReceipt | ConvertTo-Json -Depth 6) -ne ($consumerReceiptBObject | ConvertTo-Json -Depth 6)) {
        throw "The private-package consumer did not produce two identical no-execution install receipts."
    }

    $mutatedRoot = Join-Path $sessionRoot "consumer-mutated"
    New-Item -ItemType Directory -Path $mutatedRoot -Force | Out-Null
    $traversalArchive = Join-Path $mutatedRoot "traversal.zip"
    New-MutatedPackageArchive `
        -SourcePath $firstArchive `
        -DestinationPath $traversalArchive `
        -AddEntryName "$($contract.package_directory)/../escape.exe" `
        -AddEntryBytes ([byte[]](0x4D, 0x5A))
    Assert-PackageVerifierRejects `
        -PackagePath $traversalArchive `
        -Name "traversal" `
        -MessagePattern "unsafe or nested entry" `
        -Revision $revision `
        -ExecutableSha256 $fixtureExecutableHash `
        -MilesSha256 $fixtureDependencyHash

    $caseCollisionArchive = Join-Path $mutatedRoot "case-collision.zip"
    New-MutatedPackageArchive `
        -SourcePath $firstArchive `
        -DestinationPath $caseCollisionArchive `
        -AddEntryName "$($contract.package_directory)/readme.txt" `
        -AddEntryBytes ([byte[]](0x52))
    Assert-PackageVerifierRejects `
        -PackagePath $caseCollisionArchive `
        -Name "case-collision" `
        -MessagePattern "duplicate or case-colliding entry" `
        -Revision $revision `
        -ExecutableSha256 $fixtureExecutableHash `
        -MilesSha256 $fixtureDependencyHash

    $linkArchive = Join-Path $mutatedRoot "link-entry.zip"
    $unixLinkAttributes = [BitConverter]::ToInt32(
        [BitConverter]::GetBytes([uint32]2717843456),
        0
    )
    New-MutatedPackageArchive `
        -SourcePath $firstArchive `
        -DestinationPath $linkArchive `
        -AddEntryName "$($contract.package_directory)/link.txt" `
        -AddEntryBytes ([Text.Encoding]::UTF8.GetBytes("target")) `
        -AddEntryExternalAttributes $unixLinkAttributes
    Assert-PackageVerifierRejects `
        -PackagePath $linkArchive `
        -Name "link-entry" `
        -MessagePattern "link/reparse entry" `
        -Revision $revision `
        -ExecutableSha256 $fixtureExecutableHash `
        -MilesSha256 $fixtureDependencyHash

    $forgedPackageArchive = Join-Path $mutatedRoot "forged-executable.zip"
    New-MutatedPackageArchive `
        -SourcePath $firstArchive `
        -DestinationPath $forgedPackageArchive `
        -ReplaceEntryName "$($contract.package_directory)/ProjectTempestDemo.exe" `
        -ReplaceEntryBytes (New-TestPe32GuiBytes -Marker 0x72)
    Assert-PackageVerifierRejects `
        -PackagePath $forgedPackageArchive `
        -Name "forged-executable" `
        -MessagePattern "manifest verification failed" `
        -Revision $revision `
        -ExecutableSha256 $fixtureExecutableHash `
        -MilesSha256 $fixtureDependencyHash

    $coherentlyForgedArchive = Join-Path $mutatedRoot "coherently-forged-executable.zip"
    New-CoherentlyForgedExecutablePackage `
        -SourcePath $firstArchive `
        -DestinationPath $coherentlyForgedArchive `
        -ForgedExecutableBytes (New-TestPe32GuiBytes -Marker 0x73)
    Assert-PackageVerifierRejects `
        -PackagePath $coherentlyForgedArchive `
        -Name "coherently-forged-executable" `
        -MessagePattern "do not match the externally proven two-build hashes" `
        -Revision $revision `
        -ExecutableSha256 $fixtureExecutableHash `
        -MilesSha256 $fixtureDependencyHash

    $coherentlyForgedAssetArchive = Join-Path $mutatedRoot "coherently-forged-asset.zip"
    New-CoherentlyForgedGovernedFilePackage `
        -SourcePath $firstArchive `
        -DestinationPath $coherentlyForgedAssetArchive `
        -FileName "courier.w3d" `
        -ExpectedKind "asset" `
        -ForgedFileBytes ([Text.Encoding]::UTF8.GetBytes("coherently forged courier asset"))
    Assert-PackageVerifierRejects `
        -PackagePath $coherentlyForgedAssetArchive `
        -Name "coherently-forged-asset" `
        -MessagePattern "does not match reviewed asset provenance" `
        -Revision $revision `
        -ExecutableSha256 $fixtureExecutableHash `
        -MilesSha256 $fixtureDependencyHash

    $coherentlyForgedAnalyserArchive = Join-Path $mutatedRoot "coherently-forged-analyser.zip"
    New-CoherentlyForgedGovernedFilePackage `
        -SourcePath $firstArchive `
        -DestinationPath $coherentlyForgedAnalyserArchive `
        -FileName "ANALYSE-MANUAL-EVIDENCE.ps1" `
        -ExpectedKind "manual_evidence_analyser" `
        -ForgedFileBytes ([Text.Encoding]::UTF8.GetBytes("Write-Output 'forged analyser'"))
    Assert-PackageVerifierRejects `
        -PackagePath $coherentlyForgedAnalyserArchive `
        -Name "coherently-forged-analyser" `
        -MessagePattern "does not match the reviewed checkout bytes" `
        -Revision $revision `
        -ExecutableSha256 $fixtureExecutableHash `
        -MilesSha256 $fixtureDependencyHash

    $reviewedAnalyserPath = Join-Path $repositoryRoot "scripts/analyse-project-tempest-manual-evidence.ps1"
    $reviewedAnalyserText = [IO.File]::ReadAllText($reviewedAnalyserPath, [Text.UTF8Encoding]::new($false, $true))
    $lineEndingForgedAnalyserText = if ($reviewedAnalyserText.Contains("`r`n")) {
        $reviewedAnalyserText.Replace("`r`n", "`n")
    }
    else {
        $reviewedAnalyserText.Replace("`n", "`r`n")
    }
    $lineEndingForgedAnalyserBytes = [Text.UTF8Encoding]::new($false).GetBytes($lineEndingForgedAnalyserText)
    if ((Get-TestBytesSha256 -Bytes $lineEndingForgedAnalyserBytes) -eq
        (Get-FileHash -LiteralPath $reviewedAnalyserPath -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw "Line-ending forgery fixture did not change the reviewed analyser bytes."
    }
    $lineEndingForgedAnalyserArchive = Join-Path $mutatedRoot "line-ending-forged-analyser.zip"
    New-CoherentlyForgedGovernedFilePackage `
        -SourcePath $firstArchive `
        -DestinationPath $lineEndingForgedAnalyserArchive `
        -FileName "ANALYSE-MANUAL-EVIDENCE.ps1" `
        -ExpectedKind "manual_evidence_analyser" `
        -ForgedFileBytes $lineEndingForgedAnalyserBytes
    Assert-PackageVerifierRejects `
        -PackagePath $lineEndingForgedAnalyserArchive `
        -Name "line-ending-forged-analyser" `
        -MessagePattern "does not match the reviewed checkout bytes" `
        -Revision $revision `
        -ExecutableSha256 $fixtureExecutableHash `
        -MilesSha256 $fixtureDependencyHash

    Assert-PackageVerifierRejects `
        -PackagePath $firstArchive `
        -Name "wrong-reviewed-revision" `
        -MessagePattern "not bound to the expected reviewed source" `
        -Revision $revision `
        -ExecutableSha256 $fixtureExecutableHash `
        -MilesSha256 $fixtureDependencyHash `
        -ReviewedRevision ("f" * 40)

    $existingInstall = Join-Path $sessionRoot "consumer-existing/install"
    $existingReceipt = Join-Path $sessionRoot "consumer-existing/receipt.json"
    New-Item -ItemType Directory -Path $existingInstall -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $existingInstall "keep.txt"), "keep")
    $caught = $false
    try {
        & $packageVerifier `
            -PackagePath $firstArchive `
            -InstallDirectory $existingInstall `
            -ReceiptPath $existingReceipt `
            -ExpectedBuildSourceRevision $revision `
            -ExpectedReviewedSourceRevision $revision `
            -ExpectedExecutableSha256 $fixtureExecutableHash `
            -ExpectedMilesStubSha256 $fixtureDependencyHash `
            -ExpectedDistribution test_fixture `
            -ExpectedSourceTree fixture
    }
    catch {
        $caught = $_.Exception.Message -match "requires a new destination directory"
    }
    if (-not $caught -or
        -not (Test-Path -LiteralPath (Join-Path $existingInstall "keep.txt") -PathType Leaf) -or
        (Test-Path -LiteralPath $existingReceipt)) {
        throw "The private-package consumer overwrote or disturbed an existing install directory."
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
    Write-Host "Fixture install receipt SHA256: $consumerReceiptHash"
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
