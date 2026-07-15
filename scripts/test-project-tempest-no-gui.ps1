[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$fixedUnattendedSurfaces = @(
    ".github/workflows/ci.yml",
    ".github/workflows/build-toolchain.yml",
    "scripts/build-windows.ps1",
    "scripts/test-project-tempest-assets.ps1",
    "scripts/test-project-tempest-reproducibility.ps1",
    "scripts/test-w3d-pipeline.ps1",
    "scripts/prepare-w3dview-compat.ps1"
)

# Asset generators are unattended surfaces too. Discover them instead of maintaining a
# fragile allow-list so every future create-*.ps1 wrapper inherits this gate immediately.
$assetGeneratorSurfaces = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot "scripts") -Filter "create-*.ps1" -File |
    ForEach-Object { "scripts/$($_.Name)" }
$unattendedSurfaces = @($fixedUnattendedSurfaces + $assetGeneratorSurfaces | Sort-Object -Unique)

$forbiddenProcessNames = @(
    "W3DViewV",
    "W3DViewZH",
    "ProjectTempestDemo",
    "generalsv",
    "generalszh",
    "WorldBuilderV",
    "WorldBuilderZH"
)
$runningForbiddenProcesses = @(Get-Process -Name $forbiddenProcessNames -ErrorAction SilentlyContinue)
if ($runningForbiddenProcesses.Count -gt 0) {
    $details = $runningForbiddenProcesses |
        ForEach-Object { "$($_.ProcessName) (PID $($_.Id))" }
    throw "Prohibited visible GUI process detected during unattended validation: $($details -join ', ')."
}

$forbiddenProcessLaunchPatterns = @(
    "(?i)\bStart-Process\b",
    "(?i)\bInvoke-Item\b",
    "(?i)\[System\.Diagnostics\.Process\]::Start",
    "(?i)\bProcessStartInfo\b",
    "(?i)\bWScript\.Shell\b",
    "(?i)\bShellExecute\b",
    "(?i)\bStart-Job\b",
    "(?i)\bInvoke-Expression\b",
    "(?i)\bRegister-ScheduledTask\b",
    "(?i)\bschtasks(?:\.exe)?\b",
    "(?i)\bcmd(?:\.exe)?\s+\/c\s+start\b",
    '(?im)^\s*&\s+\$(?:viewerPath|executablePath|gamePath|demoPath|worldBuilderPath)\b',
    "(?im)^\s*&\s+.*(?:W3DViewV|W3DViewZH|ProjectTempestDemo|generalsv|generalszh|WorldBuilderV|WorldBuilderZH)(?:\.exe)?\b",
    "(?im)^\s*(?:\.\\|\.\/).*?(?:W3DViewV|W3DViewZH|ProjectTempestDemo|generalsv|generalszh|WorldBuilderV|WorldBuilderZH)(?:\.exe)?\b"
)

function Get-UnattendedScriptPolicyViolation {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Content,
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    $tokens = $null
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseInput(
        $Content,
        [ref]$tokens,
        [ref]$parseErrors
    )
    $violations = [System.Collections.Generic.List[string]]::new()
    foreach ($parseError in $parseErrors) {
        $violations.Add("PowerShell parse error at offset $($parseError.Extent.StartOffset): $($parseError.Message)")
    }

    $commands = $ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.CommandAst]
    }, $true)
    foreach ($command in $commands) {
        $commandName = $command.GetCommandName()
        if ($commandName -and $commandName -match '(?i)^(?:Start-Process|Invoke-Item|Invoke-Expression|Start-Job|Register-ScheduledTask|schtasks(?:\.exe)?)$') {
            $violations.Add("forbidden command '$commandName'")
            continue
        }
        if ($commandName -and $commandName -match '(?i)(?:^|[\\/])(?:W3DViewV|W3DViewZH|ProjectTempestDemo|generalsv|generalszh|WorldBuilderV|WorldBuilderZH)(?:\.exe)?$') {
            $violations.Add("forbidden GUI executable '$commandName'")
            continue
        }

        if ($command.InvocationOperator -ne [System.Management.Automation.Language.TokenKind]::Ampersand -or $commandName) {
            continue
        }

        $firstElement = $command.CommandElements[0].Extent.Text
        $commandText = $command.Extent.Text
        $isSafeBlender = (
            $firstElement -ieq '$BlenderPath' -and
            $commandText -match '(?i)--background\b' -and
            $commandText -match '(?i)--factory-startup\b'
        )
        $isSafeBuildTool = (
            $RelativePath -ieq 'scripts/build-windows.ps1' -and
            $firstElement -in @('$cmake', '$vswhere')
        )
        if (-not $isSafeBlender -and -not $isSafeBuildTool) {
            $violations.Add("unapproved dynamic invocation '$firstElement'")
        }
    }

    return @($violations)
}

foreach ($relativePath in $unattendedSurfaces) {
    $path = Join-Path $repositoryRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing Project Tempest unattended-validation surface: $relativePath"
    }

    $content = Get-Content -LiteralPath $path -Raw
    foreach ($pattern in $forbiddenProcessLaunchPatterns) {
        if ($content -match $pattern) {
            throw "Interactive process-launch primitive '$pattern' is forbidden in unattended surface '$relativePath'."
        }
    }

    if ([IO.Path]::GetExtension($relativePath) -ieq ".ps1") {
        $astViolations = @(Get-UnattendedScriptPolicyViolation -Content $content -RelativePath $relativePath)
        if ($astViolations.Count -gt 0) {
            throw "Unattended PowerShell policy violation in '$relativePath': $($astViolations -join '; ')"
        }
    }
}

# These fixtures prove that renaming or constructing a GUI path cannot bypass the AST gate.
$adversarialFixtures = @(
    '$tool = Join-Path $root "W3DViewV.exe"; & $tool',
    '$tool = Join-Path $root "W3DViewV"; & $tool',
    '$renamed = "ProjectTempestDemo.exe"; & $renamed',
    '.\ProjectTempestDemo',
    '& (Join-Path $root "WorldBuilderV.exe")'
)
foreach ($fixture in $adversarialFixtures) {
    $fixtureViolations = @(Get-UnattendedScriptPolicyViolation -Content $fixture -RelativePath "adversarial-fixture.ps1")
    if ($fixtureViolations.Count -eq 0) {
        throw "The no-GUI AST gate failed to reject adversarial fixture: $fixture"
    }
}

$safeFixtures = @(
    @{ Content = '& $BlenderPath --background --factory-startup --python $pythonScript'; Path = 'scripts/create-fixture.ps1' },
    @{ Content = '& $cmake --preset $Preset'; Path = 'scripts/build-windows.ps1' }
)
foreach ($fixture in $safeFixtures) {
    $fixtureViolations = @(Get-UnattendedScriptPolicyViolation -Content $fixture.Content -RelativePath $fixture.Path)
    if ($fixtureViolations.Count -gt 0) {
        throw "The no-GUI AST gate rejected a documented headless fixture: $($fixtureViolations -join '; ')"
    }
}

foreach ($blenderScript in $unattendedSurfaces) {
    $content = Get-Content -LiteralPath (Join-Path $repositoryRoot $blenderScript) -Raw
    $invocations = [regex]::Matches($content, '(?im)^\s*&\s+\$BlenderPath\b.*$')
    if ($invocations.Count -eq 0) {
        continue
    }
    if (
        $invocations.Count -ne 1 -or
        $invocations[0].Value -notmatch '(?i)--background\b' -or
        $invocations[0].Value -notmatch '(?i)--factory-startup\b'
    ) {
        throw "Blender invocation in '$blenderScript' must be exactly one explicit --background --factory-startup path."
    }
}

$compatibilityScript = Get-Content -LiteralPath (Join-Path $repositoryRoot "scripts/prepare-w3dview-compat.ps1") -Raw
if (
    $compatibilityScript -notmatch 'LaunchPolicy\s*=\s*"manual_only"' -or
    $compatibilityScript -notmatch 'AutomaticRetry\s*=\s*\$false' -or
    $compatibilityScript -notmatch 'VerificationMode\s*=\s*"files_and_hashes_only"'
) {
    throw "The compatibility helper must advertise manual-only launch, no automatic retry, and files/hashes-only verification."
}

$runbook = Get-Content -LiteralPath (Join-Path $repositoryRoot "ProjectTempest/README.md") -Raw
foreach ($requiredPolicy in @(
    "manual-only",
    "do not retry a visible GUI",
    "No agent, automation, CI job, or scheduled task may perform these manual checks"
)) {
    if ($runbook -notmatch [regex]::Escape($requiredPolicy)) {
        throw "Project Tempest runbook is missing required no-GUI policy text: '$requiredPolicy'."
    }
}

Write-Host "Validated Project Tempest's no-visible-GUI unattended execution contract across $($unattendedSurfaces.Count) surfaces."
Write-Host "Confirmed with AST adversarial fixtures that no prohibited GUI process is running and no automatic renderer retry is declared."
