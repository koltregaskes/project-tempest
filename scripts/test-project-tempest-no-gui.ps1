[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$fixedUnattendedSurfaces = @(
    ".github/workflows/ci.yml",
    ".github/workflows/build-toolchain.yml",
    "scripts/test-project-tempest-assets.ps1",
    "scripts/test-w3d-pipeline.ps1",
    "scripts/prepare-w3dview-compat.ps1"
)

# Asset generators are unattended surfaces too. Discover them instead of maintaining a
# fragile allow-list so every future create-*.ps1 wrapper inherits this gate immediately.
$assetGeneratorSurfaces = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot "scripts") -Filter "create-*.ps1" -File |
    ForEach-Object { "scripts/$($_.Name)" }
$unattendedSurfaces = @($fixedUnattendedSurfaces + $assetGeneratorSurfaces | Sort-Object -Unique)

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
    "(?i)\bRegister-ScheduledTask\b",
    "(?i)\bschtasks(?:\.exe)?\b",
    "(?i)\bcmd(?:\.exe)?\s+\/c\s+start\b",
    '(?im)^\s*&\s+\$(?:viewerPath|executablePath|gamePath|demoPath|worldBuilderPath)\b',
    "(?im)^\s*&\s+.*(?:W3DViewV|W3DViewZH|ProjectTempestDemo|generalsv|generalszh|WorldBuilderV|WorldBuilderZH)\.exe\b",
    "(?im)^\s*(?:\.\\|\.\/).*?(?:W3DViewV|W3DViewZH|ProjectTempestDemo|generalsv|generalszh|WorldBuilderV|WorldBuilderZH)\.exe\b"
)

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
}

foreach ($blenderScript in $unattendedSurfaces) {
    $content = Get-Content -LiteralPath (Join-Path $repositoryRoot $blenderScript) -Raw
    if ($content -notmatch '(?i)\$BlenderPath\b') {
        continue
    }
    $invocations = [regex]::Matches($content, '(?im)^\s*&\s+\$BlenderPath\b.*$')
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
    "No agent, automation, CI job, or scheduled task may perform these manual checks"
)) {
    if ($runbook -notmatch [regex]::Escape($requiredPolicy)) {
        throw "Project Tempest runbook is missing required no-GUI policy text: '$requiredPolicy'."
    }
}

Write-Host "Validated Project Tempest's no-visible-GUI unattended execution contract across $($unattendedSurfaces.Count) surfaces."
Write-Host "Confirmed that no prohibited GUI process is running and no automatic renderer retry is declared."
