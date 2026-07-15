# Project Tempest Standalone Demo Specification

Status: governing source for the first standalone playable demo.  
Internal slice codename: **Black Current**. This is not an approved public product name.  
Tracking: Linear project `200 Build Room / Project Tempest`, led by KOL-4608 and KOL-4610.

## Outcome

Deliver a reproducible, locally playable Windows real-time strategy demo derived from this GPL-licensed engine, using
only original or distribution-cleared game content. A new player must be able to launch it from a clean documented
build, complete a 15–20 minute skirmish, understand why they won or lost, change essential settings, restart, and play
again without an Electronic Arts retail installation or redistributed Electronic Arts assets.

The demo is evidence for a new game, not a promise of the full product. It proves the engine, content boundary, asset
pipeline, core loop, AI, presentation, and review process at a scale that can be finished and judged.

## Creative spine

Method: First Principles. The familiar faction-and-unit surface of the original game is not carried forward. The
retained value is the deterministic RTS simulation, readable command model, data-driven content, and mature tooling.

### Player fantasy

In 2089, the storm-battered Kestrel Basin is divided between sealed corporate arcologies and neighbourhood power
meshes assembled from abandoned infrastructure. The player commands **Freegrid**, a mobile crew that turns scrap,
stolen current, and civic machinery into a fighting network. Its enemy is **Chorus**, an extra-terrestrial signal that
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

## First playable: “Substation 9”

The standalone demo contains one skirmish scenario on one original urban map.

### Match arc

1. **Establish:** deploy the Freegrid Relay Core, send the Fabricator rig to nearby scrap, and restore one grid relay.
2. **Expand:** use the live relay to reveal routes, increase production capacity, and choose where to place defences.
3. **Contest:** Chorus probes the network, grows from fixed machine nests, and competes for the central substation.
4. **Escalate:** the player fields a compact combined-arms force and breaks either the nests or the central Spire.
5. **Resolve:** destroying the Chorus Spire wins; losing the Relay Core loses. The result screen explains decisive
   events and offers restart and settings without returning to the desktop.

Target duration is 15–20 minutes for a first successful play, with the opening command issued in under 30 seconds.

### Content budget

Freegrid is the only player-controlled faction in this slice:

| Type | Demo content | Purpose |
|---|---|---|
| Structures | Relay Core, Fabricator Bay, Dynamo, Arc Sentry | base, production, capacity, defence |
| Units | Fabricator rig, Courier scout, Lancer crew, Coil carrier | build/repair, vision, light combat, heavy combat |
| Abilities | grid-link scan, emergency overcharge | information and a short power spike |

Chorus is AI-controlled:

| Type | Demo content | Purpose |
|---|---|---|
| Structures | Machine Nest, Signal Pylon, Chorus Spire | production, map pressure, victory target |
| Units | Skitter, Warden, Harrower | harassment, ranged control, siege threat |

One resource, **salvage**, pays construction costs. Grid relays provide capacity, vision, and ability charge rather
than introducing a second spendable currency. This keeps decisions legible while making territory matter.

Excluded from this demo: campaign, cinematics, multiplayer, matchmaking, spectator mode, monetisation, account
systems, user-generated-content distribution, a second playable faction, hero units, naval or air combat, and public
release. These can only enter after the standalone skirmish passes its gates.

## Technical strategy

### Preserve

- deterministic simulation and replay-friendly command processing;
- the existing object, weapon, locomotor, AI, map, and UI data boundaries where they remain fit for purpose;
- the active community CMake/Visual Studio baseline and upstream-compatible fixes;
- separation between simulation time and presentation/render timing.

The simulation may remain fixed-step. Presentation should interpolate independently so 60 Hz, 120 Hz, and variable
refresh displays do not change game outcomes.

### Replace or isolate

- retail `.big` archive assumptions on the standalone demo path;
- retail bootstrap screens, names, strings, UI, maps, models, textures, shaders/effects, audio, video, and localisation;
- fixed-resolution UI assumptions and frame-rate-dependent presentation;
- legacy platform APIs only where they block a verified demo requirement.

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
protocol, renderer replacement, and broad platform abstraction are outside this slice unless evidence shows they block it.

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
mesh imported successfully. This selects the bridge but does not yet prove materials, collision, LODs, hierarchy, or
damage states. W3D filenames and mesh identifiers must be no longer than 16 characters; source names may be descriptive,
but the export mapping must enforce legal runtime identifiers.

Authored baseline (2026-07-15): the concept has been translated into a 47-part procedural Blender master, hero and
orthographic top-down review renders, and `ProjectTempest/Content/Art/W3D/courier.w3d`. The W3D export hash is stable
across regeneration and round-trip imports as one non-empty 7,736-vertex mesh. This proves an original editable source
and engine-format payload, but M2 remains open until collision, production LODs, damage state, materials/textures, team
readability markers, data definition, selection bounds, and an actual Project Tempest engine render pass.

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
| Video | Runway, Fal, Magnific, FFmpeg | FFmpeg is ready for evidence capture. Generative video is optional promotional work, not a demo dependency, and remains behind spend approval. |
| Music/voice | music/TTS generation tools plus audio editor | Optional for original prototypes; distribution rights, loudness, looping, accessibility, and provenance must be checked. No cloned or impersonated voice. |
| Play/visual review | local game review and captured-frame inspection | Required at each playable milestone. Browser-first Games Lab prompts are reference material only; native C++/W3D evidence governs this project. |

Highest-value next action: prove the installed Windows build path, then prove the Courier pipeline. More media generation
before those two facts are known increases rerun cost without reducing the core risk.

No paid generation, external publishing, deployment, account/authentication change, or public asset upload is authorised
by this specification. Each requires explicit approval. Every generated or sourced asset must record source URL/tool,
model/version, prompt or brief, generation date, edits, local source path, licence/terms, and intended distribution use.

## Milestones and exit evidence

### M0 — Goal and guarded workspace

- This specification is linked from the repository entry points.
- A durable Codex goal references this file and the Linear project.
- Work occurs on `codex/` branches through pull requests.
- CI, Codex review, CodeRabbit review, and a local review are visible on milestone PRs.
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

- The demo starts from its original content root without reading retail asset archives.
- Original boot/menu/loading/skirmish/result UI is functional.
- Substation 9 loads with navigation, collision, camera bounds, resources, relays, start positions, and debug overlays.
- Missing content fails with an actionable path/message rather than a silent crash.

### M4 — Complete skirmish loop

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
- The demo survives a 30-minute soak, repeated restart, alt-tab, resolution change, and clean shutdown without a new
  crash, assert, or unbounded resource growth.

### M6 — Demo gate

- A clean-machine or clean-VM build/install rehearsal succeeds from the documented inputs.
- CI and all applicable tests pass; automated and local review findings are resolved or explicitly accepted with risk.
- Two full playthroughs, screenshots at target resolutions, a short evidence capture, logs, performance trace, asset
  provenance inventory, licences/notices, known issues, and binary/source hashes are linked from Linear.
- A private reproducible demo package is prepared. Public release remains a separate explicit approval decision.

## Review and change policy

Every milestone ends in a narrow pull request. Before merge:

1. build and targeted tests pass locally where available;
2. GitHub Actions completes the applicable matrix;
3. Codex automated review and CodeRabbit findings are read and resolved or answered;
4. a fresh local review checks correctness, regressions, security, provenance, and scope;
5. player-visible work includes real launch/play evidence, not only a compile or screenshot of source; and
6. the Linear milestone/issue receives the PR, commands, results, evidence paths, known risks, and next smallest action.

Do not auto-merge failed, pending, or unreviewed work. Do not weaken tests to make a gate green. Upstream-compatible
engine fixes should remain separable from Project Tempest identity/content changes.

## Stop, pivot, and blocker policy

Continue through safe, reversible checkpoints. A blocker is actionable only when it records the blocked outcome, owner,
risk, current evidence, attempted alternatives, and smallest requested input.

The primary base remains this Generals-derived engine. A switch to another open-source engine/game, including Warzone
2100, requires a documented architecture and licence comparison plus explicit approval. Consider that pivot only if the
standalone content boundary or maintainable 3D pipeline cannot be proven after bounded spikes; ordinary difficulty is
not a pivot reason.

The goal is not complete because code builds, one asset renders, or a scripted scene runs. It completes only when the
M6 evidence proves a clean, original, fully playable skirmish demo.
