[CmdletBinding()]
param(
    [string]$BlenderPath = "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$pluginRoot = Join-Path $env:LOCALAPPDATA "ProjectTempest\Tools\OpenSAGE.BlenderPlugin"
$pluginCommit = "feb80cd0bf22b3c24c0395ae3260a5349c080892"
$resultPath = Join-Path $repoRoot "build\substation-kit\result.json"
$pythonScript = Join-Path $PSScriptRoot "create-substation-kit.py"

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
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $resultPath)) {
    throw "Substation-kit generation failed or did not produce '$resultPath'."
}

$result = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
if (@($result.assets).Count -ne 2) {
    throw "Substation-kit generation must produce the Drone and Relay."
}
$expectedContracts = @{
    drone = @{
        Meshes = @("DRBODY0", "DRBODY1", "DRBODY2", "DRGLOW0", "DRGLOW1", "DRGLOW2", "DRMAG0", "DRMAG1", "DRMAG2")
        Textures = @("ptcyan.tga", "ptmagnta.tga", "ptsteel.tga")
        HouseColor = @()
    }
    relay = @{
        Meshes = @("HouseColor0", "HouseColor1", "HouseColor2", "RLARMOR0", "RLARMOR1", "RLARMOR2", "RLBODY0", "RLBODY1", "RLBODY2")
        Textures = @("ptcyan.tga", "ptsteel.tga", "ptwhite.tga")
        HouseColor = @("HouseColor0", "HouseColor1", "HouseColor2")
    }
}
foreach ($asset in $result.assets) {
    $contract = $expectedContracts[$asset.name]
    if ($null -eq $contract) {
        throw "Unexpected Substation-kit asset '$($asset.name)'."
    }
    if ($asset.imported_render_mesh_count -ne 9 -or $asset.imported_box_count -ne 1) {
        throw "$($asset.name) W3D roundtrip mesh/collision contract failed."
    }
    if ($asset.max_material_passes_per_render_mesh -ne 1) {
        throw "$($asset.name) must use exactly one material pass per render mesh."
    }
    if ((@($asset.imported_collision_flags | Sort-Object) -join ",") -ne "PHYSICAL,PROJECTILE,VEHICLE,VIS") {
        throw "$($asset.name) collision flags are incomplete."
    }
    if ((@($asset.imported_render_mesh_names) -join ",") -ne ($contract.Meshes -join ",")) {
        throw "$($asset.name) runtime mesh-name contract failed."
    }
    if ((@($asset.imported_texture_files) -join ",") -ne ($contract.Textures -join ",")) {
        throw "$($asset.name) texture roundtrip contract failed."
    }
    if ((@($asset.house_color_meshes) -join ",") -ne ($contract.HouseColor -join ",")) {
        throw "$($asset.name) house-colour mesh contract failed."
    }
    $counts = @($asset.authored_lod_vertex_counts)
    if ($counts.Count -ne 3 -or -not ($counts[0] -gt $counts[1] -and $counts[1] -gt $counts[2])) {
        throw "$($asset.name) does not contain three strictly decreasing LOD states."
    }
    foreach ($artifact in @($asset.blend, $asset.preview, $asset.w3d)) {
        $artifactPath = Join-Path $repoRoot $artifact.path
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            throw "Missing generated artifact '$artifactPath'."
        }
        $actualHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $artifact.sha256) {
            throw "Hash mismatch for '$artifactPath'."
        }
    }
}

$texturePath = Join-Path $repoRoot $result.texture.path
$textureHash = (Get-FileHash -LiteralPath $texturePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($textureHash -ne $result.texture.sha256) {
    throw "Hash mismatch for the Chorus magenta texture."
}

$result
