# Project Tempest Original Content

This tree holds the legally distinct source material and, later, runtime content for the standalone Project Tempest
demo. It must never contain copied or derived Electronic Arts retail assets.

## Layout

- `SourceAssets/Concepts`: original visual references used to author production assets.
- `SourceAssets/Models`: future editable Blender masters, collision sources, LOD sources, and export settings.
- `Content`: original loose runtime models, textures, audio, and later map/localisation data.
- `ReviewEvidence`: durable native-engine screenshots linked from the provenance manifest.
- `asset-provenance.json`: machine-readable origin, prompt, edit, rights-review, hash, and distribution status.

Source filenames may be descriptive. Every W3D export filename and internal mesh identifier must be 16 characters or
fewer; the automated export mapping owns that constraint. Generated references are not production meshes and must not
be traced, converted, or shipped without an authored cleanup pass and an explicit public-distribution rights review.

The first reference is
[`courier-concept-v1.png`](SourceAssets/Concepts/Freegrid/Courier/courier-concept-v1.png). It is approved for internal
blockout and silhouette work only: the views are sufficiently consistent to guide a model, but they are not engineering
drawings and must not be treated as exact dimensions or topology.

After the W3D smoke test has installed the pinned OpenSAGE plugin, regenerate the first editable Courier blockout,
hero/top-down review renders, and runtime W3D with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\create-courier-blockout.ps1
```

The generator authors seven deterministic 128x128 TGA material maps, then splits each visual material into a single-pass
render submesh, producing six submeshes per LOD plus a
`BOUNDINGBOX` collision primitive with physical, projectile, visibility, and vehicle flags. `HouseColor0` and
`HouseColor1` deliberately use Generals' native mesh-name recolouring convention. It exports in W3D `HM` mode and
immediately re-imports the file, failing unless all twelve render meshes, both house-colour meshes, and exactly one
collision box survive the round trip. This one-material-per-submesh rule is required because the engine supports no more
than four render passes per mesh. The W3D output is deterministic; Workbench preview PNG and Blender container hashes are
recorded for the committed artifacts but are not deterministic build keys.

The same headless run also creates `courierd.w3d`, a separate `REALLYDAMAGED`-ready HLOD with a dark burn plate,
deformed sensor housing, powered-off cyan elements, two LODs, and the same collision contract. Both pristine and damaged
models are re-imported in an empty Blender scene; the test fails unless all expected render meshes and texture references
survive. The current executable package includes both W3Ds and all seven textures beside the executable.

Generate the dedicated Chorus Drone, Freegrid Relay, Freegrid Arc Sentry, Chorus Signal Pylon, Freegrid Relay Core,
Freegrid Fabricator Bay, and Chorus Spire kit through the same pinned, headless pipeline:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\create-substation-kit.ps1
```

The Drone uses a three-arm radial machine silhouette, magenta circuitry, cyan emitters, and three strictly decreasing
LOD states (640, 345, and 194 authored vertices). The Relay uses a radial grid-node silhouette, three native
`HouseColor` meshes, and 636, 350, and 191 authored vertices. The Sentry is a directional dual-rail defence with native
house colour and 880, 470, and 250 authored vertices. The Pylon is a tall three-leg Chorus signal structure with magenta
rings, cyan nodes, and 820, 441, and 238 authored vertices. Each W3D round trip must recover nine single-material render
meshes, its exact texture set, one collision box, and the physical/projectile/visibility/vehicle collision flags. The
Relay Core is a low asymmetric command anchor, the Fabricator Bay is an open workshop gantry, and the Chorus Spire is a
tall five-fold radial objective silhouette. Their authored LOD vertex counts are 888/485/263, 788/427/228, and
1280/1040/907 respectively. Two clean background regenerations must produce byte-identical `drone.w3d`, `relay.w3d`,
`sentry.w3d`, `pylon.w3d`, `relaycore.w3d`, `fabricbay.w3d`, `spire.w3d`, and `ptmagnta.tga` outputs. Blender containers
and Workbench preview PNGs are provenance-pinned review artifacts, not deterministic build keys.

The release asset gate runs the complete Courier-to-kit dependency graph twice in separate Blender processes and output
roots, then requires all seventeen runtime W3D/TGA hashes to match each other and the committed files:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-project-tempest-assets.ps1 `
  -VerifyReproducibility
```

Generate the original score and interaction cues without a hosted service, third-party sample, or paid credit:

```powershell
python .\scripts\create-tempest-audio.py
```

The deterministic standard-library synthesiser produces three synchronised industrial-electronic Substation 9 stems
(base, pressure, and crisis) plus confirmation, selection, command, Arc Pulse, and alert cues as 48 kHz stereo PCM16
WAVs. The normal asset gate regenerates all eight files in the ignored build tree and requires byte-identical hashes.
The standalone demo loads them through a strict WAV parser and Windows XAudio2, adds pressure/crisis layers as Chorus
territory, force disadvantage, and elapsed match pressure rise, routes music/effects through separate submixes, and
applies persisted master/music/effects settings immediately. Audio processing suspends when the window loses focus and
resumes without discarding voice positions. Missing or invalid audio is nonfatal and leaves visual command feedback active.
Audible balance, loop perception, device switching, and long-session stability remain explicit user-run manual evidence;
agents must not launch the demo to obtain it.

The colour-vision accessibility foundation is derived from Electronic Arts' Apache-2.0
[Tunable Colorblindness Solution](https://github.com/electronicarts/Tunable-Colorblindness-Solution). The isolated
`Code/TempestAccessibility.*` module provides disabled-by-default protanopia, deuteranopia, and tritanopia modes with
0-100% strength, -10% to +10% brightness, and -25% to +40% contrast. Version-four settings persist these controls and
migrate older profiles to safe defaults. Pinned vector tests protect the EA-derived math, and the executable package
includes the upstream licence, NOTICE, source record, and Project Tempest modification notice. This is the portable
math/settings foundation only: the final single-pass world-and-UI renderer connection, reference screenshots, and
frame-time evidence remain open and must not be claimed from headless tests.

The runtime W3D has a captured frame from the repository's native `W3DViewV.exe`. The viewer is blocked under Microsoft
Remote Display. On 15 July 2026, unattended launches from `build/ci-w3dview-fixed/W3DViewV.exe` caused repeated
focus-stealing render-device dialogs and six Application Error event 1000 records at 10:49:40, 10:50:40, 10:52:08,
10:55:30, 10:59:11, and 10:59:45 BST (exception codes `0xc0000005` and `0xc000041d`). This is an unattended-execution
safety incident first and a renderer-compatibility defect second. The following command may prepare dependencies, but
it never launches or retries the viewer:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\prepare-w3dview-compat.ps1 `
  -ViewerDirectory .\build\ci-w3dview-fixed
```

The captured engine result is
[`courier-w3dview-engine.png`](ReviewEvidence/courier-w3dview-engine.png). It proves geometry and HLOD loading, not final
materials or renderer stability; the production texture/material pass remains open.

## Windows execution safety

All unattended validation is compile-, package-, data-, import-, or offscreen-only. Agents, CI jobs, scheduled tasks,
and automated test scripts must never launch `W3DViewV.exe`, `W3DViewZH.exe`, `ProjectTempestDemo.exe`,
`generalsv.exe`, `generalszh.exe`, `WorldBuilderV.exe`, `WorldBuilderZH.exe`, Blender's interactive UI, a render-device
selector, or any other visible game/tool window. Manual commands in this runbook are documentation for a user-operated,
non-RDP desktop only; no unattended wrapper may invoke them or retry them.
`scripts/test-project-tempest-no-gui.ps1` enforces the no-process-launch contract on Project Tempest validation surfaces.
It parses unattended PowerShell entry points and exercises adversarial variable/indirect-path fixtures, so renaming a
GUI executable variable cannot bypass the gate. `scripts/build-windows.ps1` is explicitly covered.

Interactive renderer and gameplay checks are manual-only actions initiated by the user on a suitable non-RDP desktop.
If that environment is unavailable, record visual/gameplay verification as blocked and continue with headless evidence;
do not retry a visible GUI. `prepare-w3dview-compat.ps1` only verifies and copies dependencies and reports
`LaunchPolicy=manual_only`, `AutomaticRetry=false`, and `VerificationMode=files_and_hashes_only`.

No agent, automation, CI job, or scheduled task may perform these manual checks. The safe automated evidence set is:

- compile and link results;
- deterministic simulation and asset-contract tests;
- W3D export/import round trips in Blender `--background --factory-startup` mode;
- package contents, executable metadata, hashes, and CI logs;
- offscreen/headless output from tools that are documented to support it without a visible window.

If a renderer cannot execute through a documented headless/offscreen path, its runtime visual result remains blocked.
Do not substitute a compatibility shim, remote-display retry, process loop, scheduled retry, or hidden launch of a GUI
executable for that missing evidence.

## Standalone prototype

The standalone code now includes a deterministic Substation 9 simulation core in
`Code/TempestSimulation.h` and `Code/TempestSimulation.cpp`. It advances at a fixed 20 ticks per second, accepts
sequence-stable commands, and models role-specific movement and combat for all four Freegrid units and all three Chorus
units, three capturable substations, salvage/ability-charge income, grid-relay Dynamo and Arc Sentry construction,
capacity-aware roster production, Fabricator repair, Signal Pylon pressure, grid-link scan, emergency overcharge,
Arc Pulse, escalating Chorus reinforcement/target AI, pause, restart, victory, and defeat.
`Code/TempestInterface.*` owns the original briefing, play, pause, settings, and
result state machine without
coupling presentation state to the deterministic match checksum. `Tests/TempestSimulationTests.cpp` validates both
surfaces and proves that identical command streams yield an identical checksum on every tick.

Build and run the console-only test target without opening a renderer:

```powershell
cmake --build --preset win32 --target project_tempest_sim_tests
ctest --test-dir .\build\win32 -C Release --output-on-failure
```

The separate headless acceptance target runs three consecutive fresh Substation 9 launches: a scripted Freegrid
victory, an unassisted Chorus-AI defeat path, and a repeated victory. It drives the real simulation and interface state
machines through terminal result and in-process restart, emits stable full-trace/final checksums, and explicitly records
that it is not a manual playthrough:

```powershell
cmake --build --preset win32 --target project_tempest_headless_acceptance
.\build\win32\ProjectTempest\Release\project_tempest_headless_acceptance.exe `
  --output .\build\win32\ProjectTempest\headless-acceptance.json
```

This console executable does not initialise Direct3D, create a window, or play audio. It is permitted in unattended
validation; it is not evidence of rendered gameplay, human usability, audible quality, or measured frame pacing.

The rendered prototype now drives that simulation at the same fixed 20 Hz, converts player input into sequenced
commands, and presents the current match through a neon procedural grid, faction-coloured and shape-distinct
substation/building markers, selection brackets, dedicated models for every Freegrid and Chorus unit role,
pristine/damaged Courier switching, authored Relay Core, Fabricator Bay, Dynamo relay, Arc Sentry, Signal Pylon,
Chorus Spire, and Machine Nest models, and a
scalable in-window HUD. The original interface includes a loading panel,
mission briefing, live resources/objective/selection state, visible command acknowledgement, pause, settings, and a
victory/defeat explanation with restart and settings available without leaving the process. This is a compile- and
headless-test-proven integration checkpoint; safe manual gameplay/legibility verification remains required before the
skirmish is release-quality.

Modern Generals Win32 builds also produce `ProjectTempestDemo.exe`, a retail-asset-free executable that loads the
full dedicated unit and structure roster directly from this tree. Its current Substation
9 integration slice provides a bounded panning RTS camera, bounded Freegrid unit selection, context-sensitive
movement/capture/attack/repair orders, node income, Dynamo/Arc Sentry construction, production of all four Freegrid
roles, role-specific combat, Signal Pylon pressure, grid-link scan, emergency overcharge, Arc Pulse,
pause/settings/restart/result flow, escalating Chorus reinforcements, and victory/defeat. The four Freegrid roles,
three Chorus roles, Relay Core, Fabricator Bay, Dynamo, Arc Sentry, Signal Pylon, Chorus Spire, and Machine Nest now load
dedicated authored runtime models rather than sharing proxy art. It is an executable
integration checkpoint, not the final polished vertical slice.

Controls:

- Left-click selects any Freegrid unit within its screen-space selection bound.
- Right-click moves, captures a nearby substation, attacks a nearby Chorus unit/structure, or orders a selected
  Fabricator rig to repair a damaged friendly target.
- `WASD` pans the camera; configurable edge scroll is enabled by default.
- `B` restores a relay Dynamo at the nearest owned substation when a Fabricator rig is selected.
- `T` builds an Arc Sentry at the nearest eligible owned substation when a Fabricator rig is selected.
- `G`, `U`, `I`, and `P` queue a Fabricator rig, Courier scout, Lancer crew, and Coil carrier respectively.
- `Q` scans for contacts at the pointer; `E` activates emergency overcharge; `F` casts Arc Pulse at the pointer.
- `Space` or `Esc` pauses; `O` opens settings.
- `Enter` starts from the briefing; `R` restarts from pause/result; `Esc` exits briefing/result.

The settings overlay supports camera speed, UI scale, master/music/effects levels, edge-scroll disable, reduced motion,
reduced flashes, colour-independent ownership cues, and collision-safe keyboard-or-mouse remapping for all eighteen
actions, including primary selection and context command. Left, right, middle, Mouse 4, and Mouse 5 are recognised.
The HUD scales from the current client height and aspect ratio rather than assuming 1280×720. Chorus ownership uses an
`X` shape and `[C]` text while Freegrid uses a `+` shape and `[F]` text, so hostile state is not communicated by hue
alone. Reduced motion disables edge-driven camera movement while retaining deliberate pan; losing window focus clears
held keyboard and mouse inputs and enters the pause screen.

Changes are stored in `%LOCALAPPDATA%\ProjectTempest\settings.ini`. The versioned file contains the complete settings
and binding set; invalid, partial, out-of-range, or duplicate data is rejected without partially changing live state.
Version-one and version-two profiles migrate to version three with collision-free defaults for newly introduced roster,
structure, and ability bindings while preserving existing user remaps.
Saving writes a same-directory temporary file before replacing the prior profile. The three volume controls now drive
the XAudio2 master/music/effects routing used by the original score and cues. Player-visible multi-resolution verification,
manual audible-quality proof, and manual runtime proof of persistence/remapping remain open M5 work.

## Private reproducible package

Build the Release target twice in isolated parent-build trees, compare the console-only acceptance reports, the
Project Tempest executables, and the actual fetched GPL Miles DLLs, then pass both proven binary hashes to both
packager invocations. This does not launch the demo:

```powershell
$primaryBuild = ".\build\win32"
$repeatBuild = ".\build\win32-tempest-repro"
$runtimeDirectory = "$primaryBuild\ProjectTempest\Release"
$repeatRuntimeDirectory = "$repeatBuild\ProjectTempest\Release"
$reviewedSourceRevision = (git rev-parse HEAD).Trim()

cmake --preset win32
cmake --build --preset win32 --target project_tempest_demo project_tempest_headless_acceptance
cmake --preset win32 -B $repeatBuild
cmake --build $repeatBuild --config Release --target project_tempest_demo project_tempest_headless_acceptance

& "$runtimeDirectory\project_tempest_headless_acceptance.exe" `
  --output "$runtimeDirectory\headless-acceptance.json"
if ($LASTEXITCODE -ne 0) {
  throw "Project Tempest headless acceptance failed with exit code $LASTEXITCODE."
}
& "$repeatRuntimeDirectory\project_tempest_headless_acceptance.exe" `
  --output "$repeatRuntimeDirectory\headless-acceptance.json"
if ($LASTEXITCODE -ne 0) {
  throw "Repeated Project Tempest headless acceptance failed with exit code $LASTEXITCODE."
}
if ((Get-FileHash "$runtimeDirectory\headless-acceptance.json").Hash -ne
    (Get-FileHash "$repeatRuntimeDirectory\headless-acceptance.json").Hash) {
  throw "The two integrated acceptance reports are not byte-identical."
}

$executableHash = (Get-FileHash "$runtimeDirectory\ProjectTempestDemo.exe" -Algorithm SHA256).Hash.ToLowerInvariant()
$repeatExecutableHash = (Get-FileHash "$repeatRuntimeDirectory\ProjectTempestDemo.exe" -Algorithm SHA256).Hash.ToLowerInvariant()
if ($executableHash -ne $repeatExecutableHash) {
  throw "The two integrated Project Tempest executables are not byte-identical."
}

$milesStub = Get-ChildItem -Path "$primaryBuild\_deps\miles-build" `
  -Recurse -Filter "mss32.dll" -File | Select-Object -First 1
$repeatMilesStub = Get-ChildItem -Path "$repeatBuild\_deps\miles-build" `
  -Recurse -Filter "mss32.dll" -File | Select-Object -First 1
if ($null -eq $milesStub -or $null -eq $repeatMilesStub) {
  throw "A pinned GPL Miles stub was not produced by both integrated Release builds."
}
$milesHash = (Get-FileHash $milesStub.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$repeatMilesHash = (Get-FileHash $repeatMilesStub.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
if ($milesHash -ne $repeatMilesHash) {
  throw "The two integrated Miles DLLs are not byte-identical."
}
Copy-Item -LiteralPath $milesStub.FullName -Destination $runtimeDirectory -Force
Copy-Item -LiteralPath $repeatMilesStub.FullName -Destination $repeatRuntimeDirectory -Force

.\scripts\package-project-tempest-demo.ps1 `
  -RuntimeDirectory $runtimeDirectory `
  -OutputDirectory "$primaryBuild\ProjectTempest\private-package" `
  -ReviewedSourceRevision $reviewedSourceRevision `
  -ExpectedExecutableSha256 $executableHash `
  -ExpectedMilesStubSha256 $milesHash
.\scripts\package-project-tempest-demo.ps1 `
  -RuntimeDirectory $repeatRuntimeDirectory `
  -OutputDirectory "$repeatBuild\ProjectTempest\private-package" `
  -ReviewedSourceRevision $reviewedSourceRevision `
  -ExpectedExecutableSha256 $executableHash `
  -ExpectedMilesStubSha256 $milesHash
```

The packager stages only the executable, GPL Miles stub, governed original runtime assets, provenance, licences,
notices, the deterministic headless-acceptance report, and the private-demo readme listed in `package-contract.json`.
It rejects retail BIG/GIB archives, replays,
EA game executables, WorldBuilder, and W3DView before staging; validates every asset hash against provenance; writes a
machine-readable manifest and `SHA256SUMS.txt`; fixes all ZIP timestamps to the source commit; and produces a stable
`ProjectTempestDemo-private.zip`. Production inputs are restricted to the two governed integrated Release directories.
It also rejects a dirty source tree, a missing caller-supplied executable or Miles proof hash, a malformed/non-x86 GUI
PE, or a binary that does not match its proven hash. The manifest binds the executable hash to both the actual clean
build revision and the exact reviewed head (which may be a direct parent of GitHub's synthetic PR merge revision), plus
the two-build policy. CI retains the merge revision's parent history so that relationship is proven by Git rather than
trusted from an unverified workflow string. The pinned Miles source commit and deterministic build procedure live in provenance; the
compiler-context-dependent DLL hash is recorded in each package manifest rather than hardcoded as a portable source
identity. `test-project-tempest-package.ps1` proves byte-identical repeated packaging, manifest verification, and
missing/wrong/forged executable and dependency-hash rejection with an inert fixture. Windows Release CI compares two
isolated integrated executables and Miles DLLs before packaging, then requires identical acceptance reports and
real-build ZIP hashes. The outer GitHub Actions artifact stages ordinary Generals outputs separately and admits
Project Tempest only as the governed `ProjectTempestDemo-private.zip`; a runtime gate rejects loose Tempest executables,
DLLs, symbols, or governed assets before upload. CI and adversarial temporary-directory fixtures both execute the same
`assert-project-tempest-artifact-boundary.ps1` implementation, including clean governing/non-governing package-count
cases, recursive nested-payload rejection, and reparse-point containment. The main CI path filter routes changes to
that shared assertion back through the Tempest validation and Windows build jobs. Public distribution
remains a separate approval and rights-review gate.

### Clean-machine package rehearsal

The `verify-project-tempest-private-install` Windows CI job is an independent consumer of the governed outer artifact.
It starts from a fresh checkout and runner workspace, downloads `Generals-win32+t+e`, re-applies the shared outer-artifact
boundary, and passes its single private ZIP to `assert-project-tempest-private-package.ps1`. The verifier checks the
ZIP layout and bounded sizes, rejects traversal, nested, duplicate, case-colliding, link/reparse, unexpected, and
forbidden entries, verifies the reviewed contract and provenance, verifies every manifest and `SHA256SUMS.txt` hash,
binds every governed asset to both the canonical provenance record and its reviewed checkout file hash, binds the
executable and Miles DLL to hashes emitted independently by the governing two-build job, validates the
deterministic acceptance report, then stages
the exact governed files into a previously unused directory. It writes a deterministic receipt outside that directory
and rehashes the staged tree. Receipt ordering and JSON encoding are invariant across Windows PowerShell 5.1 and
PowerShell 7. It never invokes `ProjectTempestDemo.exe` or any renderer.

To rehearse the same consumer contract locally against a package already produced from the current checkout, use new
install and receipt paths:

```powershell
$buildSourceRevision = (git rev-parse HEAD).Trim().ToLowerInvariant()
$reviewedSourceRevision = $buildSourceRevision
.\scripts\assert-project-tempest-private-package.ps1 `
  -PackagePath ".\build\win32\ProjectTempest\private-package\ProjectTempestDemo-private.zip" `
  -InstallDirectory "$env:TEMP\project-tempest-clean-install" `
  -ReceiptPath "$env:TEMP\project-tempest-clean-install-receipt.json" `
  -ExpectedBuildSourceRevision $buildSourceRevision `
  -ExpectedReviewedSourceRevision $reviewedSourceRevision `
  -ExpectedExecutableSha256 $executableHash `
  -ExpectedMilesStubSha256 $milesHash
```

The receipt proves package consumption, source binding, containment, and byte integrity without execution. It is not
renderer, audio, performance, soak, alt-tab, resolution, accessibility, usability, or playthrough evidence; those
player-visible M5/M6 gates remain explicit manual-only work on an appropriate local Windows desktop.

### User-initiated runtime evidence

The demo contains a disabled-by-default runtime recorder so one manual acceptance session can produce governed frame-time,
working-set, focus, resolution, restart, outcome, and clean-shutdown evidence. It does not launch the game, take screenshots,
capture audio/video, simulate input, or claim that a playthrough happened.

For a manual session, the user creates an empty evidence directory and sets `PROJECT_TEMPEST_EVIDENCE_DIR` to its absolute
path in the same interactive desktop environment before starting `ProjectTempestDemo.exe` themselves. No agent,
automation, CI job, scheduled task, or unattended wrapper may start the executable or retry it. The user should then
perform the governed M5/M6 checklist, exit normally, and return the evidence directory for analysis.

Each opted-in session uses a fixed-size frame-time histogram plus bounded one-second aggregates to avoid contaminating the
measurement or creating recorder-driven memory growth. It writes a JSONL frame-window/event trace plus a summary JSON
during normal shutdown. The trace caps stored windows at two hours and reports any dropped windows. The summary records average, p50, p95, p99,
and maximum frame time; sampled initial/end/peak working set; tested resolutions; focus-loss count; restart count;
terminal outcomes; exit code; and whether the normal shutdown path completed. Frame windows, lifecycle events,
resolution entries, and outcome summary lists are capped and disclose overflow counts. Histogram samples at or above
1000 ms are explicitly counted as saturated while the exact maximum remains available. `manual_playthrough_claimed` remains false:
the files are machine measurements that must be paired with the user's checklist, screenshots, capture, and observations.

Build with a modern Generals preset, for example:

```powershell
cmake --preset win32
cmake --build --preset win32 --target project_tempest_demo
```

The build places `courier.w3d` beside the executable. A user performing an explicit manual test on a non-RDP desktop
may prepare the demo directory with the same hash-pinned compatibility layer used by W3DView:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\prepare-w3dview-compat.ps1 `
  -ViewerDirectory .\build\win32\ProjectTempest\Release `
  -ExecutableName ProjectTempestDemo.exe `
  -MilesStubPath .\build\win32\_deps\miles-build\Release\mss32.dll
```
