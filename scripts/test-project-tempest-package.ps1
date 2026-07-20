[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "build"))
$sessionRoot = Join-Path $buildRoot ("package-contract-test/" + [guid]::NewGuid().ToString("N"))
$runtimeDirectory = Join-Path $sessionRoot "runtime"
$firstOutput = Join-Path $sessionRoot "first"
$secondOutput = Join-Path $sessionRoot "second"
$contractPath = Join-Path $repositoryRoot "ProjectTempest/package-contract.json"
$packageScript = Join-Path $PSScriptRoot "package-project-tempest-demo.ps1"
$contract = Get-Content -LiteralPath $contractPath -Raw | ConvertFrom-Json
$milesSourcePath = Join-Path $repositoryRoot "ProjectTempest/ThirdParty/MilesStub/SOURCE.txt"
$milesSourceText = Get-Content -LiteralPath $milesSourcePath -Raw
$milesEntry = @($contract.runtime_files | Where-Object { $_.name -eq "mss32.dll" })
if ($milesEntry.Count -ne 1 -or
    ([string]$milesEntry[0].sha256).ToLowerInvariant() -notmatch '^[0-9a-f]{64}$' -or
    $milesSourceText -notmatch "(?im)^Expected mss32\.dll SHA-256:\s+$([regex]::Escape([string]$milesEntry[0].sha256))\s*$") {
    throw "The package contract and Miles stub source record do not agree on one governed SHA-256."
}

function Get-ZipEntryBytes {
    param(
        [Parameter(Mandatory = $true)]
        [IO.Compression.ZipArchive]$Archive,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )
    $entry = $Archive.GetEntry($Name)
    if ($null -eq $entry) {
        throw "Package archive is missing '$Name'."
    }
    $stream = $entry.Open()
    $memory = [IO.MemoryStream]::new()
    try {
        $stream.CopyTo($memory)
        return $memory.ToArray()
    }
    finally {
        $memory.Dispose()
        $stream.Dispose()
    }
}

try {
    New-Item -ItemType Directory -Path $runtimeDirectory -Force | Out-Null
    foreach ($entry in $contract.runtime_files) {
        $destination = Join-Path $runtimeDirectory ([string]$entry.name)
        if ($entry.kind -eq "asset") {
            Copy-Item -LiteralPath (Join-Path $repositoryRoot ([string]$entry.repository_path)) `
                -Destination $destination
        }
        elseif ($entry.kind -eq "third_party_notice") {
            $noticeName = ([string]$entry.name) -replace '^EA-Tunable-Colorblindness-', ''
            Copy-Item -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/ThirdParty/ElectronicArtsTunableColorblindness/$noticeName") `
                -Destination $destination
        }
        else {
            $bytes = if ($entry.kind -eq "executable") {
                [byte[]](0x4D, 0x5A, 0x50, 0x54, 0x44, 0x45, 0x4D, 0x4F)
            }
            else {
                [byte[]](0x4D, 0x53, 0x53, 0x53, 0x54, 0x55, 0x42)
            }
            [IO.File]::WriteAllBytes($destination, $bytes)
        }
    }

    $revision = "0123456789abcdef0123456789abcdef01234567"
    $epoch = 1760000000
    $fixtureDependencyHash = (Get-FileHash -LiteralPath (Join-Path $runtimeDirectory "mss32.dll") -Algorithm SHA256).Hash
    & $packageScript `
        -RuntimeDirectory $runtimeDirectory `
        -OutputDirectory $firstOutput `
        -SourceRevision $revision `
        -SourceDateEpoch $epoch `
        -TestFixture `
        -TestFixtureRuntimeDependencySha256 $fixtureDependencyHash
    & $packageScript `
        -RuntimeDirectory $runtimeDirectory `
        -OutputDirectory $secondOutput `
        -SourceRevision $revision `
        -SourceDateEpoch $epoch `
        -TestFixture `
        -TestFixtureRuntimeDependencySha256 $fixtureDependencyHash

    $firstArchive = Join-Path $firstOutput ([string]$contract.archive_name)
    $secondArchive = Join-Path $secondOutput ([string]$contract.archive_name)
    $firstHash = (Get-FileHash -LiteralPath $firstArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    $secondHash = (Get-FileHash -LiteralPath $secondArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($firstHash -ne $secondHash) {
        throw "Private demo package is not reproducible: first=$firstHash second=$secondHash."
    }

    $archive = [IO.Compression.ZipFile]::OpenRead($firstArchive)
    try {
        $prefix = "$($contract.package_directory)/"
        $entryNames = @($archive.Entries | ForEach-Object { $_.FullName })
        foreach ($entry in @($contract.runtime_files) + @($contract.repository_files)) {
            $expectedName = "$prefix$($entry.name)"
            if ($expectedName -notin $entryNames) {
                throw "Private package is missing governed entry '$expectedName'."
            }
        }
        foreach ($requiredMetadata in @("package-manifest.json", "SHA256SUMS.txt")) {
            if ("$prefix$requiredMetadata" -notin $entryNames) {
                throw "Private package is missing metadata '$requiredMetadata'."
            }
        }
        foreach ($pattern in $contract.forbidden_patterns) {
            if ($entryNames | Where-Object { [IO.Path]::GetFileName($_) -like $pattern }) {
                throw "Private package contains forbidden content matching '$pattern'."
            }
        }

        $utf8 = [Text.UTF8Encoding]::new($false)
        $manifestText = $utf8.GetString((Get-ZipEntryBytes -Archive $archive -Name "${prefix}package-manifest.json"))
        $manifest = $manifestText | ConvertFrom-Json
        if ($manifest.source_revision -ne $revision -or
            $manifest.source_date_epoch -ne $epoch -or
            $manifest.source_tree -ne "fixture" -or
            $manifest.renderer_execution -ne "not_performed" -or
            $manifest.manual_playthrough_claimed -ne $false) {
            throw "Private package manifest does not preserve the governed source/evidence state."
        }

        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            foreach ($file in $manifest.files) {
                $bytes = Get-ZipEntryBytes -Archive $archive -Name "$prefix$($file.name)"
                $actualHash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant()
                if ($actualHash -ne $file.sha256 -or $bytes.LongLength -ne $file.length) {
                    throw "Manifest verification failed for '$($file.name)'."
                }
            }
        }
        finally {
            $sha.Dispose()
        }
    }
    finally {
        $archive.Dispose()
    }

    $forbiddenFixture = Join-Path $runtimeDirectory "AudioZH.big"
    [IO.File]::WriteAllBytes($forbiddenFixture, [byte[]](0x45, 0x41))
    $rejectedOutput = Join-Path $sessionRoot "rejected"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $rejectedOutput `
            -SourceRevision $revision `
            -SourceDateEpoch $epoch `
            -TestFixture `
            -TestFixtureRuntimeDependencySha256 $fixtureDependencyHash
    }
    catch {
        $caught = $_.Exception.Message -match "forbidden"
    }
    if (-not $caught -or (Test-Path -LiteralPath $rejectedOutput)) {
        throw "The package gate did not reject a retail BIG archive before staging output."
    }

    Remove-Item -LiteralPath $forbiddenFixture -Force
    $dependencyMismatchOutput = Join-Path $sessionRoot "dependency-mismatch"
    $caught = $false
    try {
        & $packageScript `
            -RuntimeDirectory $runtimeDirectory `
            -OutputDirectory $dependencyMismatchOutput `
            -SourceRevision $revision `
            -SourceDateEpoch $epoch `
            -TestFixture `
            -TestFixtureRuntimeDependencySha256 ("0" * 64)
    }
    catch {
        $caught = $_.Exception.Message -match "governed source build"
    }
    if (-not $caught -or (Test-Path -LiteralPath $dependencyMismatchOutput)) {
        throw "The package gate did not reject a runtime dependency hash mismatch before staging output."
    }

    Write-Host "PASS: Project Tempest package contract and reproducibility"
    Write-Host "Fixture archive SHA256: $firstHash"
}
finally {
    if (Test-Path -LiteralPath $sessionRoot) {
        $resolvedSessionRoot = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $sessionRoot).Path)
        $buildPrefix = $buildRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) +
            [IO.Path]::DirectorySeparatorChar
        if (-not $resolvedSessionRoot.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean package test output outside '$buildRoot': '$resolvedSessionRoot'."
        }
        Remove-Item -LiteralPath $resolvedSessionRoot -Recurse -Force
    }
}
