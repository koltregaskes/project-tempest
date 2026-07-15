[CmdletBinding()]
param(
    [string]$BlenderPath = "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$pluginRepository = "https://github.com/OpenSAGE/OpenSAGE.BlenderPlugin.git"
$pluginCommit = "feb80cd0bf22b3c24c0395ae3260a5349c080892"
$repoRoot = Split-Path -Parent $PSScriptRoot
$toolRoot = Join-Path $env:LOCALAPPDATA "ProjectTempest\Tools\OpenSAGE.BlenderPlugin"
$outputRoot = Join-Path $repoRoot "build\w3d-pipeline-smoke"
$resultPath = Join-Path $outputRoot "result.json"
$pythonScript = Join-Path $PSScriptRoot "test-w3d-pipeline.py"

if (-not (Test-Path -LiteralPath $BlenderPath)) {
    throw "Blender 5.1 was not found at '$BlenderPath'. Pass -BlenderPath with the installed blender.exe."
}

if (Test-Path -LiteralPath $toolRoot) {
    if (-not (Test-Path -LiteralPath (Join-Path $toolRoot ".git"))) {
        throw "The W3D tool destination exists but is not the expected Git checkout: '$toolRoot'."
    }
} else {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $toolRoot) | Out-Null
    & git -c core.longpaths=true clone $pluginRepository $toolRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Cloning the OpenSAGE Blender plugin failed with exit code $LASTEXITCODE."
    }
}

& git -C $toolRoot -c core.longpaths=true fetch origin $pluginCommit
if ($LASTEXITCODE -ne 0) {
    throw "Fetching pinned OpenSAGE Blender plugin commit failed with exit code $LASTEXITCODE."
}
& git -C $toolRoot -c core.longpaths=true checkout --detach $pluginCommit
if ($LASTEXITCODE -ne 0) {
    throw "Checking out pinned OpenSAGE Blender plugin commit failed with exit code $LASTEXITCODE."
}
& git -C $toolRoot -c core.longpaths=true submodule update --init --recursive
if ($LASTEXITCODE -ne 0) {
    throw "Initialising the OpenSAGE Blender plugin submodules failed with exit code $LASTEXITCODE."
}
$pluginStatus = @(& git -C $toolRoot -c core.longpaths=true status --short --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $pluginStatus.Count -gt 0) {
    throw "The pinned OpenSAGE Blender plugin checkout is dirty: $($pluginStatus -join '; ')"
}
$dirtySubmodules = @(& git -C $toolRoot -c core.longpaths=true submodule foreach --recursive --quiet 'git status --short --untracked-files=all')
if ($LASTEXITCODE -ne 0 -or $dirtySubmodules.Count -gt 0) {
    throw "A pinned OpenSAGE Blender plugin submodule is dirty: $($dirtySubmodules -join '; ')"
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
Remove-Item -LiteralPath $resultPath -Force -ErrorAction SilentlyContinue

$env:TEMPEST_W3D_PLUGIN_ROOT = $toolRoot
$env:TEMPEST_W3D_OUTPUT_ROOT = $outputRoot
& $BlenderPath --background --factory-startup --python $pythonScript
$blenderExitCode = $LASTEXITCODE

# Blender can return success even when a Python script raises. The result sentinel is the authoritative test outcome.
if ($blenderExitCode -ne 0) {
    throw "Blender W3D pipeline failed with exit code $blenderExitCode."
}
if (-not (Test-Path -LiteralPath $resultPath)) {
    throw "The Blender W3D pipeline did not produce '$resultPath'. Review the Blender output above."
}

$result = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
if (-not $result.sha256 -or $result.mesh_count -lt 1 -or $result.vertex_count -lt 1) {
    throw "The Blender W3D pipeline result is incomplete: $($result | ConvertTo-Json -Compress)."
}

$result
