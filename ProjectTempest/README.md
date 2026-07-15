# Project Tempest Original Content

This tree holds the legally distinct source material and, later, runtime content for the standalone Project Tempest
demo. It must never contain copied or derived Electronic Arts retail assets.

## Layout

- `SourceAssets/Concepts`: original visual references used to author production assets.
- `SourceAssets/Models`: future editable Blender masters, collision sources, LOD sources, and export settings.
- `Content`: future loose runtime data using the engine's existing `Data`, `Art`, map, audio, and localisation paths.
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

The generator splits each visual material into a single-pass render submesh, producing six submeshes per LOD plus a
`BOUNDINGBOX` collision primitive with physical, projectile, visibility, and vehicle flags. `HouseColor0` and
`HouseColor1` deliberately use Generals' native mesh-name recolouring convention. It exports in W3D `HM` mode and
immediately re-imports the file, failing unless all twelve render meshes, both house-colour meshes, and exactly one
collision box survive the round trip. This one-material-per-submesh rule is required because the engine supports no more
than four render passes per mesh. The W3D output is deterministic; Workbench preview PNG and Blender container hashes are
recorded for the committed artifacts but are not deterministic build keys.

The runtime W3D has a captured frame from the repository's native `W3DViewV.exe`. The viewer is unstable under Microsoft
Remote Display: repeated Application Error event 1000 crashes occurred even with the compatibility bridge. Therefore
the following command may prepare dependencies, but it does not launch the viewer:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\prepare-w3dview-compat.ps1 `
  -ViewerDirectory .\build\ci-w3dview-fixed
```

The captured engine result is
[`courier-w3dview-engine.png`](ReviewEvidence/courier-w3dview-engine.png). It proves geometry and HLOD loading, not final
materials or renderer stability; the production texture/material pass remains open.

## Windows execution safety

All unattended validation is compile-, package-, data-, import-, or offscreen-only. Agents, CI jobs, scheduled tasks,
and automated test scripts must never launch `W3DViewV.exe`, `ProjectTempestDemo.exe`, `generalsv.exe`,
`WorldBuilderV.exe`, Blender's interactive UI, a render-device selector, or any other visible game/tool window.
`scripts/test-project-tempest-no-gui.ps1` enforces the no-process-launch contract on Project Tempest validation surfaces.

Interactive renderer and gameplay checks are manual-only actions initiated by the user on a suitable non-RDP desktop.
If that environment is unavailable, record visual/gameplay verification as blocked and continue with headless evidence;
do not retry a visible GUI. `prepare-w3dview-compat.ps1` only verifies and copies dependencies and reports
`LaunchPolicy=manual_only`.

## Standalone prototype

Modern Generals Win32 builds also produce `ProjectTempestDemo.exe`, a retail-asset-free executable that loads the
Courier directly from this tree. Its current M2 interaction slice provides a fixed RTS camera, selection, right-click
movement, keyboard movement, restart, and a simple uplink objective. It is an executable integration checkpoint, not
the final Substation 9 vertical slice.

Controls:

- Left-click selects the Courier.
- Right-click sets a movement target on the flat prototype arena.
- `WASD` or the arrow keys nudge the selected unit.
- `R` restarts the prototype objective; `Esc` exits.

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
