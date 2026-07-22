[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Expect {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw "Manual evidence fixture failed: $Message"
    }
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = Join-Path $repositoryRoot "build"
$sessionRoot = Join-Path $buildRoot ("manual-acceptance-test/" + [guid]::NewGuid().ToString("N"))
$packageRoot = Join-Path $sessionRoot "package"
$evidenceRoot = Join-Path $sessionRoot "evidence"
$revision = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
New-Item -ItemType Directory -Path $packageRoot, $evidenceRoot | Out-Null

try {
    $executablePath = Join-Path $packageRoot "ProjectTempestDemo.exe"
    $milesPath = Join-Path $packageRoot "mss32.dll"
    [IO.File]::WriteAllBytes($executablePath, [byte[]](77, 90, 1, 2, 3, 4))
    [IO.File]::WriteAllBytes($milesPath, [byte[]](77, 90, 5, 6, 7, 8))
    $executableHash = (Get-FileHash -LiteralPath $executablePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $milesHash = (Get-FileHash -LiteralPath $milesPath -Algorithm SHA256).Hash.ToLowerInvariant()
    [ordered]@{
        schema_version = 2
        distribution = "private_internal_demo"
        source_revision = $revision
        reviewed_source_revision = $revision
        source_tree = "clean"
        executable_verification = [ordered]@{
            name = "ProjectTempestDemo.exe"
            sha256 = $executableHash
            policy = "two_isolated_integrated_release_builds_byte_identical"
            source_binding = "clean_build_revision_and_reviewed_head_and_governed_integrated_release_output"
            source_revision = $revision
            reviewed_source_revision = $revision
            runtime_input_policy = "governed_integrated_release_outputs_only"
        }
        runtime_dependency_verification = [ordered]@{
            name = "mss32.dll"
            sha256 = $milesHash
            policy = "two_isolated_integrated_release_builds_byte_identical"
        }
        renderer_execution = "not_performed"
        manual_playthrough_claimed = $false
    } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $packageRoot "package-manifest.json") -Encoding UTF8

    $traceName = "project-tempest-runtime-fixture.jsonl"
    $summaryName = "project-tempest-runtime-fixture-summary.json"
    @(
        '{"schema_version":1,"type":"session_start","session_id":"fixture","started_unix_ms":1}',
        '{"schema_version":1,"type":"frame_window","start_ms":0,"end_ms":1000,"frames":60,"active_frames":60,"frame_ms":{"average":16.0,"min":15.0,"p95":16.0,"p99":16.5,"max":16.6},"last_simulation_tick":20,"width":1920,"height":1080,"working_set_bytes":110000000}',
        '{"schema_version":1,"type":"session_end","elapsed_ms":1800000,"exit_code":0,"clean_shutdown":true}'
    ) | Set-Content -LiteralPath (Join-Path $evidenceRoot $traceName) -Encoding UTF8
    [ordered]@{
        schema_version = 1
        mode = "user_initiated_runtime_evidence"
        manual_playthrough_claimed = $false
        session_id = "fixture"
        started_unix_ms = 1
        duration_ms = 1800000
        exit_code = 0
        clean_shutdown = $true
        frames = 108000
        frame_windows = 1
        frame_windows_dropped = 0
        percentile_resolution_ms = 0.1
        histogram_saturated_frames_ge_1000ms = 1
        frame_ms = [ordered]@{ min = 10.0; average = 16.0; p50 = 16.0; p95 = 16.6; p99 = 20.0; max = 1500.0 }
        working_set_bytes = [ordered]@{ start = 100000000; end = 110000000; peak = 140000000 }
        events = 12
        event_entries_dropped = 0
        focus_losses = 1
        restarts = 1
        resolutions = @("1920x1080", "2560x1440", "3840x2160", "3440x1440")
        resolution_entries_dropped = 0
        outcomes = @("victory", "defeat")
        outcome_entries_dropped = 0
        trace_file = $traceName
    } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $evidenceRoot $summaryName) -Encoding UTF8

    $observations = Get-Content -LiteralPath (
        Join-Path $repositoryRoot "ProjectTempest/manual-acceptance-observations.example.json") -Raw | ConvertFrom-Json
    $observations.source_revision = $revision
    foreach ($name in @("user_started_demo", "non_rdp_desktop", "automatic_retry_disabled", "observations_are_truthful")) {
        $observations.tester_attestation.$name = $true
    }
    foreach ($playthrough in @($observations.playthroughs)) {
        foreach ($name in @(
            "full_match", "objective_understood", "resource_identified", "first_command_issued",
            "territory_captured", "structure_built", "unit_produced", "faction_ability_used", "result_understood"
        )) {
            $playthrough.$name = $true
        }
        $playthrough.notes = "Fixture observation for $($playthrough.id)."
    }
    foreach ($entry in @($observations.resolution_checks)) {
        $entry.no_clipping = $true
        $entry.controls_readable = $true
    }
    foreach ($entry in @($observations.accessibility_checks)) {
        $entry.world_readable = $true
        $entry.ui_readable = $true
        $entry.distinct_from_off = $true
    }
    foreach ($name in @($observations.controls_and_usability.PSObject.Properties.Name)) {
        $observations.controls_and_usability.$name = $true
    }
    foreach ($name in @($observations.audio.PSObject.Properties.Name)) {
        $observations.audio.$name = $true
    }
    foreach ($name in @($observations.stability.PSObject.Properties.Name)) {
        $observations.stability.$name = $true
    }

    $evidencePaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($observations.resolution_checks) + @($observations.accessibility_checks)) {
        $null = $evidencePaths.Add([string]$entry.screenshot)
    }
    foreach ($name in @("short_capture", "runtime_log", "settings_screenshot")) {
        $null = $evidencePaths.Add([string]$observations.evidence_capture.$name)
    }
    foreach ($relativePath in $evidencePaths) {
        $path = Join-Path $evidenceRoot $relativePath
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
        [IO.File]::WriteAllBytes($path, [Text.Encoding]::UTF8.GetBytes("fixture:$relativePath"))
    }

    $observationsPath = Join-Path $evidenceRoot "manual-acceptance-observations.json"
    $observations | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $observationsPath -Encoding UTF8

    $ciWorkflow = Get-Content -LiteralPath (Join-Path $repositoryRoot ".github/workflows/ci.yml") -Raw
    foreach ($requiredCiMarker in @(
        "scripts/analyse-project-tempest-manual-evidence.ps1",
        "scripts/test-project-tempest-manual-evidence.ps1",
        "./scripts/test-project-tempest-manual-evidence.ps1"
    )) {
        Expect ($ciWorkflow -match [regex]::Escape($requiredCiMarker)) "CI does not route and invoke '$requiredCiMarker'"
    }

    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 `
            -EvidenceDirectory $evidenceRoot `
            -PackageDirectory $packageRoot
    }
    finally {
        Pop-Location
    }
    $reportPath = Join-Path $evidenceRoot "project-tempest-manual-acceptance-report.json"
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    Expect ($report.result -eq "pass") "complete fixture did not pass"
    Expect ($report.checks_failed -eq 0) "complete fixture retained failed checks"
    Expect (@($report.evidence_inventory).Count -ge 18) "complete fixture did not hash the governed evidence set"
    Expect ($report.renderer_execution_by_analyser -eq "not_performed") "analyser did not preserve the no-execution claim"

    $summaryPath = Join-Path $evidenceRoot $summaryName
    $invalidSummary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
    $invalidSummary.duration_ms = 1799999
    $invalidSummary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    $rejected = $false
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 `
            -EvidenceDirectory $evidenceRoot `
            -PackageDirectory $packageRoot
    }
    catch {
        $rejected = $_.Exception.Message -match "runtime.minimum_30_minutes"
    }
    finally {
        Pop-Location
    }
    Expect $rejected "short-session fixture was not rejected by the production analyser"

    $failedReport = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    Expect ($failedReport.result -eq "fail") "negative fixture did not produce a failure report"
    Expect (@($failedReport.checks | Where-Object { $_.id -eq "runtime.minimum_30_minutes" -and -not $_.passed }).Count -eq 1) `
        "negative report did not preserve the exact failed criterion"

    $invalidSummary.duration_ms = 1800000
    $invalidSummary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    $forgedObservations = Get-Content -LiteralPath $observationsPath -Raw | ConvertFrom-Json
    $forgedObservations.source_revision = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    $forgedObservations | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $observationsPath -Encoding UTF8
    $forgedSourceRejected = $false
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 `
            -EvidenceDirectory $evidenceRoot `
            -PackageDirectory $packageRoot
    }
    catch {
        $forgedSourceRejected = $_.Exception.Message -match "source.reviewed_revision"
    }
    finally {
        Pop-Location
    }
    Expect $forgedSourceRejected "forged observation revision was not rejected by the production analyser"
}
finally {
    if (Test-Path -LiteralPath $sessionRoot) {
        $resolvedBuildRoot = [IO.Path]::GetFullPath($buildRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        $resolvedSessionRoot = [IO.Path]::GetFullPath($sessionRoot)
        if (-not $resolvedSessionRoot.StartsWith($resolvedBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean a manual-evidence fixture outside the repository build root."
        }
        Remove-Item -LiteralPath $resolvedSessionRoot -Recurse -Force
    }
}

Write-Host "PASS: Project Tempest manual acceptance analyser and adversarial fixture"
