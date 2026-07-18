[CmdletBinding()]
param(
    [string]$BlenderPath = "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$pluginRoot = Join-Path $env:LOCALAPPDATA "ProjectTempest\Tools\OpenSAGE.BlenderPlugin"
$pluginCommit = "feb80cd0bf22b3c24c0395ae3260a5349c080892"
$buildRoot = Join-Path $repositoryRoot "build"
$sessionRoot = Join-Path $buildRoot ("asset-reproducibility\" + [guid]::NewGuid().ToString("N"))
$previousOutputRoot = $env:TEMPEST_OUTPUT_ROOT
$previousProjectRoot = $env:TEMPEST_PROJECT_ROOT
$previousPluginRoot = $env:TEMPEST_W3D_PLUGIN_ROOT
$completed = $false

if (-not (Test-Path -LiteralPath $BlenderPath -PathType Leaf)) {
    throw "Blender 5.1 was not found at '$BlenderPath'. Pass -BlenderPath with the installed blender.exe."
}
if (-not (Test-Path -LiteralPath (Join-Path $pluginRoot ".git") -PathType Container)) {
    throw "The pinned OpenSAGE Blender plugin is missing at '$pluginRoot'. Run test-w3d-pipeline.ps1 first."
}

$actualPluginCommit = (& git -C $pluginRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualPluginCommit -ne $pluginCommit) {
    throw "OpenSAGE Blender plugin must be pinned at $pluginCommit; found '$actualPluginCommit'."
}
$pluginStatus = @(& git -C $pluginRoot status --short --untracked-files=all)
$submoduleStatus = @(& git -C $pluginRoot submodule foreach --recursive --quiet 'git status --short --untracked-files=all')
if ($LASTEXITCODE -ne 0 -or $pluginStatus.Count -gt 0 -or $submoduleStatus.Count -gt 0) {
    throw "Reproducibility verification requires a clean pinned plugin and submodules."
}

function Invoke-IsolatedGenerator {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PythonScript,
        [Parameter(Mandatory = $true)]
        [string]$OutputRoot,
        [Parameter(Mandatory = $true)]
        [string]$ResultRelativePath
    )

    New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
    $env:TEMPEST_PROJECT_ROOT = $repositoryRoot
    $env:TEMPEST_OUTPUT_ROOT = $OutputRoot
    $env:TEMPEST_W3D_PLUGIN_ROOT = $pluginRoot
    & $BlenderPath --background --factory-startup --python $PythonScript
    $blenderExitCode = $LASTEXITCODE
    $resultPath = Join-Path $OutputRoot $ResultRelativePath
    if ($blenderExitCode -ne 0) {
        throw "Isolated Blender generator '$PythonScript' failed with exit code $blenderExitCode. Evidence: '$OutputRoot'."
    }
    if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
        throw "Isolated Blender generator '$PythonScript' did not produce '$resultPath'."
    }
    return Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
}

try {
    $runA = Join-Path $sessionRoot "run-a"
    $runB = Join-Path $sessionRoot "run-b"

    [void](Invoke-IsolatedGenerator `
        -PythonScript (Join-Path $PSScriptRoot "create-courier-blockout.py") `
        -OutputRoot $runA `
        -ResultRelativePath "build\courier-blockout\result.json")
    [void](Invoke-IsolatedGenerator `
        -PythonScript (Join-Path $PSScriptRoot "create-courier-blockout.py") `
        -OutputRoot $runB `
        -ResultRelativePath "build\courier-blockout\result.json")
    [void](Invoke-IsolatedGenerator `
        -PythonScript (Join-Path $PSScriptRoot "create-substation-kit.py") `
        -OutputRoot $runA `
        -ResultRelativePath "build\substation-kit\result.json")
    [void](Invoke-IsolatedGenerator `
        -PythonScript (Join-Path $PSScriptRoot "create-substation-kit.py") `
        -OutputRoot $runB `
        -ResultRelativePath "build\substation-kit\result.json")

    $runtimeArtifacts = @(
        @{ Name = "courier.w3d"; RelativePath = "ProjectTempest\Content\Art\W3D\courier.w3d"; A = $runA; B = $runB },
        @{ Name = "courierd.w3d"; RelativePath = "ProjectTempest\Content\Art\W3D\courierd.w3d"; A = $runA; B = $runB },
        @{ Name = "ptsteel.tga"; RelativePath = "ProjectTempest\Content\Art\Textures\ptsteel.tga"; A = $runA; B = $runB },
        @{ Name = "ptwhite.tga"; RelativePath = "ProjectTempest\Content\Art\Textures\ptwhite.tga"; A = $runA; B = $runB },
        @{ Name = "ptrubber.tga"; RelativePath = "ProjectTempest\Content\Art\Textures\ptrubber.tga"; A = $runA; B = $runB },
        @{ Name = "ptcyan.tga"; RelativePath = "ProjectTempest\Content\Art\Textures\ptcyan.tga"; A = $runA; B = $runB },
        @{ Name = "ptcable.tga"; RelativePath = "ProjectTempest\Content\Art\Textures\ptcable.tga"; A = $runA; B = $runB },
        @{ Name = "ptburn.tga"; RelativePath = "ProjectTempest\Content\Art\Textures\ptburn.tga"; A = $runA; B = $runB },
        @{ Name = "ptoff.tga"; RelativePath = "ProjectTempest\Content\Art\Textures\ptoff.tga"; A = $runA; B = $runB },
        @{ Name = "drone.w3d"; RelativePath = "ProjectTempest\Content\Art\W3D\drone.w3d"; A = $runA; B = $runB },
        @{ Name = "relay.w3d"; RelativePath = "ProjectTempest\Content\Art\W3D\relay.w3d"; A = $runA; B = $runB },
        @{ Name = "sentry.w3d"; RelativePath = "ProjectTempest\Content\Art\W3D\sentry.w3d"; A = $runA; B = $runB },
        @{ Name = "pylon.w3d"; RelativePath = "ProjectTempest\Content\Art\W3D\pylon.w3d"; A = $runA; B = $runB },
        @{ Name = "ptmagnta.tga"; RelativePath = "ProjectTempest\Content\Art\Textures\ptmagnta.tga"; A = $runA; B = $runB }
    )

    $verified = foreach ($artifact in $runtimeArtifacts) {
        $pathA = Join-Path $artifact.A $artifact.RelativePath
        $pathB = Join-Path $artifact.B $artifact.RelativePath
        $committedPath = Join-Path $repositoryRoot $artifact.RelativePath
        foreach ($path in @($pathA, $pathB, $committedPath)) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "Reproducibility artifact is missing: '$path'."
            }
        }
        $hashA = (Get-FileHash -LiteralPath $pathA -Algorithm SHA256).Hash.ToLowerInvariant()
        $hashB = (Get-FileHash -LiteralPath $pathB -Algorithm SHA256).Hash.ToLowerInvariant()
        $committedHash = (Get-FileHash -LiteralPath $committedPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hashA -ne $hashB -or $hashA -ne $committedHash) {
            throw "Runtime asset '$($artifact.Name)' is not reproducible: A=$hashA B=$hashB committed=$committedHash."
        }
        [pscustomobject]@{ Asset = $artifact.Name; SHA256 = $hashA }
    }

    $verified | Format-Table -AutoSize
    Write-Host "Verified $($verified.Count) runtime assets through two isolated Blender processes/output roots each."
    $completed = $true
}
finally {
    $env:TEMPEST_OUTPUT_ROOT = $previousOutputRoot
    $env:TEMPEST_PROJECT_ROOT = $previousProjectRoot
    $env:TEMPEST_W3D_PLUGIN_ROOT = $previousPluginRoot

    if ($completed -and (Test-Path -LiteralPath $sessionRoot)) {
        $resolvedBuildRoot = [IO.Path]::GetFullPath($buildRoot).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
        $resolvedSessionRoot = [IO.Path]::GetFullPath($sessionRoot)
        if (-not $resolvedSessionRoot.StartsWith($resolvedBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean reproducibility output outside '$resolvedBuildRoot': '$resolvedSessionRoot'."
        }
        Remove-Item -LiteralPath $resolvedSessionRoot -Recurse -Force
    } elseif (-not $completed) {
        Write-Warning "Reproducibility evidence was preserved at '$sessionRoot' because validation failed."
    }
}
