[CmdletBinding()]
param(
    [string]$ManifestPath = "ProjectTempest/asset-provenance.json",
    [switch]$VerifyReproducibility,
    [string]$BlenderPath = "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
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
$seenAssetIds = @{}
foreach ($asset in $manifest.assets) {
    if ([string]::IsNullOrWhiteSpace($asset.asset_id) -or [string]::IsNullOrWhiteSpace($asset.path)) {
        throw "Every asset requires a non-empty asset_id and path."
    }
    if ($seenAssetIds.ContainsKey($asset.asset_id)) {
        throw "Duplicate asset_id: $($asset.asset_id)"
    }
    $seenAssetIds[$asset.asset_id] = $true

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

$requiredSubstationAssetIds = @(
    "PT-MODEL-CH-DRONE-001",
    "PT-PREVIEW-CH-DRONE-001",
    "PT-RUNTIME-CH-DRONE-001",
    "PT-MODEL-FG-RELAY-001",
    "PT-PREVIEW-FG-RELAY-001",
    "PT-RUNTIME-FG-RELAY-001",
    "PT-TEXTURE-CH-DRONE-001"
)
$recordedAssetIds = @($manifest.assets.asset_id)
$missingSubstationAssetIds = @($requiredSubstationAssetIds | Where-Object { $_ -notin $recordedAssetIds })
if ($missingSubstationAssetIds.Count -gt 0) {
    throw "Substation-kit provenance entries are missing: $($missingSubstationAssetIds -join ', ')"
}

$droneRuntimeAsset = $manifest.assets | Where-Object { $_.asset_id -eq "PT-RUNTIME-CH-DRONE-001" }
$relayRuntimeAsset = $manifest.assets | Where-Object { $_.asset_id -eq "PT-RUNTIME-FG-RELAY-001" }
$expectedKitCollisionFlags = "PHYSICAL,PROJECTILE,VEHICLE,VIS"
$expectedDroneMeshes = "DRBODY0,DRBODY1,DRBODY2,DRGLOW0,DRGLOW1,DRGLOW2,DRMAG0,DRMAG1,DRMAG2"
$expectedRelayMeshes = "HouseColor0,HouseColor1,HouseColor2,RLARMOR0,RLARMOR1,RLARMOR2,RLBODY0,RLBODY1,RLBODY2"

if (
    $null -eq $droneRuntimeAsset -or
    $droneRuntimeAsset.validation.roundtrip_import -ne "pass" -or
    $droneRuntimeAsset.validation.export_mode -ne "HM" -or
    $droneRuntimeAsset.validation.imported_render_mesh_count -ne 9 -or
    $droneRuntimeAsset.validation.imported_box_count -ne 1 -or
    $droneRuntimeAsset.validation.material_passes_per_render_mesh -ne 1 -or
    (@($droneRuntimeAsset.validation.authored_lod_vertex_counts) -join ",") -ne "640,345,194" -or
    (@($droneRuntimeAsset.validation.render_meshes) -join ",") -ne $expectedDroneMeshes -or
    (@($droneRuntimeAsset.validation.texture_files) -join ",") -ne "ptcyan.tga,ptmagnta.tga,ptsteel.tga" -or
    (@($droneRuntimeAsset.validation.collision_flags | Sort-Object) -join ",") -ne $expectedKitCollisionFlags -or
    $droneRuntimeAsset.review.automation_policy -ne "manual_only_never_unattended"
) {
    throw "The Chorus Drone three-LOD runtime/provenance contract is incomplete."
}

if (
    $null -eq $relayRuntimeAsset -or
    $relayRuntimeAsset.validation.roundtrip_import -ne "pass" -or
    $relayRuntimeAsset.validation.export_mode -ne "HM" -or
    $relayRuntimeAsset.validation.imported_render_mesh_count -ne 9 -or
    $relayRuntimeAsset.validation.imported_box_count -ne 1 -or
    $relayRuntimeAsset.validation.material_passes_per_render_mesh -ne 1 -or
    (@($relayRuntimeAsset.validation.authored_lod_vertex_counts) -join ",") -ne "636,350,191" -or
    (@($relayRuntimeAsset.validation.render_meshes) -join ",") -ne $expectedRelayMeshes -or
    (@($relayRuntimeAsset.validation.texture_files) -join ",") -ne "ptcyan.tga,ptsteel.tga,ptwhite.tga" -or
    (@($relayRuntimeAsset.validation.house_color_meshes) -join ",") -ne "HouseColor0,HouseColor1,HouseColor2" -or
    (@($relayRuntimeAsset.validation.collision_flags | Sort-Object) -join ",") -ne $expectedKitCollisionFlags -or
    $relayRuntimeAsset.review.automation_policy -ne "manual_only_never_unattended"
) {
    throw "The Freegrid Relay three-LOD runtime/provenance contract is incomplete."
}

$requiredAudioAssetIds = @(
    "PT-AUDIO-MUSIC-SUBSTATION-001",
    "PT-AUDIO-MUSIC-PRESSURE-001",
    "PT-AUDIO-MUSIC-CRISIS-001",
    "PT-AUDIO-SFX-CONFIRM-001",
    "PT-AUDIO-SFX-SELECT-001",
    "PT-AUDIO-SFX-COMMAND-001",
    "PT-AUDIO-SFX-ARC-PULSE-001",
    "PT-AUDIO-SFX-ALERT-001"
)
$audioAssets = @($manifest.assets | Where-Object { $_.asset_id -in $requiredAudioAssetIds })
if ($audioAssets.Count -ne $requiredAudioAssetIds.Count) {
    $missingAudioAssetIds = @($requiredAudioAssetIds | Where-Object { $_ -notin $audioAssets.asset_id })
    throw "Original audio provenance entries are missing: $($missingAudioAssetIds -join ', ')"
}
foreach ($audioAsset in $audioAssets) {
    if (
        $audioAsset.origin -ne "procedurally_authored_original" -or
        $audioAsset.rights_basis -ne "original synthesis with no third-party samples or generated-service inputs" -or
        $audioAsset.validation.format -ne "PCM16 stereo 48000 Hz" -or
        $audioAsset.validation.repeat_generation_hash_stable -ne $true
    ) {
        throw "Audio provenance/format contract is incomplete for $($audioAsset.asset_id)."
    }

    $audioPath = Join-Path $contentRoot $audioAsset.path
    $bytes = [IO.File]::ReadAllBytes($audioPath)
    if (
        $bytes.Length -lt 44 -or
        [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne "RIFF" -or
        [BitConverter]::ToUInt32($bytes, 4) + 8 -ne $bytes.Length -or
        [Text.Encoding]::ASCII.GetString($bytes, 8, 4) -ne "WAVE" -or
        [Text.Encoding]::ASCII.GetString($bytes, 12, 4) -ne "fmt " -or
        [BitConverter]::ToUInt32($bytes, 16) -ne 16 -or
        [BitConverter]::ToUInt16($bytes, 20) -ne 1 -or
        [BitConverter]::ToUInt16($bytes, 22) -ne 2 -or
        [BitConverter]::ToUInt32($bytes, 24) -ne 48000 -or
        [BitConverter]::ToUInt16($bytes, 34) -ne 16 -or
        [Text.Encoding]::ASCII.GetString($bytes, 36, 4) -ne "data" -or
        [BitConverter]::ToUInt32($bytes, 40) + 44 -ne $bytes.Length
    ) {
        throw "Audio file is not canonical stereo 48 kHz PCM16 WAV: $($audioAsset.path)"
    }
}

$audioGenerator = Join-Path $repositoryRoot "scripts/create-tempest-audio.py"
if (-not (Test-Path -LiteralPath $audioGenerator -PathType Leaf)) {
    throw "The deterministic original-audio generator is missing."
}
$audioReproRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "build/project-tempest-audio-repro"))
$buildPrefix = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "build")).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $audioReproRoot.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Audio reproducibility output escaped the repository build tree."
}
$audioReportJson = if (Get-Command python -ErrorAction SilentlyContinue) {
    & python $audioGenerator --output-root $audioReproRoot
} elseif (Get-Command python3 -ErrorAction SilentlyContinue) {
    & python3 $audioGenerator --output-root $audioReproRoot
} else {
    throw "Python 3 is required for deterministic original-audio validation."
}
if ($LASTEXITCODE -ne 0) {
    throw "Original-audio deterministic regeneration failed with exit code $LASTEXITCODE."
}
$audioReport = ($audioReportJson -join [Environment]::NewLine) | ConvertFrom-Json
foreach ($audioAsset in $audioAssets) {
    $expectedReportPath = "ProjectTempest/$($audioAsset.path)"
    $reproduced = $audioReport.assets | Where-Object { $_.path -eq $expectedReportPath }
    if ($null -eq $reproduced -or $reproduced.sha256 -ne $audioAsset.sha256) {
        throw "Audio regeneration hash mismatch for $($audioAsset.asset_id)."
    }
}

$cmakeContent = Get-Content -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/CMakeLists.txt") -Raw
$ciContent = Get-Content -LiteralPath (Join-Path $repositoryRoot ".github/workflows/ci.yml") -Raw
$buildToolchainContent = Get-Content -LiteralPath (Join-Path $repositoryRoot ".github/workflows/build-toolchain.yml") -Raw
if ($ciContent -notmatch [regex]::Escape("scripts/create-tempest-audio.py")) {
    throw "Project Tempest CI path filtering must include the deterministic audio generator."
}
if ($buildToolchainContent -notmatch '(?s)ProjectTempest.*?Where-Object\s*\{[^}]*?"\.wav"') {
    throw "Win32 Project Tempest artifacts must include WAV runtime audio assets."
}
foreach ($packagedAsset in @(
    "drone.w3d",
    "relay.w3d",
    "ptmagnta.tga",
    "pt_music_substation.wav",
    "pt_music_pressure.wav",
    "pt_music_crisis.wav",
    "pt_ui_confirm.wav",
    "pt_select.wav",
    "pt_command.wav",
    "pt_arc_pulse.wav",
    "pt_alert.wav"
)) {
    if ($cmakeContent -notmatch [regex]::Escape($packagedAsset)) {
        throw "Project Tempest package contract is missing '$packagedAsset'."
    }
}

$demoSource = Get-Content -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/Code/ProjectTempestDemo.cpp") -Raw
foreach ($requiredLoad in @("Load_3D_Assets(`"drone.w3d`")", "Load_3D_Assets(`"relay.w3d`")")) {
    if ($demoSource -notmatch [regex]::Escape($requiredLoad)) {
        throw "Project Tempest renderer does not declare required asset load '$requiredLoad'."
    }
}
if (
    $demoSource -notmatch [regex]::Escape('Create_Render_Obj("relay")') -or
    $demoSource -notmatch [regex]::Escape('isChorus ? "drone"')
) {
    throw "Project Tempest renderer does not map Dynamo/Chorus simulation entities to their dedicated runtime models."
}
if (
    $demoSource -notmatch [regex]::Escape('const HGDIOBJ previousFont = SelectObject(device, font);') -or
    $demoSource -notmatch [regex]::Escape('SelectObject(device, previousFont);')
) {
    throw "Project Tempest HUD drawing must restore the prior GDI font before scalable fonts can be deleted."
}
foreach ($mouseCaptureContract in @(
    "SetCapture(window)",
    "ReleaseCapture()",
    "WM_CAPTURECHANGED",
    "WM_CANCELMODE",
    "ClearHeldMouseButtons(window, true)"
)) {
    if ($demoSource -notmatch [regex]::Escape($mouseCaptureContract)) {
        throw "Project Tempest mouse capture lifecycle is missing '$mouseCaptureContract'."
    }
}

foreach ($interfaceSource in @("Code/TempestInterface.cpp", "Code/TempestInterface.h")) {
    if ($cmakeContent -notmatch [regex]::Escape($interfaceSource)) {
        throw "Project Tempest CMake contract is missing '$interfaceSource'."
    }
}
$combinedAudioSource = $demoSource +
    (Get-Content -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/Code/TempestAudio.cpp") -Raw) +
    (Get-Content -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/Code/TempestAudioWin32.cpp") -Raw)
foreach ($audioContract in @(
    "XAudio2Create",
    "StartMusic",
    "SetMusicPressure",
    "MusicGainsForPressure",
    "SetSuspended",
    "VolumeGain",
    "playbackFailed",
    "pt_music_substation.wav",
    "Tempest::Audio::Cue::ArcPulse"
)) {
    if ($combinedAudioSource -notmatch [regex]::Escape($audioContract)) {
        throw "Project Tempest audio implementation is missing '$audioContract'."
    }
}
$combinedInterfaceSource = $demoSource +
    (Get-Content -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/Code/TempestInterface.cpp") -Raw)
foreach ($interfaceContract in @(
    "DrawHud",
    "DrawSettingsOverlay",
    "DrawModalOverlay",
    "SyncOutcome",
    "colourIndependentCues",
    "RestartMatch",
    "SerializeConfiguration",
    "LoadConfiguration",
    "HandleMouseButton",
    "PrimarySelect",
    "ContextCommand",
    "MoveFileExW"
)) {
    if ($combinedInterfaceSource -notmatch [regex]::Escape($interfaceContract)) {
        throw "Project Tempest interface contract is missing '$interfaceContract'."
    }
}
$simulationHeader = Get-Content -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/Code/TempestSimulation.h") -Raw
$simulationSource = Get-Content -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/Code/TempestSimulation.cpp") -Raw
foreach ($contentRole in @(
    "FabricatorRig", "CourierScout", "LancerCrew", "CoilCarrier",
    "Skitter", "Warden", "Harrower", "RelayCore", "FabricatorBay",
    "Dynamo", "ArcSentry", "MachineNest", "SignalPylon", "ChorusSpire",
    "GridLinkScan", "EmergencyOvercharge", "salvage", "abilityCharge"
)) {
    if (($simulationHeader + $simulationSource) -notmatch [regex]::Escape($contentRole)) {
        throw "Project Tempest content model is missing '$contentRole'."
    }
}
if (($demoSource + $simulationHeader + $simulationSource) -match
    'freegridCredits|freegridPower|BuildingKind::Workshop|BuildingKind::ChorusCore') {
    throw "Project Tempest still contains retired credits/power or Workshop/Chorus Core vocabulary."
}

$validated | Format-Table -AutoSize
Write-Host "Validated $($validated.Count) Project Tempest assets, runtime/interface contracts, and the manual-only renderer policy."

if ($VerifyReproducibility) {
    $reproducibilityScript = Join-Path $PSScriptRoot "test-project-tempest-reproducibility.ps1"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $reproducibilityScript -BlenderPath $BlenderPath
    if ($LASTEXITCODE -ne 0) {
        throw "Project Tempest runtime-asset reproducibility verification failed with exit code $LASTEXITCODE."
    }
} else {
    Write-Host "Runtime reproducibility generation was not requested; use -VerifyReproducibility for the release gate."
}
