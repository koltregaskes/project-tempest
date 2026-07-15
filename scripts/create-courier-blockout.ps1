[CmdletBinding()]
param(
    [string]$BlenderPath = "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$pluginRoot = Join-Path $env:LOCALAPPDATA "ProjectTempest\Tools\OpenSAGE.BlenderPlugin"
$pluginCommit = "feb80cd0bf22b3c24c0395ae3260a5349c080892"
$resultPath = Join-Path $repoRoot "build\courier-blockout\result.json"
$pythonScript = Join-Path $PSScriptRoot "create-courier-blockout.py"

if (-not (Test-Path -LiteralPath $BlenderPath)) {
    throw "Blender 5.1 was not found at '$BlenderPath'. Pass -BlenderPath with the installed blender.exe."
}

if (-not (Test-Path -LiteralPath (Join-Path $pluginRoot ".git"))) {
    throw "The pinned W3D plugin is not installed. Run scripts/test-w3d-pipeline.ps1 first."
}

$actualPluginCommit = (& git -C $pluginRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualPluginCommit -ne $pluginCommit) {
    throw "OpenSAGE Blender plugin must be pinned at $pluginCommit; found '$actualPluginCommit'."
}

Remove-Item -LiteralPath $resultPath -Force -ErrorAction SilentlyContinue
$env:TEMPEST_PROJECT_ROOT = $repoRoot
$env:TEMPEST_W3D_PLUGIN_ROOT = $pluginRoot
& $BlenderPath --background --factory-startup --python $pythonScript

# Blender may return success after a Python exception, so require a verified result sentinel.
if (-not (Test-Path -LiteralPath $resultPath)) {
    throw "Courier generation did not produce '$resultPath'. Review the Blender output above."
}

$result = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
if ($result.imported_mesh_count -lt 1 -or $result.imported_vertex_count -lt 1) {
    throw "Courier W3D roundtrip verification did not produce valid geometry."
}
foreach ($artifact in @($result.blend, $result.preview, $result.top_preview, $result.w3d)) {
    $artifactPath = Join-Path $repoRoot $artifact.path
    if (-not (Test-Path -LiteralPath $artifactPath)) {
        throw "Courier artifact is missing: '$artifactPath'."
    }
    $actualHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $artifact.sha256) {
        throw "Courier artifact hash mismatch for '$artifactPath'."
    }
}

$result
