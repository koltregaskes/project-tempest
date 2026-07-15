[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$unattendedSurfaces = @(
    ".github/workflows/ci.yml",
    ".github/workflows/build-toolchain.yml",
    "scripts/test-project-tempest-assets.ps1",
    "scripts/test-w3d-pipeline.ps1",
    "scripts/create-courier-blockout.ps1",
    "scripts/prepare-w3dview-compat.ps1"
)

$forbiddenProcessLaunchPatterns = @(
    "(?i)\bStart-Process\b",
    "(?i)\bInvoke-Item\b",
    "(?i)\[System\.Diagnostics\.Process\]::Start",
    "(?i)\bWScript\.Shell\b",
    "(?i)\bShellExecute\b",
    "(?im)^\s*&\s+.*(?:W3DViewV|ProjectTempestDemo|generalsv|WorldBuilderV)\.exe\b",
    "(?im)^\s*(?:\.\\|\.\/).*?(?:W3DViewV|ProjectTempestDemo|generalsv|WorldBuilderV)\.exe\b"
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

foreach ($blenderScript in @("scripts/test-w3d-pipeline.ps1", "scripts/create-courier-blockout.ps1")) {
    $content = Get-Content -LiteralPath (Join-Path $repositoryRoot $blenderScript) -Raw
    $invocations = [regex]::Matches($content, '(?im)^\s*&\s+\$BlenderPath\b.*$')
    if ($invocations.Count -ne 1 -or $invocations[0].Value -notmatch '(?i)--background\b') {
        throw "Blender invocation in '$blenderScript' must be exactly one explicit --background path."
    }
}

$compatibilityScript = Get-Content -LiteralPath (Join-Path $repositoryRoot "scripts/prepare-w3dview-compat.ps1") -Raw
if ($compatibilityScript -notmatch 'LaunchPolicy\s*=\s*"manual_only"') {
    throw "The compatibility helper must advertise LaunchPolicy=manual_only and must never launch its target."
}

Write-Host "Validated Project Tempest's no-visible-GUI unattended execution contract across $($unattendedSurfaces.Count) surfaces."
