# Project Tempest Original Content

This tree holds the legally distinct source material and, later, runtime content for the standalone Project Tempest
demo. It must never contain copied or derived Electronic Arts retail assets.

## Layout

- `SourceAssets/Concepts`: original visual references used to author production assets.
- `SourceAssets/Models`: future editable Blender masters, collision sources, LOD sources, and export settings.
- `Content`: future loose runtime data using the engine's existing `Data`, `Art`, map, audio, and localisation paths.
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

The generator authors `CRLOD0`, a 0.35-ratio decimated `CRLOD1`, dedicated `CRTEAM0`/`CRTEAM1` house-colour submeshes,
and a `BOUNDINGBOX` collision primitive with physical, projectile, visibility, and vehicle flags. It exports in W3D `HM`
mode and immediately re-imports the file, failing unless both LODs, both `UseRecolorColors` materials, and exactly one
collision box survive the round trip. The W3D output is deterministic; Workbench preview PNG and Blender container hashes
are recorded for the committed artifacts but are not deterministic build keys.
