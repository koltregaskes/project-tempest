"""Blender-driven smoke test for Project Tempest's original W3D asset pipeline."""

import hashlib
import json
import os
import sys

import bpy


def fail(message: str) -> None:
    print(f"TEMPEST_W3D_ERROR {message}")
    raise RuntimeError(message)


plugin_root = os.environ.get("TEMPEST_W3D_PLUGIN_ROOT")
output_root = os.environ.get("TEMPEST_W3D_OUTPUT_ROOT")

if not plugin_root or not os.path.isdir(plugin_root):
    fail("TEMPEST_W3D_PLUGIN_ROOT does not identify the pinned OpenSAGE plugin checkout")
if not output_root:
    fail("TEMPEST_W3D_OUTPUT_ROOT is not set")

os.makedirs(output_root, exist_ok=True)
output_path = os.path.join(output_root, "courier.w3d")
result_path = os.path.join(output_root, "result.json")

sys.path.insert(0, plugin_root)
import io_mesh_w3d  # noqa: E402

io_mesh_w3d.register()

# Use only procedural geometry created in this process. The smoke test must never depend on retail game assets.
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.mesh.primitive_cube_add(location=(0.0, 0.0, 0.5), scale=(1.5, 0.8, 0.5))
source_object = bpy.context.active_object
source_object.name = "PT_COURIER"
source_object.data.name = "PT_COURIER_M"

export_result = bpy.ops.export_mesh.westwood_w3d(filepath=output_path, export_mode="M")
if export_result != {"FINISHED"} or not os.path.isfile(output_path):
    fail(f"W3D export failed: {export_result}")

with open(output_path, "rb") as exported_file:
    sha256 = hashlib.sha256(exported_file.read()).hexdigest()

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
import_result = bpy.ops.import_mesh.westwood_w3d(filepath=output_path)
meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
vertex_count = sum(len(obj.data.vertices) for obj in meshes)

if import_result != {"FINISHED"} or not meshes or vertex_count == 0:
    fail(f"W3D import failed: result={import_result}, meshes={len(meshes)}, vertices={vertex_count}")

result = {
    "blender_version": bpy.app.version_string,
    "export_result": sorted(export_result),
    "import_result": sorted(import_result),
    "mesh_count": len(meshes),
    "output_file": os.path.basename(output_path),
    "plugin_version": ".".join(str(part) for part in io_mesh_w3d.VERSION),
    "sha256": sha256,
    "vertex_count": vertex_count,
}

with open(result_path, "w", encoding="utf-8") as result_file:
    json.dump(result, result_file, indent=2, sort_keys=True)
    result_file.write("\n")

print("TEMPEST_W3D_RESULT", json.dumps(result, sort_keys=True))
