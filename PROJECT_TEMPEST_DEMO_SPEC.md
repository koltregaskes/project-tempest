# Project Tempest: Substation 9

Status: governing source for the match this repository ships. The filename is historical.
Tracking: Linear project `200 Build Room / Project Tempest`, led by KOL-4608 and KOL-4610.

## The game

The playable product this repository ships is one match: Substation 9. This match is the whole game.

A new player starts it from the repository root:

```bash
./scripts/play-substation-9.sh
```

That launch does not use a retail Command & Conquer install and does not open a window. The briefing states the goal
before the first order. The player commands Freegrid in 2089 Kestrel Basin. Salvage pays for construction and
production. Captured substations add salvage and ability charge. There is no scrap field to harvest.

Play the match in this order. The opening command is issued in under 30 seconds.

1. Capture the western substation.
2. Build a Dynamo and an Arc Sentry there.
3. Produce a Coil carrier and a Lancer crew. Courier scouts capture and spot. They cannot break the Spire.
4. For the first twelve minutes Chorus contests the substations and does not assault the base. Chorus then assaults
   the Relay Core. The Arc Sentry holds that assault.
5. Destroy the Chorus Spire.

Win: the Chorus Spire falls. The result is `GRID RESTORED`, and the session says the Chorus Spire fell and the grid
returned to Freegrid control. Loss: the Relay Core falls. The result is `DISTRICT LOST`, and the session says the
Relay Core fell before the Chorus signal could be isolated.

A first win takes about 15 to 20 minutes. `ProjectTempest/Tests/session-victory.commands` is that order and reaches
`GRID RESTORED` just after 17 minutes 25 seconds. `ProjectTempest/Tests/session-defeat.commands` captures the western
substation, fields Courier scouts, and never builds a Dynamo or an Arc Sentry. The Spire holds. The Relay Core falls
just after 12 minutes 5 seconds, when the assault arrives.

Campaign, cinematics, multiplayer, matchmaking, a second playable faction, and the Generals engine-port programme are
later work. They are not an unfinished session wrapped around this match. The rendered Windows window is a manual
presentation of the same match. It is not a different product, and finishing the match does not require it.

## Creative spine

Method: First Principles. The familiar faction-and-unit surface of the original game is not carried forward. The
retained value is the deterministic RTS simulation, readable command model, data-driven content, and mature tooling.

### Player fantasy

In 2089, the storm-battered Kestrel Basin is divided between sealed corporate arcologies and neighbourhood power
meshes assembled from abandoned infrastructure. The player commands **Freegrid**, a mobile crew that turns stolen
current and civic machinery into a fighting network. Its enemy is **Chorus**, an extra-terrestrial signal that
has colonised obsolete industrial control systems and now grows a distributed machine ecology beneath the city.

This supports a cyberpunk foreground—dense infrastructure, improvised technology, contested power, surveillance, and
street-scale resistance—without reducing the opponent to another human army. The alien element appears through
behaviour, silhouette, light, and sound rather than exposition-heavy cutscenes.

### Visual and audio rules

- Wet concrete, oxidised steel, sodium amber, emergency red, and restrained cyan are the shared world palette.
- Freegrid forms are repaired, asymmetric, human-scaled, cable-exposed, and marked with physical stencil language.
- Chorus forms repeat impossible radial motifs, black ceramic surfaces, pale bioluminescence, and coordinated motion.
- Information hierarchy beats spectacle: selections, ownership, range, damage, threats, and resource state must read
  at normal RTS camera distance and remain distinguishable without relying on hue alone.
- Music is a sparse industrial-electronic system that adds layers as pressure rises. Effects prioritise command
  acknowledgement, weapon identity, damage state, and objective state. No generated voice impersonation is allowed.
- All product names, logos, faction marks, terminology, UI, maps, models, textures, effects, audio, and writing are
  original and legally distinct. The codename must be checked before any public use.

## What the match contains

Freegrid is the only player-controlled faction. A second faction is later work.

| Type | In this match | Purpose |
|---|---|---|
| Structures | Relay Core, Fabricator Bay, Dynamo, Arc Sentry | base, production, capacity, defence |
| Units | Fabricator rig, Courier scout, Lancer crew, Coil carrier | build and repair, capture and spotting, anti-armour, siege |
| Abilities | grid-link scan, emergency overcharge, Arc Pulse | information, a short power spike, and an area strike |

Chorus is controlled by the match:

| Type | In this match | Purpose |
|---|---|---|
| Structures | Machine Nest, Signal Pylon, Chorus Spire | production and Spire repair, map pressure, the objective |
| Units | Skitter, Warden, Harrower | harassment, ranged control, the twelve-minute assault |

One resource, **salvage**, pays construction and production. Captured substations add salvage and ability charge.
There is no second currency and no scrap field.

The Machine Nest repairs the Chorus Spire while the nest stands. Courier scouts must close inside the Spire's point
defence and cannot outpace that repair. A Coil carrier and a Lancer crew outrange that point defence. An Arc Sentry
on the western substation covers the Relay Core when the assault begins at twelve minutes. Without that sentry, the
assault destroys the Relay Core.

Later work, not part of this match: campaign, cinematics, multiplayer, matchmaking, spectator mode, monetisation,
account systems, user-generated-content distribution, a second playable faction, hero units, naval or air combat, the
rendered-window presentation pass, and public release. None of those is the session a player is in.

## Technical strategy

### Preserve

- deterministic simulation and replay-friendly command processing;
- the existing object, weapon, locomotor, AI, map, and UI data boundaries where they remain fit for purpose;
- the active community CMake/Visual Studio baseline and upstream-compatible fixes;
- separation between simulation time and presentation/render timing.

The simulation may remain fixed-step. Presentation should interpolate independently so 60 Hz, 120 Hz, and variable
refresh displays do not change game outcomes.

### Replace or isolate

- retail `.big` archive assumptions on the Substation 9 path;
- retail bootstrap screens, names, strings, UI, maps, models, textures, shaders/effects, audio, video, and localisation;
- fixed-resolution UI assumptions and frame-rate-dependent presentation;
- legacy platform APIs only where they block this match.

The least invasive route is preferred: a new standalone content root and loose-data/developer override path before a
new archive format. Retail compatibility remains available behind an explicit build/runtime mode until a deliberate
protocol boundary is approved.

### Verified engine flow and first edit surface

```text
scripts/build-windows.ps1 -> CMakePresets.json -> GeneralsMD targets
    -> GeneralsMD/Code/GameEngine/Source/Common/GameEngine.cpp
    -> local filesystem first, archive filesystem second
    -> Data/INI subsystem definitions -> thing/weapon/locomotor/AI factories
    -> W3D asset manager and model draw -> rendered units
    -> WorldBuilder map -> skirmish load -> simulation/replay command stream
```

The local-first lookup in `Core/GameEngine/Source/Common/System/FileSystem.cpp` is the critical seam: loose original
files can override archive content during development. Archive discovery is owned by
`Core/GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp` and mod archive loading by
`Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp`. INI discovery/parsing is owned by
`Core/GameEngine/Source/Common/INI/INI.cpp`; the Zero Hour bootstrap enumerates the exact data stores in
`GeneralsMD/Code/GameEngine/Source/Common/GameEngine.cpp`.

The first unit render path is owned by
`GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DAssetManager.cpp` and
`Core/GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.cpp`. Map authoring is owned by
`GeneralsMD/Code/Tools/WorldBuilder`; format inspection can use `Core/Tools/W3DView`. The initial implementation surface
should stay within the build scripts/CMake prerequisite checks, a new original loose-content root, the W3D export
automation, data definitions, and the smallest bootstrap selection needed to enter Substation 9. Networking, replay
protocol, renderer replacement, and broad platform abstraction are later work unless evidence shows they block this match.

The first standalone simulation surface is implemented in `ProjectTempest/Code/TempestSimulation.*`. It uses integer
state and a fixed 20 Hz tick, sequences commands deterministically, and owns Substation 9 movement, capture, economy,
construction, production, combat, Arc Pulse, basic Chorus AI, pause/restart, and outcomes. A console-only test target
replays the same command stream twice and compares every per-tick checksum. The standalone renderer now consumes that
state, converts mouse/keyboard input into sequenced commands, and visualises units, control nodes, buildings, selection,
and damage state with original/procedural content. A portable interface controller now owns briefing, play, pause,
settings, result, restart, bounded settings, versioned persistence, and collision-safe keyboard/mouse remapping; it is covered by the same strict
headless test executable as the simulation. The renderer consumes that controller through a scalable in-window HUD,
visible command feedback, a mission/result explanation, adjustable camera/UI/accessibility state, and `+`/`X` plus
`[F]`/`[C]` colour-independent ownership cues. This proves the compile-time rules/presentation seam; safe manual
gameplay and multi-resolution evidence remain later checks of that window. They are not a second game. Dedicated
Chorus Drone, Freegrid Relay, Freegrid Arc Sentry, and Chorus Signal Pylon assets now replace their Courier/procedural
proxies: each has three authored LOD states, single-pass textured meshes, collision, exact round-trip validation,
complete internal-development provenance, and a package/load contract. The Relay and Sentry additionally use native
`HouseColor0`/`HouseColor1`/`HouseColor2` mesh names. Their W3Ds and shared textures are byte-stable across clean
background regenerations; normal-zoom in-engine readability remains a manual-only non-RDP gate.

Current interface boundary (2026-07-18): camera speed, UI scale, master/music/effects values, edge scroll, reduced
motion, reduced flashes, colour-independent cues, and twelve keyboard-or-mouse bindings can be changed in-window.
Primary selection and context command are remappable alongside keyboard actions, with Left/Right/Middle/Mouse 4/Mouse
5 support. Settings use a strict versioned format, reject corrupt/partial/duplicate data atomically, and save through a
same-directory temporary file to `%LOCALAPPDATA%\ProjectTempest\settings.ini`. The camera and HUD consume applicable
settings immediately; reduced motion disables edge-driven camera motion, and losing focus clears all held keyboard and
mouse inputs and pauses the interface. Three synchronised deterministic PCM score stems add pressure/crisis layers from
presentation-only match pressure, while five original cue types provide action feedback; all now pass provenance,
repeat-generation, strict WAV parsing, package, and XAudio2 compile gates. Master/music/effects settings drive separate
runtime submixes. Audible balance/device behavior, normal-zoom readability, target-resolution clipping, and manual runtime
proof of audio, persistence, and remapping remain open user-only gates. Version-four settings now also persist
disabled-by-default protanopia, deuteranopia, and tritanopia modes plus bounded strength, brightness, and contrast.
The isolated Apache-2.0 EA-derived colour transform has pinned deterministic vectors and packaged attribution; connecting
it exactly once to the final world-and-UI presentation surface, then capturing target-resolution/performance evidence,
remains open rather than being inferred from the portable tests.

Current content-model boundary (2026-07-15): the deterministic rules layer names all four Freegrid units, four Freegrid
structures, both governed abilities, three Chorus units, and three Chorus structures through data-backed definitions.
Substation 9 opens with a Fabricator rig, Courier scout, Relay Core, Fabricator Bay, Machine Nest, Skitter, and Chorus
Spire. Salvage is the only spendable construction/production resource; captured relays generate separate ability charge,
and completed Relay Core/Fabricator Bay/Dynamo structures provide capacity that live and queued units reserve. The
Fabricator Bay can produce Fabricator rigs, Courier scouts, Lancer crews, and Coil carriers; each role has distinct
movement, range, damage, build-time, salvage, and capacity data. Fabricator rigs repair damaged friendly units and
structures for salvage, while deterministic Machine Nest waves escalate through Skitters, Wardens, and Harrowers.
Arc Sentries and Signal Pylons now provide deterministic defensive pressure and dedicated runtime art, while targeted
grid-link scans report contacts and emergency overcharge temporarily boosts Freegrid movement and damage. Dedicated art
for the remaining expanded roster/structures, manual balance/legibility proof, and the governed manual playtest gates
remain M4/M5 work rather than claimed release-ready content.

### Golden asset and format decision

The first end-to-end asset is the **Freegrid Courier**, a small wheeled scout with no skeletal-animation dependency.
It must progress from concept sheet to Blender master, game mesh, collision, LODs, materials/textures, team/readability
markers, engine import, data definition, selection bounds, destruction state, and an in-engine screenshot.

Concept v1 and its complete prompt/hash record live under
[`ProjectTempest/SourceAssets/Concepts/Freegrid/Courier`](ProjectTempest/SourceAssets/Concepts/Freegrid/Courier) and
[`ProjectTempest/asset-provenance.json`](ProjectTempest/asset-provenance.json). It is an internal blockout reference,
not an exact engineering drawing or a production-ready asset.

The engine currently consumes W3D-era assets while current generation tools produce formats such as GLB. The first
technical spike must choose and prove one route:

1. build a deterministic Blender-to-W3D export/conversion path;
2. add a narrowly scoped glTF 2.0 runtime importer with cached engine-ready output; or
3. use an existing GPL-compatible converter after source, maintenance, and licence review.

The decision record must compare animation, collision, LOD, materials, determinism, build integration, licensing, and
batch automation. A manual one-off conversion is not a successful pipeline.

Decision (2026-07-15): use option 3 for the first pipeline. Pin the LGPL-3.0
[OpenSAGE Blender plugin](https://github.com/OpenSAGE/OpenSAGE.BlenderPlugin) at commit
`feb80cd0bf22b3c24c0395ae3260a5349c080892` (plugin v0.7.3), which explicitly supports Blender 5.1. The repository's
`scripts/test-w3d-pipeline.ps1` fetches that source into a short per-user tool cache and performs a headless export/import
test using only procedural geometry, with evidence written to the ignored build tree. On Blender 5.1.2, two clean exports produced the same SHA-256 and the exported
mesh imported successfully. This selected the bridge; the authored baseline below additionally proves a static hierarchy,
collision, and two LODs; at that baseline checkpoint, materials, animation, and damage states were still unproven. The
production pass below now proves textured materials and deterministic damaged-state payloads, while animation remains
outside the static Courier's current contract. W3D filenames and mesh identifiers
must be no longer than 16 characters; source names may be descriptive,
but the export mapping must enforce legal runtime identifiers.

Authored baseline (2026-07-15): the concept has been translated into a 47-part procedural Blender source, hero and
orthographic top-down review renders, and `ProjectTempest/Content/Art/W3D/courier.w3d`. The export master contains a
1,800-vertex LOD0, a 760-vertex LOD1, six single-material render submeshes per LOD, native `HouseColor0`/`HouseColor1`
team-colour meshes, and one oriented `BOUNDINGBOX` with physical, projectile, visibility, and vehicle collision flags.
W3D `HM` export is hash-stable across regeneration; immediate round-trip import recovers twelve visible submeshes and
one collision box (10,998 vertices after the plugin's W3D UV/material vertex splitting). The single-material topology
keeps every mesh below the engine's four-pass ceiling. The production pass adds seven original deterministic 128×128
TGA maps and a separate `courierd.w3d` HLOD with two burn meshes, a deformed sensor, powered-off status material, two
LODs, and the same collision contract. Two clean headless generations produced byte-identical pristine and damaged W3Ds;
empty-scene re-import recovered five pristine texture references plus explicit burn and powered-off damage references.
A CI-built `W3DViewV.exe` produced one manually captured HLOD
frame showing 3,424 polygons through the hash-pinned BSD-2-Clause d3d8to9 v1.15.1 bridge. Windows Application Error
event 1000 subsequently recorded repeated access-violation and unhandled-exception crashes under Microsoft Remote
Display, so the frame proves static geometry/HLOD loading but not renderer stability. M2 remains open until damage-state data switching,
in-engine team readability, data definition, selection bounds, and safe manual runtime verification on a suitable
non-RDP desktop are proven; textured geometry and the separate damaged-state payload now pass the headless source and
format gates.

### Windows no-visible-GUI policy

Unattended agents, CI, scheduled tasks, and automated test scripts must never launch the game, W3DView, WorldBuilder,
Blender's interactive UI, a render-device selector, or any other visible renderer window. Automated evidence is limited
to compilation, packaging, static/data validation, Blender `--background`, import/export round trips, and genuinely
headless/offscreen paths. Interactive gameplay and renderer checks are explicit manual-only actions initiated by the
user on a suitable non-RDP desktop. When no safe headless route exists, mark that evidence blocked; never retry a visible
GUI. The repository's no-GUI test is a required CI gate.

## Capability and tool routing

This is a preview review of game-relevant capabilities, not a scored end-to-end tool certification. Paid generation
has not been exercised, so the review score is intentionally `null`.

| Need | Primary route | Current verdict and constraint |
|---|---|---|
| Source, PRs, CI | GitHub, `gh`, existing Actions workflows | Ready; Codex and CodeRabbit reviews already trigger. Add enforceable PR/check rules without creating a solo-maintainer deadlock. |
| Planning/evidence | Codex goal/plan plus Linear | Ready; goal owns outcome, Linear owns durable issue/evidence state. |
| Native build | VS Build Tools 2022, bundled CMake/Ninja/MSBuild, vcpkg | Installed; not all tools are on `PATH`. Prove a scripted developer-shell/bootstrap command. |
| Engine/code | Codex CLI, code-review graph, clang-tidy, tests/replays | Strong for source work; local build and standalone runtime evidence remain unproven. |
| 3D authoring | Blender 5.1 | Ready for modelling, UVs, baking, LODs, collision, animation, and scripted batch export. Engine-format bridge is the principal risk. |
| Concept/UI/texture | Built-in image generation, then Blender/Adobe/manual cleanup | Useful for reference and controlled source material. Final assets need human-readable cleanup and provenance; generated text/logos are never accepted blindly. |
| Image-to-3D | Magnific GLB generation | Optional accelerator only. It consumes credits in this environment and requires explicit spend approval; GLB still needs cleanup and an engine bridge. |
| Video | Runway, Fal, Magnific, FFmpeg | FFmpeg is ready for evidence capture. Generative video is optional promotional work, not a dependency of this match, and remains behind spend approval. |
| Music/voice | music/TTS generation tools plus audio editor | Optional for original prototypes; distribution rights, loudness, looping, accessibility, and provenance must be checked. No cloned or impersonated voice. |
| Play/visual review | explicit user-run manual session plus captured-frame inspection | Required when a person checks the rendered window, but never agent-launched or unattended. When a safe non-RDP manual session is unavailable, record the gate blocked and rely only on headless evidence. |

Highest-value next action: prove the installed Windows build path, then prove the Courier pipeline. More media generation
before those two facts are known increases rerun cost without reducing the core risk.

No paid generation, external publishing, deployment, account/authentication change, or public asset upload is authorised
by this specification. Each requires explicit approval. Every generated or sourced asset must record source URL/tool,
model/version, prompt or brief, generation date, edits, local source path, licence/terms, and intended distribution use.

## Later evidence

The checks below are later presentation, asset, and release work. They are not Substation 9, and they are not a session
a player wins or loses. The game is the match specified above. Launch it with `./scripts/play-substation-9.sh`.

### M0 — Goal and guarded workspace

- This specification is linked from the repository entry points.
- A durable Codex goal references this file and the Linear project.
- Work occurs on `codex/` branches through pull requests.
- CI, Codex review, CodeRabbit review, and a local review are visible on those pull requests.
- Main-branch rules require PRs and selected passing checks; required human approval is not enabled until it cannot
  deadlock the sole maintainer.

### M1 — Reproducible native baseline

- A clean checkout configures and builds with documented Windows commands and pinned dependencies.
- Tool discovery does not depend on an interactive developer prompt or globally edited `PATH`.
- Tests and static checks run; any retail-data-dependent replay or launch check reports a clear skip/blocker.
- Build logs, binary hashes, tool versions, and failure notes are attached to Linear.

### M2 — Golden original asset

- The Courier passes the full format pipeline without manual binary surgery.
- Re-running export from its committed source produces the expected game-ready outputs.
- It renders in-engine with correct scale, facing, team marker, selection, collision, LOD transition, and damage state.
- Source, prompts, licences/terms, edits, and output lineage are recorded.

### M3 — Standalone boot and map

- The rendered window starts from its original content root without reading retail asset archives.
- Original boot, menu, loading, match, and result UI is functional.
- Substation 9 loads with navigation, collision, camera bounds, resources, relays, start positions, and debug overlays.
- Missing content fails with an actionable path/message rather than a silent crash.

### M4 — Match loop in the rendered window

- Freegrid construction, production, capacity, salvage, selection, commands, combat, damage, repair, and abilities work.
- Chorus AI scouts, attacks, reinforces, threatens the objective, and can win without scripted cheating.
- Win, loss, pause, restart, settings, and result flow work for three consecutive fresh launches.
- A novice playtest can issue the first command, identify the resource, build a unit, and state the objective.

### M5 — 2026-quality pass

- 60 fps presentation target at 1080p High on the reference machine, with deterministic fixed-step simulation;
  documented scalable settings and a measured frame-time capture replace unsupported claims.
- UI is usable at 1920×1080, 2560×1440, 3840×2160, and 21:9 without clipping or microscopic controls.
- Mouse and keyboard are remappable; edge scroll can be disabled; camera speed, UI scale, master/music/effects volume,
  reduced flashes/camera shake, colour-independent ownership cues, and pause are available.
- Commands have immediate visible/audio feedback; unit silhouettes and threats remain readable during combat.
- The rendered window survives a 30-minute soak, repeated restart, alt-tab, resolution change, and clean shutdown without a new
  crash, assert, or unbounded resource growth.

### M6 — Release evidence

- A clean-machine or clean-VM build/install rehearsal succeeds from the documented inputs.
- CI and all applicable tests pass; automated and local review findings are resolved or explicitly accepted with risk.
- Two full playthroughs, screenshots at target resolutions, a short evidence capture, logs, performance trace, asset
  provenance inventory, licences/notices, known issues, and binary/source hashes are linked from Linear.
- A private reproducible package of the rendered window is prepared. Public release remains a separate explicit approval decision.

## Review and change policy

Each later-evidence item ends in a narrow pull request. Before merge:

1. build and targeted tests pass locally where available;
2. GitHub Actions completes the applicable matrix;
3. Codex automated review and CodeRabbit findings are read and resolved or answered;
4. a fresh local review checks correctness, regressions, security, provenance, and scope;
5. player-visible work includes real launch/play evidence, not only a compile or screenshot of source; and
6. the Linear issue receives the PR, commands, results, evidence paths, known risks, and next smallest action.

Do not auto-merge failed, pending, or unreviewed work. Do not weaken tests to make a gate green. Upstream-compatible
engine fixes should remain separable from Project Tempest identity/content changes.

## Stop, pivot, and blocker policy

Continue through safe, reversible checkpoints. A blocker is actionable only when it records the blocked outcome, owner,
risk, current evidence, attempted alternatives, and smallest requested input.

The primary base remains this Generals-derived engine. A switch to another open-source engine/game, including Warzone
2100, requires a documented architecture and licence comparison plus explicit approval. Consider that pivot only if the
standalone content boundary or maintainable 3D pipeline cannot be proven after bounded spikes; ordinary difficulty is
not a pivot reason.

The game this repository ships is the Substation 9 match specified above. A player finishes it from
`./scripts/play-substation-9.sh` by reaching `GRID RESTORED` or `DISTRICT LOST`. The M1–M6 list is later evidence for
the rendered window and the engine programme. It does not redefine the match, and it is not required to finish that match.
