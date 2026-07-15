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

Generate the dedicated Chorus Drone and Freegrid Relay kit through the same pinned, headless pipeline:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\create-substation-kit.ps1
```

The Drone uses a three-arm radial machine silhouette, magenta circuitry, cyan emitters, and three strictly decreasing
LOD states (640, 345, and 194 authored vertices). The Relay uses a radial grid-node silhouette, three native
`HouseColor` meshes, and 636, 350, and 191 authored vertices. Each W3D round trip must recover nine single-material
render meshes, its exact texture set, one collision box, and the physical/projectile/visibility/vehicle collision flags.
Two clean background regenerations produced byte-identical `drone.w3d`, `relay.w3d`, and `ptmagnta.tga` outputs. Blender
containers and Workbench preview PNGs are provenance-pinned review artifacts, not deterministic build keys.

The release asset gate runs the complete Courier-to-kit dependency graph twice in separate Blender processes and output
roots, then requires all twelve runtime W3D/TGA hashes to match each other and the committed files:

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
sequence-stable commands, and models Courier movement, three capturable substations, credit/power income, Relay
construction, Courier production, combat, Arc Pulse, Chorus reinforcement/target AI, pause, restart, victory, and
defeat. `Code/TempestInterface.*` owns the original briefing, play, pause, settings, and result state machine without
coupling presentation state to the deterministic match checksum. `Tests/TempestSimulationTests.cpp` validates both
surfaces and proves that identical command streams yield an identical checksum on every tick.

Build and run the console-only test target without opening a renderer:

```powershell
cmake --build --preset win32 --target project_tempest_sim_tests
ctest --test-dir .\build\win32 -C Release --output-on-failure
```

The rendered prototype now drives that simulation at the same fixed 20 Hz, converts player input into sequenced
commands, and presents the current match through a neon procedural grid, faction-coloured and shape-distinct
substation/building markers, selection brackets, dedicated Courier/Chorus Drone visuals, pristine/damaged Courier
switching, an authored Relay model, and a scalable in-window HUD. The original interface includes a loading panel,
mission briefing, live resources/objective/selection state, visible command acknowledgement, pause, settings, and a
victory/defeat explanation with restart and settings available without leaving the process. This is a compile- and
headless-test-proven integration checkpoint; safe manual gameplay/legibility verification and dedicated Workshop/Core
art remain required before the skirmish is release-quality.

Modern Generals Win32 builds also produce `ProjectTempestDemo.exe`, a retail-asset-free executable that loads the
Courier, damaged Courier, Chorus Drone, and Freegrid Relay directly from this tree. Its current Substation 9 integration slice provides a bounded panning RTS camera, bounded unit
selection, context-sensitive movement/capture/attack orders, node income, Relay construction, Courier production,
combat, Arc Pulse, pause/settings/restart/result flow, Chorus reinforcements, and victory/defeat. It is an executable
integration checkpoint, not the final polished vertical slice.

Controls:

- Left-click selects a Freegrid Courier within its screen-space selection bound.
- Right-click moves, captures a nearby substation, or attacks a nearby Chorus unit/Core.
- `WASD` pans the camera; configurable edge scroll is enabled by default.
- `B` constructs a Relay at the nearest owned substation; `U` queues a Courier.
- `F` casts Arc Pulse at the pointer; `Space` or `Esc` pauses; `O` opens settings.
- `Enter` starts from the briefing; `R` restarts from pause/result; `Esc` exits from briefing/result.

The settings overlay supports camera speed, UI scale, master/music/effects levels, edge-scroll disable, reduced motion,
reduced flashes, colour-independent ownership cues, and collision-safe keyboard-or-mouse remapping for all twelve
actions, including primary selection and context command. Left, right, middle, Mouse 4, and Mouse 5 are recognised.
The HUD scales from the current client height and aspect ratio rather than assuming 1280×720. Chorus ownership uses an
`X` shape and `[C]` text while Freegrid uses a `+` shape and `[F]` text, so hostile state is not communicated by hue
alone. Reduced motion disables edge-driven camera movement while retaining deliberate pan; losing window focus clears
held keyboard and mouse inputs and enters the pause screen.

Changes are stored in `%LOCALAPPDATA%\ProjectTempest\settings.ini`. The versioned file contains the complete settings
and binding set; invalid, partial, out-of-range, or duplicate data is rejected without partially changing live state.
Saving writes a same-directory temporary file before replacing the prior profile. The three volume controls now drive
the XAudio2 master/music/effects routing used by the original score and cues. Player-visible multi-resolution verification,
manual audible-quality proof, and manual runtime proof of persistence/remapping remain open M5 work.

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
