[CmdletBinding()]
param(
    [string]$ManifestPath = "ProjectTempest/asset-provenance.json"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$resolvedManifestPath = (Resolve-Path (Join-Path $repositoryRoot $ManifestPath)).Path
$contentRoot = (Resolve-Path (Join-Path $repositoryRoot "ProjectTempest")).Path
$manifest = Get-Content -LiteralPath $resolvedManifestPath -Raw | ConvertFrom-Json

if ($manifest.schema_version -ne 1) {
    throw "Unsupported Project Tempest asset manifest schema: $($manifest.schema_version)"
}

if ($manifest.public_distribution_default -ne "prohibited_until_reviewed") {
    throw "Project Tempest assets must remain prohibited from public distribution until reviewed."
}

$validated = @()
foreach ($asset in $manifest.assets) {
    if ([string]::IsNullOrWhiteSpace($asset.asset_id) -or [string]::IsNullOrWhiteSpace($asset.path)) {
        throw "Every asset requires a non-empty asset_id and path."
    }

    $candidatePath = [IO.Path]::GetFullPath((Join-Path $contentRoot $asset.path))
    $contentPrefix = $contentRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $candidatePath.StartsWith($contentPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Asset path escapes ProjectTempest: $($asset.path)"
    }

    if (-not (Test-Path -LiteralPath $candidatePath -PathType Leaf)) {
        throw "Missing asset $($asset.asset_id): $candidatePath"
    }

    $actualHash = (Get-FileHash -LiteralPath $candidatePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $asset.sha256) {
        throw "Hash mismatch for $($asset.asset_id): expected $($asset.sha256), actual $actualHash"
    }

    if ($asset.distribution -ne "internal_development_only") {
        throw "Asset $($asset.asset_id) is not explicitly restricted to internal development."
    }

    $validated += [pscustomobject]@{
        Asset = $asset.asset_id
        Bytes = (Get-Item -LiteralPath $candidatePath).Length
        SHA256 = $actualHash
    }
}

if ($validated.Count -eq 0) {
    throw "The Project Tempest asset manifest is empty."
}

$runtimeAsset = $manifest.assets | Where-Object { $_.asset_id -eq "PT-RUNTIME-FG-COURIER-001" }
if ($null -eq $runtimeAsset -or $runtimeAsset.validation.export_mode -ne "HM") {
    throw "The Courier runtime asset must record a validated W3D HM export."
}

if ($runtimeAsset.validation.imported_render_mesh_count -ne 12 -or $runtimeAsset.validation.imported_box_count -ne 1) {
    throw "The Courier runtime asset must record twelve single-material LOD meshes and one collision box."
}

$requiredCollisionFlags = @("PHYSICAL", "PROJECTILE", "VEHICLE", "VIS")
$recordedCollisionFlags = @($runtimeAsset.validation.collision_flags | Sort-Object)
if (($requiredCollisionFlags -join ",") -ne ($recordedCollisionFlags -join ",")) {
    throw "The Courier collision validation flags are incomplete: $($recordedCollisionFlags -join ', ')"
}

if ($runtimeAsset.validation.material_passes_per_render_mesh -ne 1) {
    throw "Every Courier render submesh must use one material pass for Generals engine compatibility."
}

$expectedPristineTextures = @("ptcable.tga", "ptcyan.tga", "ptrubber.tga", "ptsteel.tga", "ptwhite.tga")
$recordedPristineTextures = @($runtimeAsset.validation.texture_files | Sort-Object)
if (($expectedPristineTextures -join ",") -ne ($recordedPristineTextures -join ",")) {
    throw "The Courier pristine W3D texture roundtrip is incomplete: $($recordedPristineTextures -join ', ')"
}

if ((@($runtimeAsset.validation.house_color_meshes) -join ",") -ne "HouseColor0,HouseColor1") {
    throw "The Courier runtime asset must use the native HouseColor mesh convention in both LODs."
}

if ($runtimeAsset.review.engine_render -ne "manual_capture_only_unstable_under_rdp") {
    throw "The Courier renderer record must preserve the manual-only unstable-RDP status."
}

if ($runtimeAsset.review.automation_policy -ne "manual_only_never_unattended") {
    throw "Interactive Project Tempest rendering must never run during unattended validation."
}

if ($runtimeAsset.review.rdp_stability -ne "blocked_repeated_application_error_1000") {
    throw "The known W3D Viewer RDP stability blocker must remain explicit."
}

$damagedRuntimeAsset = $manifest.assets | Where-Object { $_.asset_id -eq "PT-RUNTIME-FG-COURIER-002" }
if (
    $null -eq $damagedRuntimeAsset -or
    $damagedRuntimeAsset.validation.export_mode -ne "HM" -or
    $damagedRuntimeAsset.validation.roundtrip_import -ne "pass" -or
    $damagedRuntimeAsset.validation.imported_render_mesh_count -ne 14 -or
    $damagedRuntimeAsset.validation.imported_box_count -ne 1 -or
    (@($damagedRuntimeAsset.validation.damage_meshes) -join ",") -ne "CRDMG0,CRDMG1" -or
    $damagedRuntimeAsset.validation.damage_texture -ne "ptburn.tga" -or
    $damagedRuntimeAsset.validation.powered_off_texture -ne "ptoff.tga"
) {
    throw "The Courier damaged-state HLOD contract is incomplete."
}

$textureAssets = @($manifest.assets | Where-Object { $_.asset_id -like "PT-TEXTURE-FG-COURIER-*" })
if ($textureAssets.Count -ne 7) {
    throw "The Courier requires exactly seven original, provenance-tracked runtime textures; found $($textureAssets.Count)."
}

$validated | Format-Table -AutoSize
Write-Host "Validated $($validated.Count) Project Tempest assets, textured pristine/damaged Courier HLODs, and the manual-only renderer policy."
