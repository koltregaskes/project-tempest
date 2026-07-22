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

function Write-Utf8NoBomJson {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $json = (($Value | ConvertTo-Json -Depth 20) -replace "`r`n", "`n") + "`n"
    [IO.File]::WriteAllText($Path, $json, [Text.UTF8Encoding]::new($false))
}

function Write-FixtureHashManifest {
    param([Parameter(Mandatory = $true)][string]$PackageRoot)
    $lines = @(
        Get-ChildItem -LiteralPath $PackageRoot -File -Force |
            Where-Object { $_.Name -ne "SHA256SUMS.txt" } |
            Sort-Object Name |
            ForEach-Object {
                "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"
            }
    )
    [IO.File]::WriteAllText(
        (Join-Path $PackageRoot "SHA256SUMS.txt"),
        ($lines -join "`n") + "`n",
        [Text.UTF8Encoding]::new($false))
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
    $analyserPackagePath = Join-Path $packageRoot "ANALYSE-MANUAL-EVIDENCE.ps1"
    $provenancePackagePath = Join-Path $packageRoot "asset-provenance.json"
    $contractPackagePath = Join-Path $packageRoot "package-contract.json"
    [IO.File]::WriteAllBytes($executablePath, [byte[]](77, 90, 1, 2, 3, 4))
    [IO.File]::WriteAllBytes($milesPath, [byte[]](77, 90, 5, 6, 7, 8))
    [IO.File]::WriteAllText(
        $analyserPackagePath,
        "Write-Output 'governed fixture analyser'`n",
        [Text.UTF8Encoding]::new($false))
    Write-Utf8NoBomJson -Path $provenancePackagePath -Value ([ordered]@{
        schema_version = 1
        assets = @()
    })
    $fixtureContract = [ordered]@{
        schema_version = 3
        package_directory = "ProjectTempestDemo-private"
        archive_name = "ProjectTempestDemo-private.zip"
        runtime_files = @(
            [ordered]@{ name = "ProjectTempestDemo.exe"; kind = "executable" },
            [ordered]@{ name = "mss32.dll"; kind = "runtime_dependency" }
        )
        repository_files = @(
            [ordered]@{ path = "ProjectTempest/package-contract.json"; name = "package-contract.json"; kind = "package_contract" },
            [ordered]@{ path = "ProjectTempest/asset-provenance.json"; name = "asset-provenance.json"; kind = "provenance" },
            [ordered]@{ path = "scripts/analyse-project-tempest-manual-evidence.ps1"; name = "ANALYSE-MANUAL-EVIDENCE.ps1"; kind = "manual_evidence_analyser" }
        )
    }
    Write-Utf8NoBomJson -Path $contractPackagePath -Value $fixtureContract
    $executableHash = (Get-FileHash -LiteralPath $executablePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $milesHash = (Get-FileHash -LiteralPath $milesPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $analysisArguments = @{
        EvidenceDirectory = $evidenceRoot
        PackageDirectory = $packageRoot
        ExpectedReviewedSourceRevision = $revision
        ExpectedExecutableSha256 = $executableHash
        ExpectedMilesSha256 = $milesHash
    }
    $fixtureManifestFiles = @(
        @($fixtureContract.runtime_files) + @($fixtureContract.repository_files) |
            ForEach-Object {
                $path = Join-Path $packageRoot ([string]$_.name)
                [ordered]@{
                    name = [string]$_.name
                    kind = [string]$_.kind
                    length = (Get-Item -LiteralPath $path -Force).Length
                    sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
                }
            }
    )
    $fixtureManifest = [ordered]@{
        schema_version = 2
        package = "ProjectTempestDemo-private"
        distribution = "private_internal_demo"
        source_revision = $revision
        reviewed_source_revision = $revision
        source_tree = "clean"
        package_contract_sha256 = (Get-FileHash -LiteralPath $contractPackagePath -Algorithm SHA256).Hash.ToLowerInvariant()
        asset_provenance_sha256 = (Get-FileHash -LiteralPath $provenancePackagePath -Algorithm SHA256).Hash.ToLowerInvariant()
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
        files = $fixtureManifestFiles
    }
    Write-Utf8NoBomJson -Path (Join-Path $packageRoot "package-manifest.json") -Value $fixtureManifest
    Write-FixtureHashManifest -PackageRoot $packageRoot

    $traceName = "project-tempest-runtime-fixture.jsonl"
    $summaryName = "project-tempest-runtime-fixture-summary.json"
    $traceLines = [Collections.Generic.List[string]]::new()
    $traceLines.Add('{"schema_version":1,"type":"session_start","session_id":"fixture","started_unix_ms":1}')
    foreach ($event in @(
        @{ elapsed_ms = 1000; name = "resolution"; detail = "1920x1080" },
        @{ elapsed_ms = 2000; name = "focus_lost"; detail = "" },
        @{ elapsed_ms = 3000; name = "focus_gained"; detail = "" },
        @{ elapsed_ms = 4000; name = "restart"; detail = "" },
        @{ elapsed_ms = 4500; name = "restart"; detail = "" },
        @{ elapsed_ms = 5000; name = "resolution"; detail = "2560x1440" },
        @{ elapsed_ms = 6000; name = "resolution"; detail = "3840x2160" },
        @{ elapsed_ms = 7000; name = "resolution"; detail = "3440x1440" },
        @{ elapsed_ms = 8000; name = "outcome"; detail = "victory" },
        @{ elapsed_ms = 9000; name = "outcome"; detail = "defeat" }
    )) {
        $record = [ordered]@{ schema_version = 1; type = "event"; elapsed_ms = $event.elapsed_ms; name = $event.name }
        if ($event.detail) { $record.detail = $event.detail }
        $traceLines.Add(($record | ConvertTo-Json -Compress))
    }
    $resolutions = @("1920x1080", "2560x1440", "3840x2160", "3440x1440")
    for ($index = 0; $index -lt 1800; ++$index) {
        $dimensions = $resolutions[$index % $resolutions.Count] -split 'x'
        $workingSet = if ($index -eq 0) { 100000000 } elseif ($index -eq 900) { 140000000 } elseif ($index -eq 1799) { 110000000 } else { 105000000 }
        $traceLines.Add(([ordered]@{
            schema_version = 1
            type = "frame_window"
            start_ms = $index * 1000
            end_ms = ($index + 1) * 1000
            frames = 60
            active_frames = 60
            frame_ms = [ordered]@{ average = 16.0; min = 10.0; p95 = 16.6; p99 = 16.8; max = 16.8 }
            last_simulation_tick = ($index + 1) * 20
            width = [int]$dimensions[0]
            height = [int]$dimensions[1]
            working_set_bytes = $workingSet
        } | ConvertTo-Json -Compress -Depth 5))
    }
    $traceLines.Add('{"schema_version":1,"type":"session_end","elapsed_ms":1800000,"exit_code":0,"clean_shutdown":true}')
    $traceLines | Set-Content -LiteralPath (Join-Path $evidenceRoot $traceName) -Encoding UTF8
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
        frame_windows = 1800
        frame_windows_dropped = 0
        percentile_resolution_ms = 0.1
        histogram_saturated_frames_ge_1000ms = 0
        frame_ms = [ordered]@{ min = 10.0; average = 16.0; p50 = 16.0; p95 = 16.6; p99 = 16.8; max = 16.8 }
        working_set_bytes = [ordered]@{ start = 100000000; end = 110000000; peak = 140000000 }
        events = 10
        event_entries_dropped = 0
        focus_losses = 1
        restarts = 2
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
        .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments
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

    $originalAnalyserPackageBytes = [IO.File]::ReadAllBytes($analyserPackagePath)
    $originalContractPackageBytes = [IO.File]::ReadAllBytes($contractPackagePath)
    $originalSumsBytes = [IO.File]::ReadAllBytes((Join-Path $packageRoot "SHA256SUMS.txt"))

    [IO.File]::WriteAllText(
        $analyserPackagePath,
        "Write-Output 'tampered after install'`n",
        [Text.UTF8Encoding]::new($false))
    $governedFileTamperRejected = $false
    Push-Location $repositoryRoot
    try { .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments }
    catch { $governedFileTamperRejected = $_.Exception.Message -match "source.package_manifest_files" }
    finally { Pop-Location }
    Expect $governedFileTamperRejected "modified governed analyser passed unpacked-package integrity checks"
    [IO.File]::WriteAllBytes($analyserPackagePath, $originalAnalyserPackageBytes)

    Remove-Item -LiteralPath $analyserPackagePath -Force
    $governedFileRemovalRejected = $false
    Push-Location $repositoryRoot
    try { .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments }
    catch { $governedFileRemovalRejected = $_.Exception.Message -match "source.package_tree" }
    finally { Pop-Location }
    Expect $governedFileRemovalRejected "removed governed analyser passed unpacked-package integrity checks"
    [IO.File]::WriteAllBytes($analyserPackagePath, $originalAnalyserPackageBytes)

    [IO.File]::WriteAllBytes(
        $contractPackagePath,
        $originalContractPackageBytes + [Text.Encoding]::UTF8.GetBytes("`n"))
    $contractTamperRejected = $false
    Push-Location $repositoryRoot
    try { .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments }
    catch { $contractTamperRejected = $_.Exception.Message -match "source.package_metadata_hashes" }
    finally { Pop-Location }
    Expect $contractTamperRejected "modified package contract passed unpacked-package integrity checks"
    [IO.File]::WriteAllBytes($contractPackagePath, $originalContractPackageBytes)

    $forgedSumsText = [Text.UTF8Encoding]::new($false, $true).GetString($originalSumsBytes)
    $forgedSumsText = "0" + $forgedSumsText.Substring(1)
    [IO.File]::WriteAllText(
        (Join-Path $packageRoot "SHA256SUMS.txt"),
        $forgedSumsText,
        [Text.UTF8Encoding]::new($false))
    $sumsTamperRejected = $false
    Push-Location $repositoryRoot
    try { .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments }
    catch { $sumsTamperRejected = $_.Exception.Message -match "source.package_hash_manifest" }
    finally { Pop-Location }
    Expect $sumsTamperRejected "modified SHA256SUMS passed unpacked-package integrity checks"
    [IO.File]::WriteAllBytes((Join-Path $packageRoot "SHA256SUMS.txt"), $originalSumsBytes)

    $summaryPath = Join-Path $evidenceRoot $summaryName
    $invalidSummary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json

    $oneRestartTraceLines = @($traceLines | Where-Object {
        $record = $_ | ConvertFrom-Json
        -not ([string]$record.type -eq "event" -and
            [string]$record.name -eq "restart" -and [uint64]$record.elapsed_ms -eq 4500)
    })
    $oneRestartTraceLines | Set-Content -LiteralPath (Join-Path $evidenceRoot $traceName) -Encoding UTF8
    $invalidSummary.events = 9
    $invalidSummary.restarts = 1
    $invalidSummary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    $singleRestartRejected = $false
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments
    }
    catch {
        $singleRestartRejected = $_.Exception.Message -match "runtime.repeated_restart"
    }
    finally {
        Pop-Location
    }
    Expect $singleRestartRejected "single-restart evidence passed the repeated-restart gate"
    $traceLines | Set-Content -LiteralPath (Join-Path $evidenceRoot $traceName) -Encoding UTF8
    $invalidSummary.events = 10
    $invalidSummary.restarts = 2

    $invalidSummary.duration_ms = 1799999
    $invalidSummary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    $rejected = $false
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments
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

    $sparseTraceLines = @($traceLines | Where-Object {
        $record = $_ | ConvertFrom-Json
        [string]$record.type -ne "frame_window" -or
            [uint64]$record.start_ms -eq 0 -or [uint64]$record.start_ms -eq 1799000
    })
    $sparseTraceLines | Set-Content -LiteralPath (Join-Path $evidenceRoot $traceName) -Encoding UTF8
    $invalidSummary.frames = 120
    $invalidSummary.frame_windows = 2
    $invalidSummary.working_set_bytes.peak = 110000000
    $invalidSummary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    $sparseTraceRejected = $false
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments
    }
    catch {
        $sparseTraceRejected = $_.Exception.Message -match "trace.window_continuity"
    }
    finally {
        Pop-Location
    }
    Expect $sparseTraceRejected "sparse 30-minute trace was not rejected by the production analyser"

    $traceLines | Set-Content -LiteralPath (Join-Path $evidenceRoot $traceName) -Encoding UTF8
    $invalidSummary.frames = 108000
    $invalidSummary.frame_windows = 1800
    $invalidSummary.working_set_bytes.peak = 140000000
    $invalidSummary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

    $shiftedTraceLines = @($traceLines | ForEach-Object {
        $record = $_ | ConvertFrom-Json
        if ([string]$record.type -eq "frame_window") {
            $record.start_ms = [uint64]$record.start_ms + 1800000
            $record.end_ms = [uint64]$record.end_ms + 1800000
        }
        $record | ConvertTo-Json -Compress -Depth 5
    })
    $shiftedTraceLines | Set-Content -LiteralPath (Join-Path $evidenceRoot $traceName) -Encoding UTF8
    $shiftedTraceRejected = $false
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments
    }
    catch {
        $shiftedTraceRejected = $_.Exception.Message -match "trace.window_continuity"
    }
    finally {
        Pop-Location
    }
    Expect $shiftedTraceRejected "trace windows outside the recorded session were not rejected"
    $traceLines | Set-Content -LiteralPath (Join-Path $evidenceRoot $traceName) -Encoding UTF8

    $lowDensityTraceLines = @($traceLines | ForEach-Object {
        $record = $_ | ConvertFrom-Json
        if ([string]$record.type -eq "frame_window") {
            $record.frames = 1
            $record.active_frames = 1
        }
        $record | ConvertTo-Json -Compress -Depth 5
    })
    $lowDensityTraceLines | Set-Content -LiteralPath (Join-Path $evidenceRoot $traceName) -Encoding UTF8
    $invalidSummary.frames = 1800
    $invalidSummary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    $lowDensityRejected = $false
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments
    }
    catch {
        $lowDensityRejected = $_.Exception.Message -match "performance.trace_1080p_60fps_target"
    }
    finally {
        Pop-Location
    }
    Expect $lowDensityRejected "one-frame-per-second trace passed the 1080p 60fps evidence gate"
    $traceLines | Set-Content -LiteralPath (Join-Path $evidenceRoot $traceName) -Encoding UTF8
    $invalidSummary.frames = 108000
    $invalidSummary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

    $forgedObservations = Get-Content -LiteralPath $observationsPath -Raw | ConvertFrom-Json
    $forgedObservations.source_revision = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    $forgedObservations | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $observationsPath -Encoding UTF8
    $forgedSourceRejected = $false
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments
    }
    catch {
        $forgedSourceRejected = $_.Exception.Message -match "source.reviewed_revision"
    }
    finally {
        Pop-Location
    }
    Expect $forgedSourceRejected "forged observation revision was not rejected by the production analyser"

    $forgedObservations.source_revision = $revision
    $forgedObservations | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $observationsPath -Encoding UTF8

    $manifestFixturePath = Join-Path $packageRoot "package-manifest.json"
    $originalManifestText = Get-Content -LiteralPath $manifestFixturePath -Raw
    $relabelledManifest = $originalManifestText | ConvertFrom-Json
    $relabelledRevision = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    $relabelledManifest.reviewed_source_revision = $relabelledRevision
    $relabelledManifest.executable_verification.reviewed_source_revision = $relabelledRevision
    $relabelledManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestFixturePath -Encoding UTF8
    $forgedObservations.source_revision = $relabelledRevision
    $forgedObservations | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $observationsPath -Encoding UTF8
    $relabelledPackageRejected = $false
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments
    }
    catch {
        $relabelledPackageRejected = $_.Exception.Message -match "source.reviewed_revision"
    }
    finally {
        Pop-Location
    }
    Expect $relabelledPackageRejected "self-consistent package relabelling bypassed the external reviewed revision"

    [IO.File]::WriteAllText($manifestFixturePath, $originalManifestText, [Text.UTF8Encoding]::new($false))
    Write-FixtureHashManifest -PackageRoot $packageRoot
    $forgedObservations.source_revision = $revision
    $forgedObservations | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $observationsPath -Encoding UTF8
    $originalExecutableBytes = [IO.File]::ReadAllBytes($executablePath)
    [IO.File]::WriteAllBytes($executablePath, [byte[]](77, 90, 9, 9, 9, 9))
    $rehashedManifest = $originalManifestText | ConvertFrom-Json
    $rehashedManifest.executable_verification.sha256 = (
        Get-FileHash -LiteralPath $executablePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $rehashedManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestFixturePath -Encoding UTF8
    $rehashedBinaryRejected = $false
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments
    }
    catch {
        $rehashedBinaryRejected = $_.Exception.Message -match "source.executable_hash"
    }
    finally {
        Pop-Location
    }
    Expect $rehashedBinaryRejected "rehashed replacement executable bypassed the external binary identity"
    [IO.File]::WriteAllBytes($executablePath, $originalExecutableBytes)
    [IO.File]::WriteAllText($manifestFixturePath, $originalManifestText, [Text.UTF8Encoding]::new($false))
    Write-FixtureHashManifest -PackageRoot $packageRoot

    $mergeRevision = "cccccccccccccccccccccccccccccccccccccccc"
    $mergeManifestPath = Join-Path $packageRoot "package-manifest.json"
    $mergeManifest = Get-Content -LiteralPath $mergeManifestPath -Raw | ConvertFrom-Json
    $mergeManifest.source_revision = $mergeRevision
    $mergeManifest.executable_verification.source_revision = $mergeRevision
    Write-Utf8NoBomJson -Path $mergeManifestPath -Value $mergeManifest
    Write-FixtureHashManifest -PackageRoot $packageRoot
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments
    }
    finally {
        Pop-Location
    }
    $mergeReport = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    Expect ($mergeReport.result -eq "pass") "valid merge-build/reviewed-head revision pair was rejected"

    $outsideEvidenceRoot = Join-Path $sessionRoot "evidence-case-escape"
    $null = New-Item -ItemType Directory -Path $outsideEvidenceRoot
    [IO.File]::WriteAllBytes(
        (Join-Path $outsideEvidenceRoot "resolution-1080p.png"),
        [Text.Encoding]::UTF8.GetBytes("outside evidence fixture"))
    $escapedObservations = Get-Content -LiteralPath $observationsPath -Raw | ConvertFrom-Json
    $escapedObservations.resolution_checks[0].screenshot = "../evidence-case-escape/resolution-1080p.png"
    $escapedObservations | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $observationsPath -Encoding UTF8
    $outsideEvidenceRejected = $false
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments
    }
    catch {
        $outsideEvidenceRejected = $_.Exception.Message -match "artifact.resolution.1920x1080"
    }
    finally {
        Pop-Location
    }
    Expect $outsideEvidenceRejected "evidence path outside the governed root was not rejected"

    $observations | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $observationsPath -Encoding UTF8
    $duplicateScreenshotSource = Join-Path $evidenceRoot ([string]$observations.resolution_checks[0].screenshot)
    $duplicateScreenshotTarget = Join-Path $evidenceRoot ([string]$observations.accessibility_checks[0].screenshot)
    [IO.File]::WriteAllBytes($duplicateScreenshotTarget, [IO.File]::ReadAllBytes($duplicateScreenshotSource))
    $duplicateScreenshotRejected = $false
    Push-Location $repositoryRoot
    try {
        .\scripts\analyse-project-tempest-manual-evidence.ps1 @analysisArguments
    }
    catch {
        $duplicateScreenshotRejected = $_.Exception.Message -match "artifact.accessibility.off"
    }
    finally {
        Pop-Location
    }
    Expect $duplicateScreenshotRejected "copied screenshot bytes were allowed to satisfy multiple required views"
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
