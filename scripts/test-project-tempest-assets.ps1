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

if ($runtimeAsset.validation.imported_render_mesh_count -ne 4 -or $runtimeAsset.validation.imported_box_count -ne 1) {
    throw "The Courier runtime asset must record four imported LOD/team meshes and one collision box."
}

$requiredCollisionFlags = @("PHYSICAL", "PROJECTILE", "VEHICLE", "VIS")
$recordedCollisionFlags = @($runtimeAsset.validation.collision_flags | Sort-Object)
if (($requiredCollisionFlags -join ",") -ne ($recordedCollisionFlags -join ",")) {
    throw "The Courier collision validation flags are incomplete: $($recordedCollisionFlags -join ', ')"
}

if (@($runtimeAsset.validation.recolor_materials).Count -ne 2) {
    throw "The Courier runtime asset must record one house-colour material for each LOD."
}

$validated | Format-Table -AutoSize
Write-Host "Validated $($validated.Count) Project Tempest assets and the Courier HM/LOD/collision/team-colour contract."
