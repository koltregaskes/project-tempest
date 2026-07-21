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
if (@($result.assets).Count -ne 13) {
    throw "Substation-kit generation must produce all thirteen governed unit and structure assets."
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
    sentry = @{
        Meshes = @("HouseColor0", "HouseColor1", "HouseColor2", "STARMOR0", "STARMOR1", "STARMOR2", "STBODY0", "STBODY1", "STBODY2")
        Textures = @("ptcyan.tga", "ptsteel.tga", "ptwhite.tga")
        HouseColor = @("HouseColor0", "HouseColor1", "HouseColor2")
    }
    pylon = @{
        Meshes = @("PYBODY0", "PYBODY1", "PYBODY2", "PYGLOW0", "PYGLOW1", "PYGLOW2", "PYMAG0", "PYMAG1", "PYMAG2")
        Textures = @("ptcyan.tga", "ptmagnta.tga", "ptsteel.tga")
        HouseColor = @()
    }
    relaycore = @{
        Meshes = @("HouseColor0", "HouseColor1", "HouseColor2", "RCARMOR0", "RCARMOR1", "RCARMOR2", "RCBODY0", "RCBODY1", "RCBODY2")
        Textures = @("ptcyan.tga", "ptsteel.tga", "ptwhite.tga")
        HouseColor = @("HouseColor0", "HouseColor1", "HouseColor2")
    }
    fabricbay = @{
        Meshes = @("FBARMOR0", "FBARMOR1", "FBARMOR2", "FBBODY0", "FBBODY1", "FBBODY2", "HouseColor0", "HouseColor1", "HouseColor2")
        Textures = @("ptcyan.tga", "ptsteel.tga", "ptwhite.tga")
        HouseColor = @("HouseColor0", "HouseColor1", "HouseColor2")
    }
    spire = @{
        Meshes = @("CSBODY0", "CSBODY1", "CSBODY2", "CSGLOW0", "CSGLOW1", "CSGLOW2", "CSMAG0", "CSMAG1", "CSMAG2")
        Textures = @("ptcyan.tga", "ptmagnta.tga", "ptsteel.tga")
        HouseColor = @()
    }
    fabricrig = @{
        Meshes = @("FRARMOR0", "FRARMOR1", "FRARMOR2", "FRBODY0", "FRBODY1", "FRBODY2", "HouseColor0", "HouseColor1", "HouseColor2")
        Textures = @("ptcyan.tga", "ptsteel.tga", "ptwhite.tga")
        HouseColor = @("HouseColor0", "HouseColor1", "HouseColor2")
    }
    lancer = @{
        Meshes = @("HouseColor0", "HouseColor1", "HouseColor2", "LNARMOR0", "LNARMOR1", "LNARMOR2", "LNBODY0", "LNBODY1", "LNBODY2")
        Textures = @("ptcyan.tga", "ptsteel.tga", "ptwhite.tga")
        HouseColor = @("HouseColor0", "HouseColor1", "HouseColor2")
    }
    coil = @{
        Meshes = @("CCARMOR0", "CCARMOR1", "CCARMOR2", "CCBODY0", "CCBODY1", "CCBODY2", "HouseColor0", "HouseColor1", "HouseColor2")
        Textures = @("ptcyan.tga", "ptsteel.tga", "ptwhite.tga")
        HouseColor = @("HouseColor0", "HouseColor1", "HouseColor2")
    }
    warden = @{
        Meshes = @("WDBODY0", "WDBODY1", "WDBODY2", "WDGLOW0", "WDGLOW1", "WDGLOW2", "WDMAG0", "WDMAG1", "WDMAG2")
        Textures = @("ptcyan.tga", "ptmagnta.tga", "ptsteel.tga")
        HouseColor = @()
    }
    harrower = @{
        Meshes = @("HABODY0", "HABODY1", "HABODY2", "HAGLOW0", "HAGLOW1", "HAGLOW2", "HAMAG0", "HAMAG1", "HAMAG2")
        Textures = @("ptcyan.tga", "ptmagnta.tga", "ptsteel.tga")
        HouseColor = @()
    }
    nest = @{
        Meshes = @("MNBODY0", "MNBODY1", "MNBODY2", "MNGLOW0", "MNGLOW1", "MNGLOW2", "MNMAG0", "MNMAG1", "MNMAG2")
        Textures = @("ptcyan.tga", "ptmagnta.tga", "ptsteel.tga")
        HouseColor = @()
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
