[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$fixedUnattendedSurfaces = @(
    ".github/workflows/build-toolchain.yml",
    "scripts/build-windows.ps1",
    "scripts/analyse-project-tempest-manual-evidence.ps1",
    "scripts/package-project-tempest-demo.ps1",
    "scripts/test-w3d-pipeline.ps1",
    "scripts/prepare-w3dview-compat.ps1"
)

# Discover current and future Project Tempest assertion/validation/generation wrappers instead of
# relying on an allow-list that can silently omit a newly added unattended entry point.
$scriptSurfaces = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot "scripts") -File -Filter "*.ps1" |
    Where-Object {
        $_.Name -ne "test-project-tempest-no-gui.ps1" -and
        $_.Name -match '^(?:assert-project-tempest-.*|create-.*|test-project-tempest-.*)\.ps1$'
    } |
    ForEach-Object { "scripts/$($_.Name)" }

if ("scripts/assert-project-tempest-artifact-boundary.ps1" -notin $scriptSurfaces) {
    throw "Project Tempest no-GUI discovery omitted the shared artifact-boundary assertion."
}
if ("scripts/analyse-project-tempest-manual-evidence.ps1" -notin $fixedUnattendedSurfaces) {
    throw "Project Tempest no-GUI policy omitted the production manual evidence analyser."
}

# A workflow that names Project Tempest is part of the unattended surface even when it
# invokes a binary directly instead of going through one of the guarded wrappers.
$projectWorkflowPattern = '(?i)project[ -]?tempest'
$workflowSurfaces = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot ".github/workflows") -File |
    Where-Object {
        $_.Extension -in @(".yml", ".yaml") -and
        (Get-Content -LiteralPath $_.FullName -Raw) -match $projectWorkflowPattern
    } |
    ForEach-Object { ".github/workflows/$($_.Name)" }

foreach ($projectSpelling in @("ProjectTempest/Code", "Project Tempest", "project-tempest")) {
    if ($projectSpelling -notmatch $projectWorkflowPattern) {
        throw "Project Tempest workflow discovery misses canonical spelling '$projectSpelling'."
    }
}

$unattendedSurfaces = @($fixedUnattendedSurfaces + $scriptSurfaces + $workflowSurfaces | Sort-Object -Unique)

$forbiddenProcessNames = @(
    "W3DViewV",
    "W3DViewZH",
    "ProjectTempestDemo",
    "generalsv",
    "generalszh",
    "WorldBuilderV",
    "WorldBuilderZH"
)
$runningForbiddenProcesses = @(Get-Process -Name $forbiddenProcessNames -ErrorAction SilentlyContinue)
if ($runningForbiddenProcesses.Count -gt 0) {
    $details = $runningForbiddenProcesses |
        ForEach-Object { "$($_.ProcessName) (PID $($_.Id))" }
    throw "Prohibited visible GUI process detected during unattended validation: $($details -join ', ')."
}

$forbiddenProcessLaunchPatterns = @(
    "(?i)\bStart-Process\b",
    "(?i)\bInvoke-Item\b",
    "(?i)\[System\.Diagnostics\.Process\]::Start",
    "(?i)\bProcessStartInfo\b",
    "(?i)\bWScript\.Shell\b",
    "(?i)\bShellExecute\b",
    "(?i)\bStart-Job\b",
    "(?i)\bInvoke-Expression\b",
    "(?i)\bRegister-ScheduledTask\b",
    "(?i)\bschtasks(?:\.exe)?\b",
    "(?i)\bcmd(?:\.exe)?\s+\/c\s+start\b",
    '(?im)^\s*&\s+\$(?:viewerPath|executablePath|gamePath|demoPath|worldBuilderPath)\b',
    "(?im)^\s*&\s+.*(?:W3DViewV|W3DViewZH|ProjectTempestDemo|generalsv|generalszh|WorldBuilderV|WorldBuilderZH)(?:\.exe)?\b",
    "(?im)^\s*(?:\.\\|\.\/).*?(?:W3DViewV|W3DViewZH|ProjectTempestDemo|generalsv|generalszh|WorldBuilderV|WorldBuilderZH)(?:\.exe)?\b"
)

function Get-UnattendedScriptPolicyViolation {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Content,
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    $tokens = $null
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseInput(
        $Content,
        [ref]$tokens,
        [ref]$parseErrors
    )
    $violations = [System.Collections.Generic.List[string]]::new()
    foreach ($parseError in $parseErrors) {
        $violations.Add("PowerShell parse error at offset $($parseError.Extent.StartOffset): $($parseError.Message)")
    }

    $commands = $ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.CommandAst]
    }, $true)
    foreach ($command in $commands) {
        $commandName = $command.GetCommandName()
        if ($commandName -and $commandName -match '(?i)^(?:Start-Process|Invoke-Item|Invoke-Expression|Start-Job|Register-ScheduledTask|schtasks(?:\.exe)?)$') {
            $violations.Add("forbidden command '$commandName'")
            continue
        }
        if ($commandName -and $commandName -match '(?i)(?:^|[\\/])(?:W3DViewV|W3DViewZH|ProjectTempestDemo|generalsv|generalszh|WorldBuilderV|WorldBuilderZH)(?:\.exe)?$') {
            $violations.Add("forbidden GUI executable '$commandName'")
            continue
        }

        if ($command.InvocationOperator -ne [System.Management.Automation.Language.TokenKind]::Ampersand -or $commandName) {
            continue
        }

        $firstElement = $command.CommandElements[0].Extent.Text
        $commandText = $command.Extent.Text
        $isSafeBlender = (
            $firstElement -ieq '$BlenderPath' -and
            $commandText -match '(?i)--background\b' -and
            $commandText -match '(?i)--factory-startup\b'
        )
        $isSafeBuildTool = (
            $RelativePath -ieq 'scripts/build-windows.ps1' -and
            $firstElement -in @('$cmake', '$vswhere')
        )
        $isSafePackageTool = (
            $RelativePath -ieq 'scripts/test-project-tempest-package.ps1' -and
            $firstElement -in @('$packageScript', '$packageVerifier')
        )
        if (-not $isSafeBlender -and -not $isSafeBuildTool -and -not $isSafePackageTool) {
            $violations.Add("unapproved dynamic invocation '$firstElement'")
        }
    }

    return @($violations)
}

foreach ($relativePath in $unattendedSurfaces) {
    $path = Join-Path $repositoryRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing Project Tempest unattended-validation surface: $relativePath"
    }

    $content = Get-Content -LiteralPath $path -Raw
    foreach ($pattern in $forbiddenProcessLaunchPatterns) {
        if ($content -match $pattern) {
            throw "Interactive process-launch primitive '$pattern' is forbidden in unattended surface '$relativePath'."
        }
    }

    if ([IO.Path]::GetExtension($relativePath) -ieq ".ps1") {
        $astViolations = @(Get-UnattendedScriptPolicyViolation -Content $content -RelativePath $relativePath)
        if ($astViolations.Count -gt 0) {
            throw "Unattended PowerShell policy violation in '$relativePath': $($astViolations -join '; ')"
        }
    }
}

# These fixtures prove that renaming or constructing a GUI path cannot bypass the AST gate.
$adversarialFixtures = @(
    '$tool = Join-Path $root "W3DViewV.exe"; & $tool',
    '$tool = Join-Path $root "W3DViewV"; & $tool',
    '$renamed = "ProjectTempestDemo.exe"; & $renamed',
    '.\ProjectTempestDemo',
    '& (Join-Path $root "WorldBuilderV.exe")'
)
foreach ($fixture in $adversarialFixtures) {
    $fixtureViolations = @(Get-UnattendedScriptPolicyViolation -Content $fixture -RelativePath "adversarial-fixture.ps1")
    if ($fixtureViolations.Count -eq 0) {
        throw "The no-GUI AST gate failed to reject adversarial fixture: $fixture"
    }
}

$safeFixtures = @(
    @{ Content = '& $BlenderPath --background --factory-startup --python $pythonScript'; Path = 'scripts/create-fixture.ps1' },
    @{ Content = '& $cmake --preset $Preset'; Path = 'scripts/build-windows.ps1' },
    @{ Content = '& $packageScript -RuntimeDirectory $runtime'; Path = 'scripts/test-project-tempest-package.ps1' },
    @{ Content = '& $packageVerifier -PackagePath $archive'; Path = 'scripts/test-project-tempest-package.ps1' }
)
foreach ($fixture in $safeFixtures) {
    $fixtureViolations = @(Get-UnattendedScriptPolicyViolation -Content $fixture.Content -RelativePath $fixture.Path)
    if ($fixtureViolations.Count -gt 0) {
        throw "The no-GUI AST gate rejected a documented headless fixture: $($fixtureViolations -join '; ')"
    }
}

foreach ($blenderScript in $unattendedSurfaces) {
    $content = Get-Content -LiteralPath (Join-Path $repositoryRoot $blenderScript) -Raw
    $invocations = [regex]::Matches($content, '(?im)^\s*&\s+\$BlenderPath\b.*$')
    if ($invocations.Count -eq 0) {
        continue
    }
    if (
        $invocations.Count -ne 1 -or
        $invocations[0].Value -notmatch '(?i)--background\b' -or
        $invocations[0].Value -notmatch '(?i)--factory-startup\b'
    ) {
        throw "Blender invocation in '$blenderScript' must be exactly one explicit --background --factory-startup path."
    }
}

$compatibilityScript = Get-Content -LiteralPath (Join-Path $repositoryRoot "scripts/prepare-w3dview-compat.ps1") -Raw
if (
    $compatibilityScript -notmatch 'LaunchPolicy\s*=\s*"manual_only"' -or
    $compatibilityScript -notmatch 'AutomaticRetry\s*=\s*\$false' -or
    $compatibilityScript -notmatch 'VerificationMode\s*=\s*"files_and_hashes_only"'
) {
    throw "The compatibility helper must advertise manual-only launch, no automatic retry, and files/hashes-only verification."
}

$runbook = Get-Content -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/README.md") -Raw
foreach ($requiredPolicy in @(
    "manual-only",
    "do not retry a visible GUI",
    "No agent, automation, CI job, or scheduled task may perform these manual checks",
    "no unattended wrapper may invoke them or retry them",
    "PROJECT_TEMPEST_EVIDENCE_DIR",
    "It does not launch the game",
    "It never starts or retries the demo"
)) {
    if ($runbook -notmatch [regex]::Escape($requiredPolicy)) {
        throw "Project Tempest runbook is missing required no-GUI policy text: '$requiredPolicy'."
    }
}

$runtimeEvidenceSource = Get-Content -LiteralPath (
    Join-Path $repositoryRoot "ProjectTempest/Code/TempestRuntimeEvidence.cpp") -Raw
$demoSource = Get-Content -LiteralPath (
    Join-Path $repositoryRoot "ProjectTempest/Code/ProjectTempestDemo.cpp") -Raw
foreach ($evidenceContract in @(
    "manual_playthrough_claimed",
    "clean_shutdown",
    "focus_losses",
    "working_set_bytes",
    "PROJECT_TEMPEST_EVIDENCE_DIR",
    "RecordFrame",
    "RecordFocus",
    "RecordResolution"
)) {
    if (($runtimeEvidenceSource + $demoSource) -notmatch [regex]::Escape($evidenceContract)) {
        throw "Project Tempest runtime evidence is missing governed contract '$evidenceContract'."
    }
}

$manualEvidenceAnalyser = Get-Content -LiteralPath (
    Join-Path $repositoryRoot "scripts/analyse-project-tempest-manual-evidence.ps1") -Raw
foreach ($requiredAnalyserPolicy in @(
    "renderer_execution_by_analyser",
    "not_performed",
    "automatic_retry"
)) {
    if ($manualEvidenceAnalyser -notmatch [regex]::Escape($requiredAnalyserPolicy)) {
        throw "Manual evidence analyser is missing no-GUI evidence '$requiredAnalyserPolicy'."
    }
}

foreach ($forbiddenExecutable in @(
    "W3DViewV.exe",
    "W3DViewZH.exe",
    "ProjectTempestDemo.exe",
    "generalsv.exe",
    "generalszh.exe",
    "WorldBuilderV.exe",
    "WorldBuilderZH.exe"
)) {
    if ($runbook -notmatch [regex]::Escape($forbiddenExecutable)) {
        throw "Project Tempest runbook is missing prohibited executable '$forbiddenExecutable'."
    }
}

$testingGuide = Get-Content -LiteralPath (Join-Path $repositoryRoot "TESTING.md") -Raw
foreach ($requiredTestingPolicy in @(
    "manual-only user action",
    "unattended scripts must not run it",
    "record that evidence as blocked",
    "not retry the executable"
)) {
    if ($testingGuide -notmatch [regex]::Escape($requiredTestingPolicy)) {
        throw "Testing guide is missing required no-GUI policy text: '$requiredTestingPolicy'."
    }
}

Write-Host "Validated Project Tempest's no-visible-GUI unattended execution contract across $($unattendedSurfaces.Count) surfaces."
Write-Host "Confirmed with AST adversarial fixtures that no prohibited GUI process is running and no automatic renderer retry is declared."
