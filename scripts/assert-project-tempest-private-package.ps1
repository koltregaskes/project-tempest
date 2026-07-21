[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath,

    [Parameter(Mandatory = $true)]
    [string]$InstallDirectory,

    [Parameter(Mandatory = $true)]
    [string]$ReceiptPath,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedBuildSourceRevision,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedReviewedSourceRevision,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{64}$')]
    [string]$ExpectedExecutableSha256,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{64}$')]
    [string]$ExpectedMilesStubSha256,

    [ValidateSet("private_internal_demo", "test_fixture")]
    [string]$ExpectedDistribution = "private_internal_demo",

    [ValidateSet("clean", "fixture")]
    [string]$ExpectedSourceTree = "clean"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$contractPath = Join-Path $repositoryRoot "ProjectTempest/package-contract.json"
$provenancePath = Join-Path $repositoryRoot "ProjectTempest/asset-provenance.json"
$resolvedPackagePath = (Resolve-Path -LiteralPath $PackagePath).Path
if (-not (Test-Path -LiteralPath $resolvedPackagePath -PathType Leaf)) {
    throw "Project Tempest private package was not found: '$PackagePath'."
}

$resolvedPackageItem = Get-Item -LiteralPath $resolvedPackagePath -Force
if (($resolvedPackageItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Project Tempest private package may not be a reparse point: '$resolvedPackagePath'."
}

$installFullPath = [IO.Path]::GetFullPath($InstallDirectory)
$installParentPath = Split-Path -Parent $installFullPath
if (-not (Test-Path -LiteralPath $installParentPath -PathType Container)) {
    throw "Install parent directory does not exist: '$installParentPath'."
}
$resolvedInstallParent = (Resolve-Path -LiteralPath $installParentPath).Path
$installParentItem = Get-Item -LiteralPath $resolvedInstallParent -Force
if (($installParentItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Install parent directory may not be a reparse point: '$resolvedInstallParent'."
}
if (Test-Path -LiteralPath $installFullPath) {
    throw "Install rehearsal requires a new destination directory: '$installFullPath'."
}

$installParentPrefix = $resolvedInstallParent.TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar
) + [IO.Path]::DirectorySeparatorChar
if (-not $installFullPath.StartsWith($installParentPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Install directory escaped its resolved parent: '$installFullPath'."
}

$receiptFullPath = [IO.Path]::GetFullPath($ReceiptPath)
$receiptParentPath = Split-Path -Parent $receiptFullPath
if (-not (Test-Path -LiteralPath $receiptParentPath -PathType Container)) {
    throw "Receipt parent directory does not exist: '$receiptParentPath'."
}
$resolvedReceiptParent = (Resolve-Path -LiteralPath $receiptParentPath).Path
$receiptParentItem = Get-Item -LiteralPath $resolvedReceiptParent -Force
if (($receiptParentItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Receipt parent directory may not be a reparse point: '$resolvedReceiptParent'."
}
if (Test-Path -LiteralPath $receiptFullPath) {
    throw "Install receipt already exists: '$receiptFullPath'."
}
$receiptParentPrefix = $resolvedReceiptParent.TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar
) + [IO.Path]::DirectorySeparatorChar
if (-not $receiptFullPath.StartsWith($receiptParentPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Install receipt escaped its resolved parent: '$receiptFullPath'."
}
$installPrefix = $installFullPath.TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar
) + [IO.Path]::DirectorySeparatorChar
if ($receiptFullPath.StartsWith($installPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Install receipt must remain outside the staged package directory."
}

$ExpectedBuildSourceRevision = $ExpectedBuildSourceRevision.ToLowerInvariant()
$ExpectedReviewedSourceRevision = $ExpectedReviewedSourceRevision.ToLowerInvariant()
$ExpectedExecutableSha256 = $ExpectedExecutableSha256.ToLowerInvariant()
$ExpectedMilesStubSha256 = $ExpectedMilesStubSha256.ToLowerInvariant()

$strictUtf8 = [Text.UTF8Encoding]::new($false, $true)
$contract = [IO.File]::ReadAllText($contractPath, $strictUtf8) | ConvertFrom-Json
if ($contract.schema_version -ne 3 -or
    [string]$contract.archive_name -ne "ProjectTempestDemo-private.zip" -or
    [string]$contract.package_directory -ne "ProjectTempestDemo-private") {
    throw "Unsupported Project Tempest private-package contract."
}
$provenance = [IO.File]::ReadAllText($provenancePath, $strictUtf8) | ConvertFrom-Json
if ($provenance.schema_version -ne 1 -or
    [string]$provenance.project -ne "Project Tempest") {
    throw "Unsupported Project Tempest asset provenance record."
}

function Get-CanonicalTextFileSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $encoding = [Text.UTF8Encoding]::new($false, $true)
    $canonicalText = $encoding.GetString([IO.File]::ReadAllBytes($Path)) `
        -replace "`r`n", "`n" -replace "`r", "`n"
    return Get-BytesSha256 -Bytes $encoding.GetBytes($canonicalText)
}

$expectedGovernedEntries = @($contract.runtime_files) + @($contract.repository_files)
$expectedGovernedNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$contractKinds = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $expectedGovernedEntries) {
    $name = [string]$entry.name
    if ($name -notmatch '^[^/\\:]+$' -or -not $expectedGovernedNames.Add($name)) {
        throw "Package contract contains an unsafe or duplicate governed name: '$name'."
    }
    $contractKinds.Add($name, [string]$entry.kind)
}

$expectedArchiveNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($name in $expectedGovernedNames) {
    [void]$expectedArchiveNames.Add($name)
}
foreach ($metadataName in @("package-manifest.json", "SHA256SUMS.txt")) {
    if (-not $expectedArchiveNames.Add($metadataName)) {
        throw "Package metadata name collides with the governed contract: '$metadataName'."
    }
}
[string[]]$orderedArchiveNames = @($expectedArchiveNames)
[Array]::Sort($orderedArchiveNames, [StringComparer]::Ordinal)

function Get-BytesSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Bytes
    )

    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function Get-CanonicalTextBytesSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Bytes
    )

    $encoding = [Text.UTF8Encoding]::new($false, $true)
    $canonicalText = $encoding.GetString($Bytes) -replace "`r`n", "`n" -replace "`r", "`n"
    return Get-BytesSha256 -Bytes $encoding.GetBytes($canonicalText)
}

function ConvertTo-DeterministicJsonStringLiteral {
    param(
        [AllowNull()]
        [string]$Value
    )

    if ($null -eq $Value) {
        return "null"
    }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    foreach ($character in $Value.ToCharArray()) {
        $code = [int][char]$character
        switch ($code) {
            8 { [void]$builder.Append('\b'); continue }
            9 { [void]$builder.Append('\t'); continue }
            10 { [void]$builder.Append('\n'); continue }
            12 { [void]$builder.Append('\f'); continue }
            13 { [void]$builder.Append('\r'); continue }
            34 { [void]$builder.Append('\"'); continue }
            92 { [void]$builder.Append('\\'); continue }
        }
        if ($code -lt 0x20 -or $code -gt 0x7E) {
            [void]$builder.Append(('\u{0:x4}' -f $code))
        }
        else {
            [void]$builder.Append($character)
        }
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function ConvertTo-DeterministicJson {
    param(
        [AllowNull()]
        [object]$Value,
        [int]$Depth = 0
    )

    if ($Depth -gt 12) {
        throw "Install receipt exceeded the deterministic JSON depth limit."
    }
    if ($null -eq $Value) {
        return "null"
    }
    if ($Value -is [string] -or $Value -is [char]) {
        return ConvertTo-DeterministicJsonStringLiteral -Value ([string]$Value)
    }
    if ($Value -is [bool]) {
        return $(if ($Value) { "true" } else { "false" })
    }
    if ($Value -is [byte] -or $Value -is [sbyte] -or
        $Value -is [int16] -or $Value -is [uint16] -or
        $Value -is [int32] -or $Value -is [uint32] -or
        $Value -is [int64] -or $Value -is [uint64] -or
        $Value -is [decimal] -or $Value -is [single] -or $Value -is [double]) {
        return ([IFormattable]$Value).ToString($null, [Globalization.CultureInfo]::InvariantCulture)
    }

    $indent = "  " * $Depth
    $childIndent = "  " * ($Depth + 1)
    $properties = [Collections.Generic.List[object]]::new()
    if ($Value -is [Collections.IDictionary]) {
        foreach ($key in $Value.Keys) {
            $properties.Add([ordered]@{ name = [string]$key; value = $Value[$key] })
        }
    }
    elseif ($Value -is [Management.Automation.PSCustomObject]) {
        foreach ($property in $Value.PSObject.Properties) {
            $properties.Add([ordered]@{ name = $property.Name; value = $property.Value })
        }
    }
    if ($properties.Count -gt 0) {
        $records = [Collections.Generic.List[string]]::new()
        foreach ($property in $properties) {
            $name = ConvertTo-DeterministicJsonStringLiteral -Value ([string]$property.name)
            $jsonValue = ConvertTo-DeterministicJson -Value $property.value -Depth ($Depth + 1)
            $records.Add("$childIndent$name`: $jsonValue")
        }
        return "{`n$($records -join ",`n")`n$indent}"
    }
    if ($Value -is [Collections.IEnumerable]) {
        $items = @($Value)
        if ($items.Count -eq 0) {
            return "[]"
        }
        $records = [Collections.Generic.List[string]]::new()
        foreach ($item in $items) {
            $records.Add("$childIndent$(ConvertTo-DeterministicJson -Value $item -Depth ($Depth + 1))")
        }
        return "[`n$($records -join ",`n")`n$indent]"
    }
    throw "Install receipt contains unsupported JSON value type '$($Value.GetType().FullName)'."
}

$contractHash = Get-CanonicalTextFileSha256 -Path $contractPath
$provenanceHash = Get-CanonicalTextFileSha256 -Path $provenancePath
$projectTempestRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $provenancePath)).Path
$projectTempestPrefix = $projectTempestRoot.TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar
) + [IO.Path]::DirectorySeparatorChar
$reviewedProvenanceByLeaf = [Collections.Generic.Dictionary[string, object]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
foreach ($asset in @($provenance.assets)) {
    $relativePath = [string]$asset.path
    $assetId = [string]$asset.asset_id
    $expectedHash = [string]$asset.sha256
    if ([string]::IsNullOrWhiteSpace($relativePath) -or
        $assetId -notmatch '^PT-[A-Z0-9-]+$' -or
        $expectedHash -notmatch '^[0-9a-f]{64}$' -or
        [IO.Path]::IsPathRooted($relativePath)) {
        throw "Asset provenance contains an incomplete or unsafe reviewed asset record."
    }
    $reviewedAssetPath = [IO.Path]::GetFullPath((Join-Path $projectTempestRoot $relativePath))
    if (-not $reviewedAssetPath.StartsWith($projectTempestPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $reviewedAssetPath -PathType Leaf)) {
        throw "Reviewed provenance asset '$assetId' does not resolve to a repository file."
    }
    $reviewedAssetItem = Get-Item -LiteralPath $reviewedAssetPath -Force
    if (($reviewedAssetItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        (Get-FileHash -LiteralPath $reviewedAssetPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
        throw "Reviewed provenance asset '$assetId' does not match the reviewed checkout hash."
    }
    $leaf = [IO.Path]::GetFileName($relativePath)
    if ([string]::IsNullOrWhiteSpace($leaf) -or $reviewedProvenanceByLeaf.ContainsKey($leaf)) {
        throw "Asset provenance contains a duplicate runtime leaf name '$leaf'."
    }
    $reviewedProvenanceByLeaf.Add($leaf, $asset)
}

function Get-ZipEntryBytes {
    param(
        [Parameter(Mandatory = $true)]
        [IO.Compression.ZipArchiveEntry]$Entry
    )

    if ($Entry.Length -lt 0 -or $Entry.Length -gt 536870912) {
        throw "Package entry '$($Entry.FullName)' exceeds the bounded extraction size."
    }
    $inputStream = $Entry.Open()
    $memory = [IO.MemoryStream]::new()
    try {
        $inputStream.CopyTo($memory)
        $bytes = $memory.ToArray()
        if ($bytes.LongLength -ne $Entry.Length) {
            throw "Package entry '$($Entry.FullName)' length changed while being read."
        }
        return ,$bytes
    }
    finally {
        $memory.Dispose()
        $inputStream.Dispose()
    }
}

function Test-ZipEntryIsLinkOrReparsePoint {
    param(
        [Parameter(Mandatory = $true)]
        [IO.Compression.ZipArchiveEntry]$Entry
    )

    $external = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$Entry.ExternalAttributes), 0)
    $unixType = ($external -shr 16) -band 0xF000
    $dosAttributes = $external -band 0xFFFF
    return ($unixType -eq 0xA000 -or
        ($dosAttributes -band [uint32][IO.FileAttributes]::ReparsePoint) -ne 0)
}

function Assert-Pe32X86GuiImage {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Bytes,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $stream = [IO.MemoryStream]::new($Bytes, $false)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 0x100 -or $reader.ReadUInt16() -ne 0x5A4D) {
            throw "Installed executable '$Name' is not a valid PE32 x86 GUI image."
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0x40 -or $peOffset -gt ($stream.Length - 94)) {
            throw "Installed executable '$Name' is not a valid PE32 x86 GUI image."
        }
        $stream.Position = $peOffset
        $signature = $reader.ReadUInt32()
        $machine = $reader.ReadUInt16()
        $numberOfSections = $reader.ReadUInt16()
        $stream.Position = $peOffset + 20
        $optionalHeaderSize = $reader.ReadUInt16()
        $optionalHeaderOffset = $peOffset + 24
        if ($signature -ne 0x00004550 -or $machine -ne 0x014C -or $numberOfSections -lt 1 -or
            $optionalHeaderSize -lt 70 -or ($optionalHeaderOffset + $optionalHeaderSize) -gt $stream.Length) {
            throw "Installed executable '$Name' is not a valid PE32 x86 GUI image."
        }
        $stream.Position = $optionalHeaderOffset
        $optionalMagic = $reader.ReadUInt16()
        $stream.Position = $optionalHeaderOffset + 68
        $subsystem = $reader.ReadUInt16()
        if ($optionalMagic -ne 0x010B -or $subsystem -ne 2) {
            throw "Installed executable '$Name' is not a valid PE32 x86 GUI image."
        }
    }
    finally {
        $reader.Dispose()
    }
}

$archiveHash = (Get-FileHash -LiteralPath $resolvedPackagePath -Algorithm SHA256).Hash.ToLowerInvariant()
$archive = [IO.Compression.ZipFile]::OpenRead($resolvedPackagePath)
$entryBytes = [Collections.Generic.Dictionary[string, byte[]]]::new([StringComparer]::OrdinalIgnoreCase)
try {
    $prefix = "$($contract.package_directory)/"
    $archiveEntries = [Collections.Generic.Dictionary[string, IO.Compression.ZipArchiveEntry]]::new(
        [StringComparer]::OrdinalIgnoreCase
    )
    $totalUncompressedLength = 0L
    foreach ($entry in $archive.Entries) {
        if (Test-ZipEntryIsLinkOrReparsePoint -Entry $entry) {
            throw "Project Tempest private package rejects link/reparse entry '$($entry.FullName)'."
        }
        if ($entry.FullName -notmatch "^$([regex]::Escape($prefix))[^/\\:]+$" -or
            [string]::IsNullOrWhiteSpace($entry.Name) -or
            $entry.Name -in @(".", "..")) {
            throw "Project Tempest private package contains unsafe or nested entry '$($entry.FullName)'."
        }
        if ($archiveEntries.ContainsKey($entry.Name)) {
            throw "Project Tempest private package contains duplicate or case-colliding entry '$($entry.Name)'."
        }
        $archiveEntries.Add($entry.Name, $entry)
        $totalUncompressedLength += $entry.Length
        if ($totalUncompressedLength -gt 1073741824) {
            throw "Project Tempest private package exceeds the bounded extraction size."
        }
    }

    if ($archiveEntries.Count -ne $expectedArchiveNames.Count) {
        throw "Project Tempest private package entry count is $($archiveEntries.Count); expected $($expectedArchiveNames.Count)."
    }
    foreach ($name in $expectedArchiveNames) {
        if (-not $archiveEntries.ContainsKey($name)) {
            throw "Project Tempest private package is missing governed entry '$name'."
        }
    }
    foreach ($name in $archiveEntries.Keys) {
        if (-not $expectedArchiveNames.Contains($name)) {
            throw "Project Tempest private package contains unexpected entry '$name'."
        }
        foreach ($pattern in $contract.forbidden_patterns) {
            if ($name -like [string]$pattern) {
                throw "Project Tempest private package contains forbidden entry '$name'."
            }
        }
        $entryBytes.Add($name, (Get-ZipEntryBytes -Entry $archiveEntries[$name]))
    }
}
finally {
    $archive.Dispose()
}

$utf8 = [Text.UTF8Encoding]::new($false)
$manifest = $strictUtf8.GetString($entryBytes["package-manifest.json"]) | ConvertFrom-Json
$packagedContractHash = Get-BytesSha256 -Bytes $entryBytes["package-contract.json"]
$packagedProvenanceHash = Get-BytesSha256 -Bytes $entryBytes["asset-provenance.json"]
if ((Get-CanonicalTextBytesSha256 -Bytes $entryBytes["package-contract.json"]) -ne $contractHash -or
    (Get-CanonicalTextBytesSha256 -Bytes $entryBytes["asset-provenance.json"]) -ne $provenanceHash) {
    throw "Packaged contract or provenance does not match the canonical reviewed source input."
}
$expectedRuntimeInputPolicy = if ($ExpectedDistribution -eq "test_fixture") {
    "restricted_test_fixture"
}
else {
    "governed_integrated_release_outputs_only"
}
if ($manifest.schema_version -ne 2 -or
    [string]$manifest.package -ne [string]$contract.package_directory -or
    [string]$manifest.distribution -ne $ExpectedDistribution -or
    [string]$manifest.source_repository -ne "https://github.com/koltregaskes/project-tempest" -or
    [string]$manifest.source_revision -ne $ExpectedBuildSourceRevision -or
    [string]$manifest.reviewed_source_revision -ne $ExpectedReviewedSourceRevision -or
    [string]$manifest.source_tree -ne $ExpectedSourceTree -or
    [string]$manifest.package_contract_sha256 -ne $packagedContractHash -or
    [string]$manifest.asset_provenance_sha256 -ne $packagedProvenanceHash -or
    [string]$manifest.executable_verification.runtime_input_policy -ne $expectedRuntimeInputPolicy -or
    [string]$manifest.renderer_execution -ne "not_performed" -or
    $manifest.manual_playthrough_claimed -ne $false) {
    throw "Project Tempest private package manifest is not bound to the expected reviewed source and no-GUI state."
}
if ([long]$manifest.source_date_epoch -lt 315532800) {
    throw "Project Tempest private package manifest has an invalid source timestamp."
}

$manifestFiles = @($manifest.files)
if ($manifestFiles.Count -ne $expectedGovernedNames.Count) {
    throw "Project Tempest package manifest file count is $($manifestFiles.Count); expected $($expectedGovernedNames.Count)."
}
$manifestNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($file in $manifestFiles) {
    $name = [string]$file.name
    if (-not $expectedGovernedNames.Contains($name) -or -not $manifestNames.Add($name)) {
        throw "Project Tempest package manifest contains unexpected or duplicate file '$name'."
    }
    if ([string]$file.kind -ne $contractKinds[$name]) {
        throw "Project Tempest package manifest kind does not match the contract for '$name'."
    }
    $bytes = $entryBytes[$name]
    $actualHash = Get-BytesSha256 -Bytes $bytes
    if ([string]$file.sha256 -ne $actualHash -or [long]$file.length -ne $bytes.LongLength) {
        throw "Project Tempest package manifest verification failed for '$name'."
    }
    if ($contractKinds[$name] -eq "asset") {
        if (-not $reviewedProvenanceByLeaf.ContainsKey($name)) {
            throw "Project Tempest package asset '$name' has no reviewed provenance record."
        }
        $reviewedAsset = $reviewedProvenanceByLeaf[$name]
        if ([string]$reviewedAsset.distribution -ne "internal_development_only" -or
            [string]$reviewedAsset.sha256 -ne $actualHash -or
            [string]$file.provenance_asset_id -ne [string]$reviewedAsset.asset_id) {
            throw "Project Tempest package asset '$name' does not match reviewed asset provenance."
        }
    }
}

$sumText = $strictUtf8.GetString($entryBytes["SHA256SUMS.txt"])
if (-not $sumText.EndsWith("`n") -or $sumText.Contains("`r")) {
    throw "Project Tempest SHA256SUMS.txt must use canonical LF-terminated records."
}
$sumNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$sumLines = @($sumText.TrimEnd("`n") -split "`n")
if ($sumLines.Count -ne ($expectedArchiveNames.Count - 1)) {
    throw "Project Tempest SHA256SUMS.txt record count is incorrect."
}
foreach ($line in $sumLines) {
    if ($line -notmatch '^(?<hash>[0-9a-f]{64})  (?<name>[^/\\:]+)$') {
        throw "Project Tempest SHA256SUMS.txt contains a malformed record."
    }
    $name = [string]$Matches.name
    if ($name -eq "SHA256SUMS.txt" -or -not $expectedArchiveNames.Contains($name) -or -not $sumNames.Add($name)) {
        throw "Project Tempest SHA256SUMS.txt contains unexpected or duplicate name '$name'."
    }
    if ([string]$Matches.hash -ne (Get-BytesSha256 -Bytes $entryBytes[$name])) {
        throw "Project Tempest SHA256SUMS.txt hash verification failed for '$name'."
    }
}

$executableContract = @($contract.runtime_files | Where-Object { $_.name -eq "ProjectTempestDemo.exe" })
$milesContract = @($contract.runtime_files | Where-Object { $_.name -eq "mss32.dll" })
if ($executableContract.Count -ne 1 -or $milesContract.Count -ne 1) {
    throw "Project Tempest package contract must govern one executable and one Miles dependency."
}
$executableHash = Get-BytesSha256 -Bytes $entryBytes["ProjectTempestDemo.exe"]
$milesHash = Get-BytesSha256 -Bytes $entryBytes["mss32.dll"]
Assert-Pe32X86GuiImage -Bytes $entryBytes["ProjectTempestDemo.exe"] -Name "ProjectTempestDemo.exe"
if ($executableHash -ne $ExpectedExecutableSha256 -or $milesHash -ne $ExpectedMilesStubSha256) {
    throw "Project Tempest package binaries do not match the externally proven two-build hashes."
}
if ([string]$manifest.executable_verification.name -ne "ProjectTempestDemo.exe" -or
    [string]$manifest.executable_verification.sha256 -ne $executableHash -or
    [string]$manifest.executable_verification.policy -ne [string]$executableContract[0].hash_verification -or
    [string]$manifest.executable_verification.source_binding -ne [string]$executableContract[0].source_binding -or
    [string]$manifest.executable_verification.source_revision -ne $ExpectedBuildSourceRevision -or
    [string]$manifest.executable_verification.reviewed_source_revision -ne $ExpectedReviewedSourceRevision -or
    [string]$manifest.runtime_dependency_verification.name -ne "mss32.dll" -or
    [string]$manifest.runtime_dependency_verification.sha256 -ne $milesHash -or
    [string]$manifest.runtime_dependency_verification.provenance_id -ne [string]$milesContract[0].provenance_id -or
    [string]$manifest.runtime_dependency_verification.policy -ne [string]$milesContract[0].hash_verification) {
    throw "Project Tempest binary proof hashes do not match the independently verified package entries."
}

$acceptanceContract = @($contract.runtime_files | Where-Object { $_.name -eq "headless-acceptance.json" })
$acceptance = $strictUtf8.GetString($entryBytes["headless-acceptance.json"]) | ConvertFrom-Json
$scenarios = @($acceptance.scenarios)
if ($acceptanceContract.Count -ne 1 -or
    $acceptance.schema_version -ne 1 -or
    [string]$acceptance.mode -ne "headless_deterministic_acceptance" -or
    $acceptance.manual_playthrough_claimed -ne $false -or
    $acceptance.fresh_launches -ne 3 -or
    $scenarios.Count -ne 3 -or
    [string]$scenarios[0].name -ne "freegrid_victory_a" -or
    [string]$scenarios[0].outcome -ne "victory" -or
    $scenarios[0].ticks -ne $acceptanceContract[0].victory_ticks -or
    [string]$scenarios[0].final_checksum -ne [string]$acceptanceContract[0].victory_final_checksum -or
    [string]$scenarios[0].trace_checksum -ne [string]$acceptanceContract[0].victory_trace_checksum -or
    [string]$scenarios[1].name -ne "chorus_defeat" -or
    [string]$scenarios[1].outcome -ne "defeat" -or
    $scenarios[1].ticks -ne $acceptanceContract[0].defeat_ticks -or
    [string]$scenarios[1].final_checksum -ne [string]$acceptanceContract[0].defeat_final_checksum -or
    [string]$scenarios[1].trace_checksum -ne [string]$acceptanceContract[0].defeat_trace_checksum -or
    [string]$scenarios[2].name -ne "freegrid_victory_b" -or
    [string]$scenarios[2].outcome -ne "victory" -or
    $scenarios[2].ticks -ne $acceptanceContract[0].victory_ticks -or
    [string]$scenarios[2].final_checksum -ne [string]$acceptanceContract[0].victory_final_checksum -or
    [string]$scenarios[2].trace_checksum -ne [string]$acceptanceContract[0].victory_trace_checksum -or
    @($scenarios | Where-Object { $_.result_flow -ne $true -or $_.restart_flow -ne $true }).Count -ne 0) {
    throw "Project Tempest package does not contain the governed deterministic acceptance evidence."
}

$installCreated = $false
try {
    New-Item -ItemType Directory -Path $installFullPath -ErrorAction Stop | Out-Null
    $installCreated = $true
    foreach ($name in $orderedArchiveNames) {
        [IO.File]::WriteAllBytes((Join-Path $installFullPath $name), $entryBytes[$name])
    }

    $installedItems = @(Get-ChildItem -LiteralPath $installFullPath -Force)
    if ($installedItems.Count -ne $expectedArchiveNames.Count -or
        @($installedItems | Where-Object { $_.PSIsContainer }).Count -ne 0 -or
        @($installedItems | Where-Object {
            ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
        }).Count -ne 0) {
        throw "Installed Project Tempest tree contains an unexpected directory or reparse point."
    }
    foreach ($item in $installedItems) {
        if (-not $expectedArchiveNames.Contains($item.Name) -or
            (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -ne
                (Get-BytesSha256 -Bytes $entryBytes[$item.Name])) {
            throw "Installed Project Tempest tree verification failed for '$($item.Name)'."
        }
    }

    $receipt = [ordered]@{
        schema_version = 1
        package = [string]$contract.package_directory
        verification = "verified_without_execution"
        archive_sha256 = $archiveHash
        archive_length = $resolvedPackageItem.Length
        source_revision = $ExpectedBuildSourceRevision
        reviewed_source_revision = $ExpectedReviewedSourceRevision
        source_tree = $ExpectedSourceTree
        distribution = $ExpectedDistribution
        source_date_epoch = [long]$manifest.source_date_epoch
        package_contract_sha256 = $packagedContractHash
        asset_provenance_sha256 = $packagedProvenanceHash
        reviewed_contract_canonical_sha256 = $contractHash
        reviewed_provenance_canonical_sha256 = $provenanceHash
        binary_hash_source = "governing_two_build_job_outputs"
        asset_hash_source = "reviewed_checkout_and_canonical_provenance"
        verified_asset_count = @($contract.runtime_files | Where-Object { $_.kind -eq "asset" }).Count
        executable_sha256 = $executableHash
        miles_sha256 = $milesHash
        installed_file_count = $expectedArchiveNames.Count
        renderer_execution = "not_performed"
        manual_playthrough_claimed = $false
        files = @(
            $orderedArchiveNames |
                ForEach-Object {
                    [ordered]@{
                        name = $_
                        length = $entryBytes[$_].LongLength
                        sha256 = Get-BytesSha256 -Bytes $entryBytes[$_]
                    }
                }
        )
    }
    $receiptJson = (ConvertTo-DeterministicJson -Value $receipt) + "`n"
    [IO.File]::WriteAllText($receiptFullPath, $receiptJson, $utf8)
}
catch {
    if ($installCreated -and (Test-Path -LiteralPath $installFullPath -PathType Container)) {
        $resolvedFailedInstall = (Resolve-Path -LiteralPath $installFullPath).Path
        if (-not $resolvedFailedInstall.StartsWith($installParentPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean failed install outside '$resolvedInstallParent': '$resolvedFailedInstall'."
        }
        Remove-Item -LiteralPath $resolvedFailedInstall -Recurse -Force
    }
    if (Test-Path -LiteralPath $receiptFullPath) {
        Remove-Item -LiteralPath $receiptFullPath -Force
    }
    throw
}

$receiptHash = (Get-FileHash -LiteralPath $receiptFullPath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "PASS: Project Tempest private package verified and staged without execution"
Write-Host "Archive SHA256: $archiveHash"
Write-Host "Install receipt: $receiptFullPath"
Write-Host "Receipt SHA256: $receiptHash"
