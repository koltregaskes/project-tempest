[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceDirectory,

    [string]$ObservationsPath,

    [string]$PackageDirectory = $PSScriptRoot,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{40}$')]
    [string]$ExpectedReviewedSourceRevision,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ExpectedExecutableSha256,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ExpectedMilesSha256,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ExpectedPackageContractSha256,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ExpectedPackageManifestSha256,

    [string]$ReportPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-TrueProperty {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $property = $Object.PSObject.Properties[$Name]
    return $null -ne $property -and $property.Value -eq $true
}

if (-not (Test-Path -LiteralPath $EvidenceDirectory -PathType Container)) {
    throw "Manual evidence directory is unavailable: '$EvidenceDirectory'."
}
if (-not (Test-Path -LiteralPath $PackageDirectory -PathType Container)) {
    throw "Governed package directory is unavailable: '$PackageDirectory'."
}

$evidenceRoot = (Resolve-Path -LiteralPath $EvidenceDirectory).Path.TrimEnd('\', '/')
$packageRoot = (Resolve-Path -LiteralPath $PackageDirectory).Path.TrimEnd('\', '/')
$evidenceRootPrefix = $evidenceRoot + [IO.Path]::DirectorySeparatorChar
$pathComparison = if ([IO.Path]::DirectorySeparatorChar -eq '\') {
    [StringComparison]::OrdinalIgnoreCase
} else {
    [StringComparison]::Ordinal
}

if (-not $ObservationsPath) {
    $ObservationsPath = Join-Path $evidenceRoot "manual-acceptance-observations.json"
}
$ObservationsPath = [IO.Path]::GetFullPath($ObservationsPath)
if (-not $ObservationsPath.StartsWith($evidenceRootPrefix, $pathComparison)) {
    throw "The manual observations file must remain inside the evidence directory."
}
if (-not $ReportPath) {
    $ReportPath = Join-Path $evidenceRoot "project-tempest-manual-acceptance-report.json"
}
$resolvedReportPath = [IO.Path]::GetFullPath($ReportPath)
if (-not $resolvedReportPath.StartsWith($evidenceRootPrefix, $pathComparison)) {
    throw "The acceptance report must remain inside the manual evidence directory."
}

$reparseEntries = @(
    @(
        Get-Item -LiteralPath $evidenceRoot -Force
        Get-ChildItem -LiteralPath $evidenceRoot -Recurse -Force
    ) | Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 }
)
if ($reparseEntries.Count -gt 0) {
    throw "Manual evidence rejects reparse points: $($reparseEntries.FullName -join ', ')."
}
$packageReparseEntries = @(
    @(
        Get-Item -LiteralPath $packageRoot -Force
        Get-ChildItem -LiteralPath $packageRoot -Recurse -Force
    ) | Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 }
)
if ($packageReparseEntries.Count -gt 0) {
    throw "Governed package inputs reject reparse points: $($packageReparseEntries.FullName -join ', ')."
}

$checks = [Collections.Generic.List[object]]::new()
$inventory = [Collections.Generic.List[object]]::new()
$requiredScreenshotPaths = [Collections.Generic.HashSet[string]]::new(
    $(if ($pathComparison -eq [StringComparison]::OrdinalIgnoreCase) {
        [StringComparer]::OrdinalIgnoreCase
    } else {
        [StringComparer]::Ordinal
    }))
$requiredScreenshotHashes = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

function Add-Check {
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [Parameter(Mandatory = $true)][bool]$Passed,
        [Parameter(Mandatory = $true)][string]$Evidence
    )
    $checks.Add([pscustomobject]@{ id = $Id; passed = $Passed; evidence = $Evidence })
}

function Add-InventoryFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Role
    )
    $item = Get-Item -LiteralPath $Path -Force
    $inventory.Add([pscustomobject]@{
        role = $Role
        path = $item.FullName
        length = $item.Length
        sha256 = Get-Sha256 -Path $item.FullName
    })
}

function Add-RequiredEvidenceFile {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$Role,
        [Parameter(Mandatory = $true)][string]$CheckId
    )
    $relativeIsSafe = -not [string]::IsNullOrWhiteSpace($RelativePath) -and
        -not [IO.Path]::IsPathRooted($RelativePath)
    $candidate = if ($relativeIsSafe) {
        [IO.Path]::GetFullPath((Join-Path $evidenceRoot $RelativePath))
    } else {
        ""
    }
    $contained = $relativeIsSafe -and
        $candidate.StartsWith($evidenceRootPrefix, $pathComparison)
    $exists = $contained -and (Test-Path -LiteralPath $candidate -PathType Leaf)
    $nonempty = $exists -and (Get-Item -LiteralPath $candidate -Force).Length -gt 0
    $extension = if ($exists) { [IO.Path]::GetExtension($candidate).ToLowerInvariant() } else { "" }
    $allowedExtensions = switch ($Role) {
        "resolution_screenshot" { @(".png", ".jpg", ".jpeg") }
        "accessibility_screenshot" { @(".png", ".jpg", ".jpeg") }
        "settings_screenshot" { @(".png", ".jpg", ".jpeg") }
        "short_capture" { @(".mp4", ".webm", ".mov", ".mkv") }
        "runtime_log" { @(".log", ".txt", ".jsonl") }
        default { @() }
    }
    $formatAllowed = $allowedExtensions.Count -eq 0 -or $extension -in $allowedExtensions
    $uniqueRequiredScreenshot = $true
    if ($Role -in @("resolution_screenshot", "accessibility_screenshot", "settings_screenshot")) {
        $screenshotHash = if ($exists -and $nonempty -and $formatAllowed) {
            Get-Sha256 -Path $candidate
        } else { "" }
        $uniqueRequiredScreenshot = $contained -and
            $requiredScreenshotPaths.Add($candidate) -and
            -not [string]::IsNullOrWhiteSpace($screenshotHash) -and
            $requiredScreenshotHashes.Add($screenshotHash)
    }
    $passed = $exists -and $nonempty -and $formatAllowed -and $uniqueRequiredScreenshot
    $evidenceText = if ($passed) {
        "$candidate length=$((Get-Item -LiteralPath $candidate -Force).Length)"
    } else {
        "Missing, empty, unsafe, unsupported, or reused evidence path '$RelativePath'."
    }
    Add-Check -Id $CheckId -Passed $passed -Evidence $evidenceText
    if ($passed) {
        Add-InventoryFile -Path $candidate -Role $Role
    }
}

foreach ($requiredPackageFile in @(
    "package-manifest.json", "package-contract.json", "SHA256SUMS.txt",
    "ProjectTempestDemo.exe", "mss32.dll"
)) {
    $path = Join-Path $packageRoot $requiredPackageFile
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Governed package file is unavailable: '$path'."
    }
}
if (-not (Test-Path -LiteralPath $ObservationsPath -PathType Leaf)) {
    throw "Manual observations file is unavailable: '$ObservationsPath'."
}

$summaryFiles = @(
    Get-ChildItem -LiteralPath $evidenceRoot -File -Filter "project-tempest-runtime-*-summary.json"
)
if ($summaryFiles.Count -ne 1) {
    throw "Expected exactly one runtime summary in the evidence root; found $($summaryFiles.Count)."
}

$strictUtf8 = [Text.UTF8Encoding]::new($false, $true)
$manifestPath = Join-Path $packageRoot "package-manifest.json"
$contractPath = Join-Path $packageRoot "package-contract.json"
$sumsPath = Join-Path $packageRoot "SHA256SUMS.txt"
$manifest = [IO.File]::ReadAllText($manifestPath, $strictUtf8) | ConvertFrom-Json
$contract = [IO.File]::ReadAllText($contractPath, $strictUtf8) | ConvertFrom-Json
$summaryPath = $summaryFiles[0].FullName
$summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
$observations = Get-Content -LiteralPath $ObservationsPath -Raw | ConvertFrom-Json
$traceName = [string]$summary.trace_file
if ([string]::IsNullOrWhiteSpace($traceName) -or [IO.Path]::IsPathRooted($traceName)) {
    throw "Runtime summary contains an unsafe trace path."
}
$tracePath = [IO.Path]::GetFullPath((Join-Path $evidenceRoot $traceName))
if (-not $tracePath.StartsWith($evidenceRootPrefix, $pathComparison)) {
    throw "Runtime summary trace escapes the evidence directory."
}
if (-not (Test-Path -LiteralPath $tracePath -PathType Leaf)) {
    throw "Runtime summary references a missing trace file: '$tracePath'."
}

$traceRecords = @(
    Get-Content -LiteralPath $tracePath |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { $_ | ConvertFrom-Json }
)
if ($traceRecords.Count -lt 2) {
    throw "Runtime trace does not contain a complete session."
}

foreach ($coreFile in @(
    @{ Path = $manifestPath; Role = "package_manifest" },
    @{ Path = $contractPath; Role = "package_contract" },
    @{ Path = $sumsPath; Role = "package_hash_manifest" },
    @{ Path = (Join-Path $packageRoot "ProjectTempestDemo.exe"); Role = "governed_executable" },
    @{ Path = (Join-Path $packageRoot "mss32.dll"); Role = "governed_runtime_dependency" },
    @{ Path = $summaryPath; Role = "runtime_summary" },
    @{ Path = $tracePath; Role = "runtime_trace" },
    @{ Path = $ObservationsPath; Role = "manual_observations" }
)) {
    Add-InventoryFile -Path $coreFile.Path -Role $coreFile.Role
}

$revisionPattern = '^[0-9a-f]{40}$'
$buildRevision = ([string]$manifest.source_revision).ToLowerInvariant()
$reviewedRevision = ([string]$manifest.reviewed_source_revision).ToLowerInvariant()
$expectedReviewedRevision = $ExpectedReviewedSourceRevision.ToLowerInvariant()
$expectedExecutableHash = $ExpectedExecutableSha256.ToLowerInvariant()
$expectedMilesHash = $ExpectedMilesSha256.ToLowerInvariant()
$expectedPackageContractHash = $ExpectedPackageContractSha256.ToLowerInvariant()
$expectedPackageManifestHash = $ExpectedPackageManifestSha256.ToLowerInvariant()
$expectedGovernedNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$expectedPackageNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$contractKinds = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
$contractShapeValid = $contract.schema_version -eq 3 -and
    [string]$contract.package_directory -eq [string]$manifest.package -and
    [string]$contract.archive_name -eq "ProjectTempestDemo-private.zip"
foreach ($entry in @($contract.runtime_files) + @($contract.repository_files)) {
    $name = [string]$entry.name
    $safeAndUnique = $name -match '^[^/\\:]+$' -and $expectedGovernedNames.Add($name)
    if (-not $safeAndUnique) {
        $contractShapeValid = $false
        continue
    }
    $null = $expectedPackageNames.Add($name)
    $contractKinds[$name] = [string]$entry.kind
}
$null = $expectedPackageNames.Add("package-manifest.json")
$null = $expectedPackageNames.Add("SHA256SUMS.txt")

$packageItems = @(Get-ChildItem -LiteralPath $packageRoot -Force)
$packageFiles = @($packageItems | Where-Object { -not $_.PSIsContainer })
$actualPackageNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$packageTreeValid = @($packageItems | Where-Object { $_.PSIsContainer }).Count -eq 0 -and
    $packageFiles.Count -eq $expectedPackageNames.Count
foreach ($file in $packageFiles) {
    if (-not $actualPackageNames.Add($file.Name) -or -not $expectedPackageNames.Contains($file.Name)) {
        $packageTreeValid = $false
    }
}
foreach ($name in $expectedPackageNames) {
    if (-not $actualPackageNames.Contains($name)) {
        $packageTreeValid = $false
    }
}

$manifestNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$manifestFilesValid = @($manifest.files).Count -eq $expectedGovernedNames.Count
foreach ($fileRecord in @($manifest.files)) {
    $name = [string]$fileRecord.name
    $path = Join-Path $packageRoot $name
    if (-not $expectedGovernedNames.Contains($name) -or -not $manifestNames.Add($name) -or
        -not (Test-Path -LiteralPath $path -PathType Leaf) -or
        [string]$fileRecord.kind -ne $contractKinds[$name] -or
        ([string]$fileRecord.sha256).ToLowerInvariant() -ne (Get-Sha256 -Path $path) -or
        [long]$fileRecord.length -ne (Get-Item -LiteralPath $path -Force).Length) {
        $manifestFilesValid = $false
    }
}
foreach ($name in $expectedGovernedNames) {
    if (-not $manifestNames.Contains($name)) {
        $manifestFilesValid = $false
    }
}

$sumNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$hashManifestValid = $true
$sumLines = @(
    [IO.File]::ReadAllText($sumsPath, $strictUtf8).TrimEnd("`r", "`n") -split "`r?`n" |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
)
foreach ($line in $sumLines) {
    if ($line -notmatch '^(?<hash>[0-9a-f]{64})  (?<name>[^/\\:]+)$') {
        $hashManifestValid = $false
        continue
    }
    $name = [string]$Matches.name
    $path = Join-Path $packageRoot $name
    if ($name -eq "SHA256SUMS.txt" -or -not $expectedPackageNames.Contains($name) -or
        -not $sumNames.Add($name) -or -not (Test-Path -LiteralPath $path -PathType Leaf) -or
        [string]$Matches.hash -ne (Get-Sha256 -Path $path)) {
        $hashManifestValid = $false
    }
}
foreach ($name in $expectedPackageNames) {
    if ($name -ne "SHA256SUMS.txt" -and -not $sumNames.Contains($name)) {
        $hashManifestValid = $false
    }
}

$actualContractHash = Get-Sha256 -Path $contractPath
$actualManifestHash = Get-Sha256 -Path $manifestPath
$contractHashValid = ([string]$manifest.package_contract_sha256).ToLowerInvariant() -eq $actualContractHash -and
    $actualContractHash -eq $expectedPackageContractHash
$manifestHashValid = $actualManifestHash -eq $expectedPackageManifestHash
$provenanceHashValid = $expectedGovernedNames.Contains("asset-provenance.json") -and
    (Test-Path -LiteralPath (Join-Path $packageRoot "asset-provenance.json") -PathType Leaf) -and
    ([string]$manifest.asset_provenance_sha256).ToLowerInvariant() -eq
        (Get-Sha256 -Path (Join-Path $packageRoot "asset-provenance.json"))
Add-Check "source.package_tree" ($contractShapeValid -and $packageTreeValid) `
    "contract_schema=$($contract.schema_version) files=$($packageFiles.Count)/$($expectedPackageNames.Count)"
Add-Check "source.package_manifest_files" $manifestFilesValid `
    "manifest_files=$(@($manifest.files).Count)/$($expectedGovernedNames.Count)"
Add-Check "source.package_hash_manifest" $hashManifestValid `
    "hash_records=$($sumNames.Count)/$($expectedPackageNames.Count - 1)"
Add-Check "source.package_metadata_hashes" ($manifestHashValid -and $contractHashValid -and $provenanceHashValid) `
    "manifest=$actualManifestHash expected_manifest=$expectedPackageManifestHash contract=$actualContractHash expected_contract=$expectedPackageContractHash provenance=$provenanceHashValid"
foreach ($name in $expectedGovernedNames) {
    $governedPath = Join-Path $packageRoot $name
    if (Test-Path -LiteralPath $governedPath -PathType Leaf) {
        Add-InventoryFile -Path $governedPath -Role "governed_package_file"
    }
}
Add-Check "source.package_contract" (
    $manifest.schema_version -eq 2 -and
    [string]$manifest.package -eq [string]$contract.package_directory -and
    [string]$manifest.distribution -eq "private_internal_demo" -and
    [string]$manifest.renderer_execution -eq "not_performed" -and
    $manifest.manual_playthrough_claimed -eq $false
) "schema=$($manifest.schema_version) distribution=$($manifest.distribution) renderer=$($manifest.renderer_execution)"
Add-Check "source.reviewed_revision" (
    $reviewedRevision -match $revisionPattern -and
    $buildRevision -match $revisionPattern -and
    $reviewedRevision -eq $expectedReviewedRevision -and
    ([string]$observations.source_revision).ToLowerInvariant() -eq $reviewedRevision
) "build=$buildRevision reviewed=$reviewedRevision expected_reviewed=$expectedReviewedRevision observations=$($observations.source_revision)"
Add-Check "source.clean_tree" ([string]$manifest.source_tree -eq "clean") "source_tree=$($manifest.source_tree)"
Add-Check "source.executable_provenance" (
    [string]$manifest.executable_verification.name -eq "ProjectTempestDemo.exe" -and
    [string]$manifest.executable_verification.policy -eq "two_isolated_integrated_release_builds_byte_identical" -and
    [string]$manifest.executable_verification.source_binding -eq "clean_build_revision_and_reviewed_head_and_governed_integrated_release_output" -and
    ([string]$manifest.executable_verification.source_revision).ToLowerInvariant() -eq $buildRevision -and
    ([string]$manifest.executable_verification.reviewed_source_revision).ToLowerInvariant() -eq $reviewedRevision -and
    [string]$manifest.executable_verification.runtime_input_policy -eq "governed_integrated_release_outputs_only"
) "policy=$($manifest.executable_verification.policy) source=$($manifest.executable_verification.source_revision)"
Add-Check "source.executable_hash" (
    (Get-Sha256 (Join-Path $packageRoot "ProjectTempestDemo.exe")) -eq
        ([string]$manifest.executable_verification.sha256).ToLowerInvariant() -and
    (Get-Sha256 (Join-Path $packageRoot "ProjectTempestDemo.exe")) -eq $expectedExecutableHash
) "manifest=$($manifest.executable_verification.sha256) external=$expectedExecutableHash"
Add-Check "source.runtime_dependency_hash" (
    (Get-Sha256 (Join-Path $packageRoot "mss32.dll")) -eq
        ([string]$manifest.runtime_dependency_verification.sha256).ToLowerInvariant() -and
    (Get-Sha256 (Join-Path $packageRoot "mss32.dll")) -eq $expectedMilesHash
) "manifest=$($manifest.runtime_dependency_verification.sha256) external=$expectedMilesHash"
Add-Check "source.runtime_dependency_provenance" (
    [string]$manifest.runtime_dependency_verification.name -eq "mss32.dll" -and
    [string]$manifest.runtime_dependency_verification.policy -eq "two_isolated_integrated_release_builds_byte_identical"
) "policy=$($manifest.runtime_dependency_verification.policy)"

Add-Check "runtime.schema" ($summary.schema_version -eq 1 -and $observations.schema_version -eq 1) `
    "summary=$($summary.schema_version) observations=$($observations.schema_version)"
Add-Check "runtime.mode" ([string]$summary.mode -eq "user_initiated_runtime_evidence") "mode=$($summary.mode)"
Add-Check "runtime.no_automatic_playthrough_claim" ($summary.manual_playthrough_claimed -eq $false) `
    "manual_playthrough_claimed=$($summary.manual_playthrough_claimed)"
Add-Check "runtime.clean_shutdown" ($summary.clean_shutdown -eq $true -and $summary.exit_code -eq 0) `
    "clean_shutdown=$($summary.clean_shutdown) exit_code=$($summary.exit_code)"
Add-Check "runtime.minimum_30_minutes" ($summary.duration_ms -ge 1800000) "duration_ms=$($summary.duration_ms)"
Add-Check "runtime.frames_recorded" ($summary.frames -gt 0 -and $summary.frame_windows -gt 0) `
    "frames=$($summary.frames) windows=$($summary.frame_windows)"
Add-Check "runtime.bounded_capture" (
    $summary.frame_windows_dropped -eq 0 -and
    $summary.event_entries_dropped -eq 0 -and
    $summary.resolution_entries_dropped -eq 0 -and
    $summary.outcome_entries_dropped -eq 0
) "window/event/resolution/outcome drops=$($summary.frame_windows_dropped)/$($summary.event_entries_dropped)/$($summary.resolution_entries_dropped)/$($summary.outcome_entries_dropped)"
$workingSetStart = [double]$summary.working_set_bytes.start
$workingSetEnd = [double]$summary.working_set_bytes.end
$growthAllowance = [Math]::Max(67108864.0, $workingSetStart * 0.25)
Add-Check "performance.bounded_working_set" (
    $workingSetStart -gt 0 -and $workingSetEnd -gt 0 -and
    ($workingSetEnd - $workingSetStart) -le $growthAllowance
) "start=$workingSetStart end=$workingSetEnd peak=$($summary.working_set_bytes.peak) allowance=$growthAllowance"

$requiredResolutions = @("1920x1080", "2560x1440", "3840x2160")
$recordedResolutions = @($summary.resolutions | ForEach-Object { [string]$_ })
$runtimeResolutionPass = @($requiredResolutions | Where-Object { $_ -notin $recordedResolutions }).Count -eq 0 -and
    @(@("3440x1440", "2560x1080") | Where-Object { $_ -in $recordedResolutions }).Count -gt 0
Add-Check "runtime.target_resolutions" $runtimeResolutionPass "recorded=$($recordedResolutions -join ',')"
Add-Check "runtime.alt_tab" ($summary.focus_losses -ge 1) "focus_losses=$($summary.focus_losses)"
Add-Check "runtime.repeated_restart" ($summary.restarts -ge 2) "restarts=$($summary.restarts)"
$runtimeOutcomes = @($summary.outcomes | ForEach-Object { ([string]$_).ToLowerInvariant() })
Add-Check "runtime.two_outcomes" (
    $runtimeOutcomes.Count -ge 2 -and "victory" -in $runtimeOutcomes -and "defeat" -in $runtimeOutcomes
) "outcomes=$($runtimeOutcomes -join ',')"

$traceStart = $traceRecords[0]
$traceEnd = $traceRecords[-1]
$traceEvents = @($traceRecords | Where-Object { [string]$_.type -eq "event" })
$traceWindows = @($traceRecords | Where-Object { [string]$_.type -eq "frame_window" })
Add-Check "trace.session_boundary" (
    [string]$traceStart.type -eq "session_start" -and
    [string]$traceEnd.type -eq "session_end" -and
    [string]$traceStart.session_id -eq [string]$summary.session_id -and
    $traceEnd.clean_shutdown -eq $true -and $traceEnd.exit_code -eq 0
) "start=$($traceStart.type) end=$($traceEnd.type) session=$($summary.session_id)"
Add-Check "trace.window_count" ($traceWindows.Count -eq $summary.frame_windows) `
    "trace_windows=$($traceWindows.Count) summary_windows=$($summary.frame_windows)"

$traceFrameTotal = [uint64](($traceWindows | Measure-Object -Property frames -Sum).Sum)
$weightedFrameMsTotal = 0.0
foreach ($window in $traceWindows) {
    $weightedFrameMsTotal += [double]$window.frame_ms.average * [double]$window.frames
}
$traceAverageFrameMs = if ($traceFrameTotal -gt 0) { $weightedFrameMsTotal / $traceFrameTotal } else { 0.0 }
$traceMinimumFrameMs = [double](($traceWindows | ForEach-Object { [double]$_.frame_ms.min } | Measure-Object -Minimum).Minimum)
$traceMaximumFrameMs = [double](($traceWindows | ForEach-Object { [double]$_.frame_ms.max } | Measure-Object -Maximum).Maximum)
$traceMaximumP95Ms = [double](($traceWindows | ForEach-Object { [double]$_.frame_ms.p95 } | Measure-Object -Maximum).Maximum)
$traceMaximumP99Ms = [double](($traceWindows | ForEach-Object { [double]$_.frame_ms.p99 } | Measure-Object -Maximum).Maximum)
$traceWorkingSets = @($traceWindows | ForEach-Object { [uint64]$_.working_set_bytes } | Where-Object { $_ -gt 0 })
$traceWorkingSetStart = if ($traceWorkingSets.Count -gt 0) { $traceWorkingSets[0] } else { 0 }
$traceWorkingSetEnd = if ($traceWorkingSets.Count -gt 0) { $traceWorkingSets[-1] } else { 0 }
$traceWorkingSetPeak = if ($traceWorkingSets.Count -gt 0) {
    [uint64](($traceWorkingSets | Measure-Object -Maximum).Maximum)
} else { 0 }
$traceCoveredWindowMs = [uint64]0
$traceWindowGapMs = [uint64]0
$traceLargestWindowMs = [uint64]0
$traceWindowsWellFormed = $traceWindows.Count -gt 0
$previousWindowEndMs = $null
foreach ($window in $traceWindows) {
    $windowStartMs = [uint64]$window.start_ms
    $windowEndMs = [uint64]$window.end_ms
    if ($windowEndMs -le $windowStartMs) {
        $traceWindowsWellFormed = $false
        continue
    }
    if ($windowEndMs -gt [uint64]$summary.duration_ms) {
        $traceWindowsWellFormed = $false
    }
    if ([uint64]$window.frames -eq 0 -or [uint64]$window.active_frames -gt [uint64]$window.frames) {
        $traceWindowsWellFormed = $false
    }
    $windowDurationMs = $windowEndMs - $windowStartMs
    $traceCoveredWindowMs += $windowDurationMs
    $traceLargestWindowMs = [Math]::Max($traceLargestWindowMs, $windowDurationMs)
    if ($null -ne $previousWindowEndMs) {
        if ($windowStartMs -lt $previousWindowEndMs) {
            $traceWindowsWellFormed = $false
        } else {
            $traceWindowGapMs += $windowStartMs - $previousWindowEndMs
        }
    }
    $previousWindowEndMs = $windowEndMs
}
$minimumTraceWindowCount = [uint64][Math]::Floor([double]$summary.duration_ms / 2000.0)
$active1080pTraceWindows = @($traceWindows | Where-Object {
    [uint64]$_.active_frames -gt 0 -and [int]$_.width -eq 1920 -and [int]$_.height -eq 1080
})
$target1080pTraceWindows = @($active1080pTraceWindows | Where-Object { [double]$_.frame_ms.p95 -le 17.0 })
$target1080pTraceWindowRatio = if ($active1080pTraceWindows.Count -gt 0) {
    [double]$target1080pTraceWindows.Count / [double]$active1080pTraceWindows.Count
} else { 0.0 }
$trace1080pCoveredMs = [uint64](($active1080pTraceWindows | ForEach-Object {
    [uint64]$_.end_ms - [uint64]$_.start_ms
} | Measure-Object -Sum).Sum)
$trace1080pActiveFrames = [uint64](($active1080pTraceWindows | Measure-Object -Property active_frames -Sum).Sum)
$trace1080pFrameRate = if ($trace1080pCoveredMs -gt 0) {
    [double]$trace1080pActiveFrames * 1000.0 / [double]$trace1080pCoveredMs
} else { 0.0 }
$trace1080pWeightedFrameMs = 0.0
foreach ($window in $active1080pTraceWindows) {
    $trace1080pWeightedFrameMs += [double]$window.frame_ms.average * [double]$window.active_frames
}
$trace1080pAverageFrameMs = if ($trace1080pActiveFrames -gt 0) {
    $trace1080pWeightedFrameMs / [double]$trace1080pActiveFrames
} else { 0.0 }

$traceResolutions = [Collections.Generic.List[string]]::new()
$traceOutcomes = [Collections.Generic.List[string]]::new()
foreach ($event in $traceEvents) {
    if ([string]$event.name -eq "resolution" -and -not $traceResolutions.Contains([string]$event.detail)) {
        $traceResolutions.Add([string]$event.detail)
    }
    if ([string]$event.name -eq "outcome") {
        $traceOutcomes.Add(([string]$event.detail).ToLowerInvariant())
    }
}
$traceFocusLosses = @($traceEvents | Where-Object { [string]$_.name -eq "focus_lost" }).Count
$traceRestarts = @($traceEvents | Where-Object { [string]$_.name -eq "restart" }).Count

Add-Check "trace.summary_totals" (
    [uint64]$traceEnd.elapsed_ms -eq [uint64]$summary.duration_ms -and
    $traceFrameTotal -eq [uint64]$summary.frames -and
    $traceEvents.Count -eq [uint64]$summary.events
) "elapsed=$($traceEnd.elapsed_ms)/$($summary.duration_ms) frames=$traceFrameTotal/$($summary.frames) events=$($traceEvents.Count)/$($summary.events)"
Add-Check "trace.summary_lifecycle" (
    $traceFocusLosses -eq [uint64]$summary.focus_losses -and
    $traceRestarts -eq [uint64]$summary.restarts -and
    ($traceResolutions -join '|') -eq ($recordedResolutions -join '|') -and
    ($traceOutcomes -join '|') -eq ($runtimeOutcomes -join '|')
) "focus=$traceFocusLosses/$($summary.focus_losses) restart=$traceRestarts/$($summary.restarts) resolutions=$($traceResolutions -join ',') outcomes=$($traceOutcomes -join ',')"
Add-Check "trace.summary_frame_statistics" (
    [Math]::Abs($traceAverageFrameMs - [double]$summary.frame_ms.average) -le 0.1 -and
    [Math]::Abs($traceMinimumFrameMs - [double]$summary.frame_ms.min) -le 0.1 -and
    [Math]::Abs($traceMaximumFrameMs - [double]$summary.frame_ms.max) -le 0.1 -and
    [double]$summary.frame_ms.p95 -le $traceMaximumP95Ms + 0.1 -and
    [double]$summary.frame_ms.p99 -le $traceMaximumP99Ms + 0.1
) "average=$traceAverageFrameMs/$($summary.frame_ms.average) min=$traceMinimumFrameMs/$($summary.frame_ms.min) max=$traceMaximumFrameMs/$($summary.frame_ms.max)"
Add-Check "trace.summary_working_set" (
    $traceWorkingSetEnd -eq [uint64]$summary.working_set_bytes.end -and
    [uint64]$summary.working_set_bytes.start -gt 0 -and
    [uint64]$summary.working_set_bytes.peak -ge $traceWorkingSetPeak -and
    [uint64]$summary.working_set_bytes.peak -ge [uint64]$summary.working_set_bytes.start -and
    [Math]::Abs([double]$traceWorkingSetStart - [double]$summary.working_set_bytes.start) -le 67108864.0 -and
    ([double]$summary.working_set_bytes.peak - [double]$traceWorkingSetPeak) -le 67108864.0
) "start=$traceWorkingSetStart/$($summary.working_set_bytes.start) end=$traceWorkingSetEnd/$($summary.working_set_bytes.end) peak=$traceWorkingSetPeak/$($summary.working_set_bytes.peak) tolerance=67108864"
Add-Check "trace.window_continuity" (
    $traceWindowsWellFormed -and
    $traceLargestWindowMs -le 2000 -and
    $traceWindows.Count -ge $minimumTraceWindowCount -and
    $traceCoveredWindowMs -ge [uint64]([double]$summary.duration_ms * 0.95) -and
    $traceWindowGapMs -le [uint64]([double]$summary.duration_ms * 0.05)
) "covered_ms=$traceCoveredWindowMs gaps_ms=$traceWindowGapMs largest_window_ms=$traceLargestWindowMs windows=$($traceWindows.Count)/$minimumTraceWindowCount duration_ms=$($summary.duration_ms)"
Add-Check "performance.trace_1080p_60fps_target" (
    $trace1080pCoveredMs -ge 60000 -and
    $trace1080pFrameRate -ge 58.0 -and
    $trace1080pAverageFrameMs -le 17.0 -and
    $target1080pTraceWindowRatio -ge 0.95
) "1080p_covered_ms=$trace1080pCoveredMs active_frames=$trace1080pActiveFrames fps=$trace1080pFrameRate weighted_average_ms=$trace1080pAverageFrameMs target_windows=$($target1080pTraceWindows.Count)/$($active1080pTraceWindows.Count) ratio=$target1080pTraceWindowRatio"

$attestationFields = @("user_started_demo", "non_rdp_desktop", "automatic_retry_disabled", "observations_are_truthful")
$attestationPass = @($attestationFields | Where-Object {
    -not (Test-TrueProperty -Object $observations.tester_attestation -Name $_)
}).Count -eq 0
Add-Check "manual.attestation" $attestationPass "required=$($attestationFields -join ',')"

$playthroughs = @($observations.playthroughs)
$requiredPlaythroughFields = @(
    "full_match", "objective_understood", "resource_identified", "first_command_issued",
    "territory_captured", "structure_built", "unit_produced", "faction_ability_used", "result_understood"
)
$playthroughPass = $playthroughs.Count -ge 2 -and
    @($playthroughs | Where-Object {
        $playthrough = $_
        @($requiredPlaythroughFields | Where-Object {
            -not (Test-TrueProperty -Object $playthrough -Name $_)
        }).Count -gt 0 -or
        [string]::IsNullOrWhiteSpace([string]$playthrough.notes) -or
        [string]$playthrough.notes -match "Replace with truthful observations"
    }).Count -eq 0
$manualOutcomes = @($playthroughs | ForEach-Object { ([string]$_.outcome).ToLowerInvariant() })
$playthroughPass = $playthroughPass -and "victory" -in $manualOutcomes -and "defeat" -in $manualOutcomes
Add-Check "manual.two_full_playthroughs" $playthroughPass "count=$($playthroughs.Count) outcomes=$($manualOutcomes -join ',')"

$resolutionChecks = @($observations.resolution_checks)
foreach ($resolution in $requiredResolutions) {
    $entry = @($resolutionChecks | Where-Object { [string]$_.resolution -eq $resolution })
    $passed = $entry.Count -eq 1 -and
        (Test-TrueProperty $entry[0] "no_clipping") -and
        (Test-TrueProperty $entry[0] "controls_readable")
    Add-Check "manual.resolution.$resolution" $passed "required readable unclipped screenshot"
    if ($entry.Count -eq 1) {
        Add-RequiredEvidenceFile ([string]$entry[0].screenshot) "resolution_screenshot" "artifact.resolution.$resolution"
    }
}
$wideEntries = @($resolutionChecks | Where-Object { [string]$_.resolution -in @("3440x1440", "2560x1080") })
$widePass = $wideEntries.Count -eq 1 -and
    (Test-TrueProperty $wideEntries[0] "no_clipping") -and
    (Test-TrueProperty $wideEntries[0] "controls_readable")
Add-Check "manual.resolution.21x9" $widePass "required readable unclipped 21:9 screenshot"
if ($wideEntries.Count -eq 1) {
    Add-RequiredEvidenceFile ([string]$wideEntries[0].screenshot) "resolution_screenshot" "artifact.resolution.21x9"
}

foreach ($mode in @("off", "protanopia", "deuteranopia", "tritanopia", "partial")) {
    $entry = @($observations.accessibility_checks | Where-Object { [string]$_.mode -eq $mode })
    $distinctPass = $entry.Count -eq 1 -and (
        $mode -eq "off" -or (Test-TrueProperty $entry[0] "distinct_from_off")
    )
    $passed = $entry.Count -eq 1 -and
        (Test-TrueProperty $entry[0] "world_readable") -and
        (Test-TrueProperty $entry[0] "ui_readable") -and
        $distinctPass
    Add-Check "manual.accessibility.$mode" $passed "required distinct readable world/UI mode"
    if ($entry.Count -eq 1) {
        Add-RequiredEvidenceFile ([string]$entry[0].screenshot) "accessibility_screenshot" "artifact.accessibility.$mode"
    }
}

$controlFields = @(
    "mouse_remap_persisted", "keyboard_remap_persisted", "edge_scroll_can_be_disabled",
    "camera_speed_works", "ui_scale_works", "pause_works", "reduced_flashes_and_shake_works",
    "ownership_readable_without_colour", "unit_silhouettes_readable", "threats_readable_during_combat",
    "visible_command_feedback_immediate"
)
$controlPass = @($controlFields | Where-Object {
    -not (Test-TrueProperty -Object $observations.controls_and_usability -Name $_)
}).Count -eq 0
Add-Check "manual.controls_and_usability" $controlPass "required=$($controlFields -join ',')"

$audioFields = @(
    "music_audible_and_balanced", "effects_audible_and_balanced", "command_feedback_immediate",
    "alerts_distinct", "volume_controls_work", "no_clipping_or_obvious_loop_fault"
)
$audioPass = @($audioFields | Where-Object {
    -not (Test-TrueProperty -Object $observations.audio -Name $_)
}).Count -eq 0
Add-Check "manual.audio" $audioPass "required=$($audioFields -join ',')"

$stabilityFields = @(
    "alt_tab_returned_cleanly", "resolution_change_returned_cleanly", "restart_returned_cleanly",
    "clean_shutdown_observed", "no_crash_or_assert", "no_observed_unbounded_growth"
)
$stabilityPass = @($stabilityFields | Where-Object {
    -not (Test-TrueProperty -Object $observations.stability -Name $_)
}).Count -eq 0
Add-Check "manual.stability" $stabilityPass "required=$($stabilityFields -join ',')"

Add-RequiredEvidenceFile ([string]$observations.evidence_capture.short_capture) `
    "short_capture" "artifact.short_capture"
Add-RequiredEvidenceFile ([string]$observations.evidence_capture.runtime_log) `
    "runtime_log" "artifact.runtime_log"
Add-RequiredEvidenceFile ([string]$observations.evidence_capture.settings_screenshot) `
    "settings_screenshot" "artifact.settings_screenshot"

$failedChecks = @($checks | Where-Object { -not $_.passed })
$report = [ordered]@{
    schema_version = 1
    result = if ($failedChecks.Count -eq 0) { "pass" } else { "fail" }
    build_source_revision = $buildRevision
    reviewed_source_revision = $reviewedRevision
    session_id = [string]$summary.session_id
    checks_total = $checks.Count
    checks_failed = $failedChecks.Count
    checks = @($checks)
    measured = [ordered]@{
        duration_ms = $summary.duration_ms
        frames = $summary.frames
        frame_ms = $summary.frame_ms
        histogram_saturated_frames_ge_1000ms = $summary.histogram_saturated_frames_ge_1000ms
        working_set_bytes = $summary.working_set_bytes
        focus_losses = $summary.focus_losses
        restarts = $summary.restarts
        resolutions = @($summary.resolutions)
        outcomes = @($summary.outcomes)
    }
    known_issues = @($observations.known_issues)
    evidence_inventory = @($inventory | Sort-Object path -Unique)
    renderer_execution_by_analyser = "not_performed"
    automatic_retry = $false
}

$report | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $resolvedReportPath -Encoding UTF8
Write-Host "Manual acceptance report: $resolvedReportPath"
if ($failedChecks.Count -gt 0) {
    throw "Project Tempest manual acceptance failed $($failedChecks.Count) of $($checks.Count) checks: $($failedChecks.id -join ', ')."
}

Write-Host "PASS: Project Tempest one-pass manual acceptance evidence"
Write-Host "Reviewed source revision: $reviewedRevision"
Write-Host "Evidence files hashed: $($inventory.Count)"
