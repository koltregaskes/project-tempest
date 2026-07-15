"""Create original Chorus Drone and Freegrid Relay W3D assets in headless Blender."""

import hashlib
import json
import math
import os
import sys

import bpy
from mathutils import Vector


def fail(message):
    print(f"TEMPEST_SUBSTATION_KIT_ERROR {message}")
    raise RuntimeError(message)


project_root = os.environ.get("TEMPEST_PROJECT_ROOT")
output_root = os.environ.get("TEMPEST_OUTPUT_ROOT", project_root)
plugin_root = os.environ.get("TEMPEST_W3D_PLUGIN_ROOT")
if not project_root or not os.path.isdir(project_root):
    fail("TEMPEST_PROJECT_ROOT is not a valid directory")
if not output_root:
    fail("TEMPEST_OUTPUT_ROOT is not a valid directory")
os.makedirs(output_root, exist_ok=True)
if not plugin_root or not os.path.isdir(plugin_root):
    fail("TEMPEST_W3D_PLUGIN_ROOT is not a valid OpenSAGE plugin checkout")

sys.path.insert(0, plugin_root)
import io_mesh_w3d  # noqa: E402

io_mesh_w3d.register()
bpy.context.preferences.filepaths.save_version = 0

runtime_root = os.path.join(output_root, "ProjectTempest", "Content", "Art", "W3D")
texture_root = os.path.join(output_root, "ProjectTempest", "Content", "Art", "Textures")
evidence_root = os.path.join(output_root, "build", "substation-kit")
os.makedirs(runtime_root, exist_ok=True)
os.makedirs(texture_root, exist_ok=True)
os.makedirs(evidence_root, exist_ok=True)


def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for mesh in list(bpy.data.meshes):
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    for collection in list(bpy.data.collections):
        if collection.users == 0 or collection.name != bpy.context.scene.collection.name:
            bpy.data.collections.remove(collection)


def create_texture(filename, base_color, accent_color):
    size = 128
    image = bpy.data.images.new(filename, width=size, height=size, alpha=True)
    pixels = [0.0] * (size * size * 4)
    for y in range(size):
        for x in range(size):
            noise = ((x * 29 + y * 43 + x * y * 11) % 101) / 100.0
            circuit = (x % 32 <= 2) or (y % 32 <= 2) or ((x + 2 * y) % 47 <= 1)
            color = accent_color if circuit else base_color
            factor = 0.76 + (0.24 * noise)
            index = (y * size + x) * 4
            pixels[index:index + 4] = [
                min(1.0, color[0] * factor),
                min(1.0, color[1] * factor),
                min(1.0, color[2] * factor),
                1.0,
            ]
    image.pixels = pixels
    image.filepath_raw = os.path.join(texture_root, filename)
    image.file_format = "TARGA_RAW"
    image.save()
    return image


def load_texture(filename):
    path = os.path.join(texture_root, filename)
    if not os.path.isfile(path):
        fail(f"Required Courier-pipeline texture is missing: {path}")
    return bpy.data.images.load(path, check_existing=True)


def material(name, color, texture=None, metallic=0.0, roughness=0.55, emission=None):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1.0)
    mat.material_type = "VERTEX_MATERIAL"
    mat.ambient = (0.28, 0.28, 0.28, 0.0)
    mat.specular = (0.20, 0.20, 0.20) if metallic > 0.1 else (0.04, 0.04, 0.04)
    mat.surface_type = "1" if metallic > 0.5 else "13"
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get("Principled BSDF")
    if shader:
        shader.inputs["Base Color"].default_value = (*color, 1.0)
        shader.inputs["Metallic"].default_value = metallic
        shader.inputs["Roughness"].default_value = roughness
        if emission:
            shader.inputs["Emission Color"].default_value = (*emission, 1.0)
            shader.inputs["Emission Strength"].default_value = 3.5
        if texture:
            texture_node = mat.node_tree.nodes.new("ShaderNodeTexImage")
            texture_node.image = texture
            mat.node_tree.links.new(texture_node.outputs["Color"], shader.inputs["Base Color"])
    return mat


steel_texture = load_texture("ptsteel.tga")
white_texture = load_texture("ptwhite.tga")
cyan_texture = load_texture("ptcyan.tga")
magenta_texture = create_texture("ptmagnta.tga", (0.22, 0.01, 0.10), (1.0, 0.05, 0.42))

steel = material("PT_KIT_STEEL", (0.68, 0.70, 0.72), steel_texture, metallic=0.75, roughness=0.38)
white = material("PT_KIT_WHITE", (0.82, 0.80, 0.70), white_texture, metallic=0.25, roughness=0.58)
cyan = material(
    "PT_KIT_CYAN", (0.72, 0.78, 0.80), cyan_texture, roughness=0.25, emission=(0.0, 0.72, 0.92)
)
magenta = material(
    "PT_KIT_MAGENTA", (0.78, 0.74, 0.76), magenta_texture, metallic=0.2, roughness=0.32,
    emission=(0.72, 0.0, 0.20)
)


def make_builder():
    objects = []
    groups = {}

    def remember(obj, mat):
        if mat:
            obj.data.materials.append(mat)
            groups.setdefault(mat.name, []).append(obj)
            objects.append(obj)
        return obj

    def box(name, location, dimensions, mat, rotation=(0.0, 0.0, 0.0), bevel=0.05):
        bpy.ops.mesh.primitive_cube_add(location=location, rotation=rotation)
        obj = bpy.context.active_object
        obj.name = name
        obj.dimensions = dimensions
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        if bevel:
            modifier = obj.modifiers.new("PT_BEVEL", "BEVEL")
            modifier.width = bevel
            modifier.segments = 2
            bpy.context.view_layer.objects.active = obj
            bpy.ops.object.modifier_apply(modifier=modifier.name)
        return remember(obj, mat)

    def cylinder(name, location, radius, depth, mat, rotation=(0.0, 0.0, 0.0), vertices=16):
        bpy.ops.mesh.primitive_cylinder_add(
            vertices=vertices, radius=radius, depth=depth, location=location, rotation=rotation
        )
        obj = bpy.context.active_object
        obj.name = name
        return remember(obj, mat)

    def pipe(name, start, end, radius, mat, vertices=10):
        start_v = Vector(start)
        end_v = Vector(end)
        direction = end_v - start_v
        obj = cylinder(name, (start_v + end_v) * 0.5, radius, direction.length, mat, vertices=vertices)
        obj.rotation_mode = "QUATERNION"
        obj.rotation_quaternion = direction.to_track_quat("Z", "Y")
        return obj

    return objects, groups, box, cylinder, pipe


def build_drone():
    objects, groups, box, cylinder, pipe = make_builder()
    box("DRCORE", (0.0, 0.0, 0.92), (1.35, 1.15, 0.48), steel, rotation=(0.0, 0.0, math.radians(45)), bevel=0.12)
    cylinder("DREYE", (0.0, -0.58, 0.94), 0.22, 0.12, magenta, rotation=(math.radians(90), 0.0, 0.0), vertices=20)
    cylinder("DRCROWN", (0.0, 0.0, 1.23), 0.42, 0.18, magenta, vertices=20)
    for index, angle in enumerate((0.0, 120.0, 240.0)):
        radians = math.radians(angle)
        inner = (math.cos(radians) * 0.45, math.sin(radians) * 0.45, 0.92)
        outer = (math.cos(radians) * 1.55, math.sin(radians) * 1.55, 0.78)
        pipe(f"DRARM{index}", inner, outer, 0.11, steel, vertices=10)
        box(
            f"DRPOD{index}", outer, (0.84, 0.44, 0.28), magenta,
            rotation=(0.0, 0.0, radians), bevel=0.08
        )
        glow = (outer[0], outer[1], outer[2] - 0.18)
        cylinder(f"DRGLW{index}", glow, 0.24, 0.10, cyan, vertices=18)
        tip = (math.cos(radians) * 2.05, math.sin(radians) * 2.05, 0.72)
        box(
            f"DRFIN{index}", tip, (0.78, 0.14, 0.32), steel,
            rotation=(0.0, math.radians(-12), radians), bevel=0.035
        )
    return objects, groups


def build_relay():
    objects, groups, box, cylinder, pipe = make_builder()
    cylinder("RLBASE", (0.0, 0.0, 0.28), 1.18, 0.56, steel, vertices=24)
    cylinder("RLPLINTH", (0.0, 0.0, 0.68), 0.76, 0.42, white, vertices=20)
    box("RLTOWER", (0.0, 0.0, 2.0), (0.62, 0.62, 2.45), steel, rotation=(0.0, 0.0, math.radians(45)), bevel=0.09)
    for level, z in enumerate((1.08, 1.62, 2.18, 2.74)):
        cylinder(f"RLCOIL{level}", (0.0, 0.0, z), 0.48, 0.12, cyan, vertices=20)
    for index, angle in enumerate((0.0, 120.0, 240.0)):
        radians = math.radians(angle)
        start = (math.cos(radians) * 0.28, math.sin(radians) * 0.28, 2.72)
        end = (math.cos(radians) * 1.05, math.sin(radians) * 1.05, 3.42)
        pipe(f"RLARM{index}", start, end, 0.075, steel, vertices=10)
        panel = (math.cos(radians) * 1.13, math.sin(radians) * 1.13, 3.48)
        box(
            f"RLPANEL{index}", panel, (0.68, 0.16, 0.82), white,
            rotation=(0.0, math.radians(-18), radians), bevel=0.04
        )
        cylinder(f"RLLAMP{index}", (panel[0], panel[1], panel[2] + 0.48), 0.11, 0.20, cyan, vertices=14)
    pipe("RLSPIRE", (0.0, 0.0, 3.18), (0.0, 0.0, 4.32), 0.045, cyan, vertices=10)
    return objects, groups


def join_objects(objects, object_name):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    joined = bpy.context.active_object
    joined.name = object_name
    joined.data.name = f"{object_name}_M"
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return joined


def create_lod(source, collection, name, ratio):
    lod = source.copy()
    lod.data = source.data.copy()
    lod.name = name
    lod.data.name = f"{name}_M"
    collection.objects.link(lod)
    modifier = lod.modifiers.new("PT_DECIMATE", "DECIMATE")
    modifier.ratio = ratio
    modifier.use_collapse_triangulate = True
    bpy.context.view_layer.objects.active = lod
    lod.select_set(True)
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    lod.select_set(False)
    return lod


def quantize_uvs(objects):
    for obj in objects:
        for uv_layer in obj.data.uv_layers:
            for uv_loop in uv_layer.data:
                uv_loop.uv = (round(float(uv_loop.uv.x), 5), round(float(uv_loop.uv.y), 5))


def render_preview(path, object_extent, target_z):
    bpy.ops.mesh.primitive_plane_add(size=30.0, location=(0.0, 0.0, 0.0))
    ground = bpy.context.active_object
    ground.name = "PT_PREVIEW_GROUND"
    preview_material = material("PT_KIT_PREVIEW", (0.018, 0.023, 0.030), roughness=0.95)
    ground.data.materials.append(preview_material)
    bpy.ops.object.camera_add(location=(0.0, 0.0, object_extent * 2.6))
    camera = bpy.context.active_object
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = object_extent
    camera.rotation_euler = (0.0, 0.0, 0.0)
    camera.data.lens = 58.0
    camera.location.z += target_z
    bpy.context.scene.camera = camera
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "TEXTURE"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.render.resolution_x = 960
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = path
    scene.world.color = (0.01, 0.014, 0.021)
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(ground, do_unlink=True)
    bpy.data.objects.remove(camera, do_unlink=True)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def artifact(path):
    return {"path": os.path.relpath(path, output_root).replace("\\", "/"), "sha256": sha256(path)}


def export_asset(name, built_objects, groups, specs, collision_center, collision_size, preview_extent, target_z):
    source_root = os.path.join(output_root, "ProjectTempest", "SourceAssets", "Models", *name["source_parts"])
    os.makedirs(source_root, exist_ok=True)
    blend_path = os.path.join(source_root, f"{name['slug']}-master-v1.blend")
    preview_path = os.path.join(source_root, f"{name['slug']}-top-v1.png")
    w3d_path = os.path.join(runtime_root, f"{name['runtime']}.w3d")

    render_preview(preview_path, preview_extent, target_z)
    lod0 = [join_objects(groups[mat.name], f"{prefix}0") for mat, prefix, _lod1, _lod2 in specs]

    bpy.ops.mesh.primitive_cube_add(location=collision_center)
    collision = bpy.context.active_object
    collision.name = "BOUNDINGBOX"
    collision.dimensions = collision_size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    collision.data.object_type = "BOX"
    collision.data.box_type = "1"
    collision.data.box_collision_types = {"PHYSICAL", "PROJECTILE", "VIS", "VEHICLE"}
    collision.display_type = "WIRE"
    collision.hide_render = True

    lod1_collection = bpy.data.collections.new("LOD1")
    lod2_collection = bpy.data.collections.new("LOD2")
    bpy.context.scene.collection.children.link(lod1_collection)
    lod1_collection.children.link(lod2_collection)
    lod1 = [
        create_lod(source, lod1_collection, f"{prefix}1", ratio1)
        for source, (_mat, prefix, ratio1, _ratio2) in zip(lod0, specs, strict=True)
    ]
    lod2 = [
        create_lod(source, lod2_collection, f"{prefix}2", ratio2)
        for source, (_mat, prefix, _ratio1, ratio2) in zip(lod0, specs, strict=True)
    ]
    quantize_uvs(lod0 + lod1 + lod2)
    runtime_names = [obj.name for obj in lod0 + lod1 + lod2] + [collision.name]
    if any(len(runtime_name) > 16 for runtime_name in runtime_names):
        fail(f"Runtime identifier exceeds 16 characters: {runtime_names}")
    expected_render_names = sorted(runtime_names[:-1])
    expected_texture_files = sorted({
        os.path.basename(node.image.filepath or node.image.name).lower()
        for mat, _prefix, _ratio1, _ratio2 in specs
        if mat.use_nodes
        for node in mat.node_tree.nodes
        if node.type == "TEX_IMAGE" and node.image is not None
    })
    expected_collision_flags = {"PHYSICAL", "PROJECTILE", "VIS", "VEHICLE"}

    bpy.ops.wm.save_as_mainfile(filepath=blend_path)
    export_result = bpy.ops.export_mesh.westwood_w3d(filepath=w3d_path, export_mode="HM")
    if export_result != {"FINISHED"} or not os.path.isfile(w3d_path):
        fail(f"{name['slug']} W3D export failed: {export_result}")

    authored_counts = [sum(len(obj.data.vertices) for obj in lod) for lod in (lod0, lod1, lod2)]
    if not authored_counts[0] > authored_counts[1] > authored_counts[2] > 0:
        fail(f"{name['slug']} LOD counts are not strictly decreasing: {authored_counts}")

    clear_scene()
    import_result = bpy.ops.import_mesh.westwood_w3d(filepath=w3d_path)
    imported_meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    imported_render = [obj for obj in imported_meshes if getattr(obj.data, "object_type", None) != "BOX"]
    imported_boxes = [obj for obj in imported_meshes if getattr(obj.data, "object_type", None) == "BOX"]
    imported_render_names = sorted(obj.name for obj in imported_render)
    invalid_material_counts = {
        obj.name: len(obj.data.materials)
        for obj in imported_render
        if len(obj.data.materials) != 1
    }
    empty_render_meshes = sorted(
        obj.name for obj in imported_render if len(obj.data.vertices) == 0
    )
    imported_collision_flags = (
        set(imported_boxes[0].data.box_collision_types) - {"DEFAULT"}
        if len(imported_boxes) == 1
        else set()
    )
    imported_texture_files = sorted({
        os.path.basename(node.image.filepath or node.image.name).lower()
        for obj in imported_render
        for imported_material in obj.data.materials
        if imported_material is not None and imported_material.use_nodes
        for node in imported_material.node_tree.nodes
        if node.type == "TEX_IMAGE" and node.image is not None
    })
    if (
        import_result != {"FINISHED"}
        or imported_render_names != expected_render_names
        or len(imported_boxes) != 1
        or invalid_material_counts
        or empty_render_meshes
        or imported_collision_flags != expected_collision_flags
        or imported_texture_files != expected_texture_files
    ):
        fail(
            f"{name['slug']} roundtrip failed: result={import_result}, "
            f"render={imported_render_names}, expected_render={expected_render_names}, "
            f"boxes={len(imported_boxes)}, materials={invalid_material_counts}, "
            f"empty_render_meshes={empty_render_meshes}, "
            f"collision_flags={sorted(imported_collision_flags)}, "
            f"textures={imported_texture_files}, expected_textures={expected_texture_files}"
        )

    return {
        "name": name["slug"],
        "source_part_count": len(built_objects),
        "authored_lod_vertex_counts": authored_counts,
        "imported_render_mesh_count": len(imported_render),
        "imported_render_mesh_names": imported_render_names,
        "imported_box_count": len(imported_boxes),
        "imported_collision_flags": sorted(imported_collision_flags),
        "imported_texture_files": imported_texture_files,
        "max_material_passes_per_render_mesh": max(len(obj.data.materials) for obj in imported_render),
        "house_color_meshes": [mesh_name for mesh_name in imported_render_names if mesh_name.startswith("HouseColor")],
        "blend": artifact(blend_path),
        "preview": artifact(preview_path),
        "w3d": artifact(w3d_path),
    }


results = []
clear_scene()
drone_parts, drone_groups = build_drone()
results.append(export_asset(
    {"slug": "drone", "runtime": "drone", "source_parts": ("Chorus", "Drone")},
    drone_parts,
    drone_groups,
    ((steel, "DRBODY", 0.52, 0.28), (magenta, "DRMAG", 0.50, 0.25), (cyan, "DRGLOW", 0.55, 0.30)),
    (0.0, 0.0, 0.85),
    (4.4, 4.4, 1.7),
    5.4,
    0.9,
))

clear_scene()
relay_parts, relay_groups = build_relay()
results.append(export_asset(
    {"slug": "relay", "runtime": "relay", "source_parts": ("Freegrid", "Relay")},
    relay_parts,
    relay_groups,
    ((steel, "RLBODY", 0.52, 0.26), (white, "RLARMOR", 0.50, 0.24), (cyan, "HouseColor", 0.55, 0.28)),
    (0.0, 0.0, 2.15),
    (2.5, 2.5, 4.4),
    5.7,
    2.1,
))

result = {
    "blender_version": ".".join(str(value) for value in bpy.app.version),
    "plugin_version": getattr(io_mesh_w3d, "bl_info", {}).get("version"),
    "texture": artifact(os.path.join(texture_root, "ptmagnta.tga")),
    "assets": results,
}
result_path = os.path.join(evidence_root, "result.json")
with open(result_path, "w", encoding="utf-8") as handle:
    json.dump(result, handle, indent=2, sort_keys=True)

print("TEMPEST_SUBSTATION_KIT_RESULT " + json.dumps(result, sort_keys=True))
