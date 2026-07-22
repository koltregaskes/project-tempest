[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceDirectory,

    [string]$ObservationsPath,

    [string]$PackageDirectory = $PSScriptRoot,

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

if (-not $ObservationsPath) {
    $ObservationsPath = Join-Path $evidenceRoot "manual-acceptance-observations.json"
}
$ObservationsPath = [IO.Path]::GetFullPath($ObservationsPath)
if (-not $ObservationsPath.StartsWith($evidenceRootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "The manual observations file must remain inside the evidence directory."
}
if (-not $ReportPath) {
    $ReportPath = Join-Path $evidenceRoot "project-tempest-manual-acceptance-report.json"
}
$resolvedReportPath = [IO.Path]::GetFullPath($ReportPath)
if (-not $resolvedReportPath.StartsWith($evidenceRootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
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
        Get-ChildItem -LiteralPath $packageRoot -Force
    ) | Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 }
)
if ($packageReparseEntries.Count -gt 0) {
    throw "Governed package inputs reject reparse points: $($packageReparseEntries.FullName -join ', ')."
}

$checks = [Collections.Generic.List[object]]::new()
$inventory = [Collections.Generic.List[object]]::new()

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
        $candidate.StartsWith($evidenceRootPrefix, [StringComparison]::OrdinalIgnoreCase)
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
    $passed = $exists -and $nonempty -and $formatAllowed
    $evidenceText = if ($passed) {
        "$candidate length=$((Get-Item -LiteralPath $candidate -Force).Length)"
    } else {
        "Missing, empty, unsafe, or unsupported evidence path '$RelativePath'."
    }
    Add-Check -Id $CheckId -Passed $passed -Evidence $evidenceText
    if ($passed) {
        Add-InventoryFile -Path $candidate -Role $Role
    }
}

foreach ($requiredPackageFile in @("package-manifest.json", "ProjectTempestDemo.exe", "mss32.dll")) {
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

$manifestPath = Join-Path $packageRoot "package-manifest.json"
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$summaryPath = $summaryFiles[0].FullName
$summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
$observations = Get-Content -LiteralPath $ObservationsPath -Raw | ConvertFrom-Json
$traceName = [string]$summary.trace_file
if ([string]::IsNullOrWhiteSpace($traceName) -or [IO.Path]::IsPathRooted($traceName)) {
    throw "Runtime summary contains an unsafe trace path."
}
$tracePath = [IO.Path]::GetFullPath((Join-Path $evidenceRoot $traceName))
if (-not $tracePath.StartsWith($evidenceRootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
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
    @{ Path = (Join-Path $packageRoot "ProjectTempestDemo.exe"); Role = "governed_executable" },
    @{ Path = (Join-Path $packageRoot "mss32.dll"); Role = "governed_runtime_dependency" },
    @{ Path = $summaryPath; Role = "runtime_summary" },
    @{ Path = $tracePath; Role = "runtime_trace" },
    @{ Path = $ObservationsPath; Role = "manual_observations" }
)) {
    Add-InventoryFile -Path $coreFile.Path -Role $coreFile.Role
}

$revisionPattern = '^[0-9a-f]{40}$'
$reviewedRevision = ([string]$manifest.reviewed_source_revision).ToLowerInvariant()
Add-Check "source.package_contract" (
    $manifest.schema_version -eq 2 -and
    [string]$manifest.distribution -eq "private_internal_demo" -and
    [string]$manifest.renderer_execution -eq "not_performed" -and
    $manifest.manual_playthrough_claimed -eq $false
) "schema=$($manifest.schema_version) distribution=$($manifest.distribution) renderer=$($manifest.renderer_execution)"
Add-Check "source.reviewed_revision" (
    $reviewedRevision -match $revisionPattern -and
    ([string]$manifest.source_revision).ToLowerInvariant() -eq $reviewedRevision -and
    ([string]$observations.source_revision).ToLowerInvariant() -eq $reviewedRevision
) "manifest=$reviewedRevision observations=$($observations.source_revision)"
Add-Check "source.clean_tree" ([string]$manifest.source_tree -eq "clean") "source_tree=$($manifest.source_tree)"
Add-Check "source.executable_provenance" (
    [string]$manifest.executable_verification.name -eq "ProjectTempestDemo.exe" -and
    [string]$manifest.executable_verification.policy -eq "two_isolated_integrated_release_builds_byte_identical" -and
    [string]$manifest.executable_verification.source_binding -eq "clean_build_revision_and_reviewed_head_and_governed_integrated_release_output" -and
    ([string]$manifest.executable_verification.source_revision).ToLowerInvariant() -eq $reviewedRevision -and
    ([string]$manifest.executable_verification.reviewed_source_revision).ToLowerInvariant() -eq $reviewedRevision -and
    [string]$manifest.executable_verification.runtime_input_policy -eq "governed_integrated_release_outputs_only"
) "policy=$($manifest.executable_verification.policy) source=$($manifest.executable_verification.source_revision)"
Add-Check "source.executable_hash" (
    (Get-Sha256 (Join-Path $packageRoot "ProjectTempestDemo.exe")) -eq
        ([string]$manifest.executable_verification.sha256).ToLowerInvariant()
) "expected=$($manifest.executable_verification.sha256)"
Add-Check "source.runtime_dependency_hash" (
    (Get-Sha256 (Join-Path $packageRoot "mss32.dll")) -eq
        ([string]$manifest.runtime_dependency_verification.sha256).ToLowerInvariant()
) "expected=$($manifest.runtime_dependency_verification.sha256)"
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
Add-Check "performance.1080p_60fps_target" (
    [double]$summary.frame_ms.average -le 17.0 -and [double]$summary.frame_ms.p95 -le 17.0
) "target_ms=17.0 average_ms=$($summary.frame_ms.average) p95_ms=$($summary.frame_ms.p95) p99_ms=$($summary.frame_ms.p99) max_ms=$($summary.frame_ms.max) saturated_ge_1000ms=$($summary.histogram_saturated_frames_ge_1000ms)"

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
Add-Check "runtime.restart" ($summary.restarts -ge 1) "restarts=$($summary.restarts)"
$runtimeOutcomes = @($summary.outcomes | ForEach-Object { ([string]$_).ToLowerInvariant() })
Add-Check "runtime.two_outcomes" (
    $runtimeOutcomes.Count -ge 2 -and "victory" -in $runtimeOutcomes -and "defeat" -in $runtimeOutcomes
) "outcomes=$($runtimeOutcomes -join ',')"

$traceStart = $traceRecords[0]
$traceEnd = $traceRecords[-1]
$traceWindows = @($traceRecords | Where-Object { [string]$_.type -eq "frame_window" })
Add-Check "trace.session_boundary" (
    [string]$traceStart.type -eq "session_start" -and
    [string]$traceEnd.type -eq "session_end" -and
    [string]$traceStart.session_id -eq [string]$summary.session_id -and
    $traceEnd.clean_shutdown -eq $true -and $traceEnd.exit_code -eq 0
) "start=$($traceStart.type) end=$($traceEnd.type) session=$($summary.session_id)"
Add-Check "trace.window_count" ($traceWindows.Count -eq $summary.frame_windows) `
    "trace_windows=$($traceWindows.Count) summary_windows=$($summary.frame_windows)"

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
