# Project Tempest Delivery Contract

Project Tempest is the working identity for a modern, legally distinct real-time strategy game derived from the GPLv3
engine source released by Electronic Arts. The project starts from the active `TheSuperHackers/GeneralsGameCode`
lineage because it already supplies a modern CMake, Visual Studio 2022 and C++20 baseline, dependency management,
profiling, continuous integration, replay compatibility tests, and a substantial body of reviewed fixes.

The working identity is deliberately neutral. It is not a new name for an Electronic Arts title and must not be used
to imply endorsement, affiliation, or ownership of Electronic Arts trademarks.

## Source lineage

- Original source release: `electronicarts/CnC_Generals_Zero_Hour`
- Active community engineering base: `TheSuperHackers/GeneralsGameCode`
- Project Tempest downstream: `koltregaskes/project-tempest`
- Original licence: GNU GPL version 3 with the Electronic Arts Section 7 additional terms in `LICENSE.md`

All upstream copyright, attribution, warranty, provenance, and modification notices must remain intact. New or changed
source must remain GPL-compatible.

## Player outcome

The target is a stable, responsive RTS that feels native on current hardware rather than a compatibility wrapper around
a 2003 executable. The programme is complete only when it has:

1. reproducible builds and signed, traceable release inputs;
2. a 64-bit and cross-platform-capable engine path;
3. modern rendering, high-refresh timing, robust frame pacing, and scalable performance;
4. resolution-independent 4K and ultrawide UI, remappable controls, and practical accessibility options;
5. modern networking, deterministic replays, spectator support, and an explicit anti-cheat boundary;
6. a legally distinct product identity plus replaceable art, audio, text, maps, and localisation pipelines; and
7. automated checks backed by real playtests, screenshots, logs, input checks, and performance evidence.

## Architecture boundary

Simulation state must remain deterministic and separate from rendering, presentation timing, input sampling, and online
service concerns. Modernization work should preserve the community project's retail replay compatibility until a
deliberate versioned protocol break is approved.

The first implementation lanes are:

- **Foundation:** reproduce the community Windows build and replay-test baseline.
- **Presentation:** fix frame-rate-dependent presentation and make HUD/UI scaling resolution-independent.
- **Platform:** remove 32-bit assumptions and isolate operating-system, renderer, audio, video, and networking APIs.
- **Experience:** add modern input, accessibility, onboarding, settings, save/replay, and spectator surfaces.
- **Standalone:** replace protected identity and retail-only content dependencies with an original, auditable asset set.

## Release gates

No modified public binary or release is allowed until all applicable gates pass:

- the product name, executable identity, UI, packaging, store copy, and promotional material use no Electronic Arts
  trademark;
- the release includes the GPLv3 source, required notices, modification notice, and Electronic Arts additional terms;
- every bundled asset has recorded provenance and distribution rights;
- the build is reproducible from documented tools and contains no proprietary SDK or credential;
- automated build and replay checks pass;
- a real game session has been checked at relevant resolutions and input modes, with screenshots and error logs; and
- performance and compatibility evidence is attached to the release decision.

Until the standalone gate passes, local engine testing may require a legally owned installation of the original retail
game data. That prerequisite is never permission to copy, commit, upload, or redistribute those assets.

## First milestone: verified modern baseline

The first milestone is intentionally narrow:

1. build the current community baseline with the supported 32-bit Windows preset;
2. run its automated checks and headless replay suite against a legal local game installation;
3. capture the current frame pacing, resolution/UI, input, and crash baseline;
4. select one player-visible improvement not already covered by an active community pull request;
5. implement it in a reviewable change and verify it in a real session; and
6. feed reusable fixes upstream when they do not depend on Project Tempest's distinct identity.

Large renderer, 64-bit, multiplayer, or asset-pipeline work must be split into independently verifiable slices. A plan or
successful compile is not evidence that the game is playable.

The first standalone playable is governed by
[PROJECT_TEMPEST_DEMO_SPEC.md](PROJECT_TEMPEST_DEMO_SPEC.md). Where that specification narrows this programme to a
finishable demo, its content budget, milestone evidence, and review gates control the work; this delivery contract
continues to govern the longer programme and public-release boundary.
