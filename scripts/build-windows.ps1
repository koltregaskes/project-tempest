[CmdletBinding()]
param(
    [ValidateSet("Configure", "Build", "All")]
    [string]$Action = "All",

    [ValidateSet("win32", "win32-debug", "win32-profile")]
    [string]$Preset = "win32",

    [ValidateRange(1, 64)]
    [int]$Parallel = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer was not found at '$vswhere'. Install Visual Studio 2022 Build Tools with Desktop development with C++."
}

$vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsInstall) {
    throw "Visual Studio 2022 with the x86/x64 C++ toolchain was not found."
}

$devShellModule = Join-Path $vsInstall "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
$cmake = Join-Path $vsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninjaDirectory = Join-Path $vsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"

foreach ($requiredPath in @($devShellModule, $cmake, (Join-Path $ninjaDirectory "ninja.exe"))) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required Visual Studio build tool was not found at '$requiredPath'. Modify the Build Tools installation to include CMake tools for Windows."
    }
}

Import-Module $devShellModule
Enter-VsDevShell -VsInstallPath $vsInstall -SkipAutomaticLocation -DevCmdArguments "-arch=x86 -host_arch=x64"
$env:PATH = "$ninjaDirectory;$env:PATH"

if (-not $env:VCToolsInstallDir) {
    throw "Visual Studio's developer shell did not set VCToolsInstallDir for the x86 toolchain."
}

$atlHeader = Join-Path $env:VCToolsInstallDir "atlmfc\include\atlbase.h"
if (-not (Test-Path -LiteralPath $atlHeader)) {
    $installer = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\setup.exe"
    throw @"
The Visual C++ ATL headers are missing at '$atlHeader'.
Open an elevated PowerShell and install the component, then rerun this script:
& '$installer' modify --installPath '$vsInstall' --add Microsoft.VisualStudio.Component.VC.ATL --quiet --norestart
"@
}

Push-Location $repoRoot
try {
    & git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) {
        throw "Git submodule initialisation failed with exit code $LASTEXITCODE."
    }

    if ($Action -in @("Configure", "All")) {
        & $cmake --preset $Preset
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed with exit code $LASTEXITCODE."
        }
    }

    if ($Action -in @("Build", "All")) {
        & $cmake --build --preset $Preset --parallel $Parallel
        if ($LASTEXITCODE -ne 0) {
            throw "CMake build failed with exit code $LASTEXITCODE."
        }
    }
}
finally {
    Pop-Location
}
