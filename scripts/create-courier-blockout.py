"""Create Project Tempest's first original Courier blockout and W3D export in Blender."""

import hashlib
import json
import math
import os
import sys

import bpy
from mathutils import Vector


def fail(message: str) -> None:
    print(f"TEMPEST_COURIER_ERROR {message}")
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

source_root = os.path.join(
    output_root, "ProjectTempest", "SourceAssets", "Models", "Freegrid", "Courier"
)
runtime_root = os.path.join(output_root, "ProjectTempest", "Content", "Art", "W3D")
texture_root = os.path.join(output_root, "ProjectTempest", "Content", "Art", "Textures")
evidence_root = os.path.join(output_root, "build", "courier-blockout")
os.makedirs(source_root, exist_ok=True)
os.makedirs(runtime_root, exist_ok=True)
os.makedirs(texture_root, exist_ok=True)
os.makedirs(evidence_root, exist_ok=True)

blend_path = os.path.join(source_root, "courier-master-v1.blend")
damaged_blend_path = os.path.join(source_root, "courier-damaged-v1.blend")
preview_path = os.path.join(source_root, "courier-blockout-v1.png")
top_preview_path = os.path.join(source_root, "courier-top-v1.png")
damaged_preview_path = os.path.join(source_root, "courier-damaged-v1.png")
w3d_path = os.path.join(runtime_root, "courier.w3d")
damaged_w3d_path = os.path.join(runtime_root, "courierd.w3d")
result_path = os.path.join(evidence_root, "result.json")

def clear_scene_geometry():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for mesh in list(bpy.data.meshes):
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    for collection in list(bpy.data.collections):
        if collection.users == 0 or collection.name != bpy.context.scene.collection.name:
            bpy.data.collections.remove(collection)


clear_scene_geometry()


texture_artifacts = []


def create_texture(filename, base_color, accent_color, pattern):
    size = 128
    image = bpy.data.images.new(filename, width=size, height=size, alpha=True)
    pixels = [0.0] * (size * size * 4)
    for y in range(size):
        for x in range(size):
            noise = ((x * 17 + y * 31 + x * y * 7) % 97) / 96.0
            seam = (x % 32 <= 1) or (y % 32 <= 1)
            diagonal = ((x + (2 * y)) % 53) <= 1
            if pattern == "rubber":
                use_accent = ((x // 8) + (y // 8)) % 2 == 0
                factor = 0.72 + (0.18 * noise)
            elif pattern == "cable":
                use_accent = (x % 24) <= 3
                factor = 0.78 + (0.18 * noise)
            elif pattern == "emissive":
                use_accent = seam
                factor = 0.90 + (0.10 * noise)
            elif pattern == "burn":
                use_accent = seam or diagonal or ((x * 5 + y * 3) % 41 < 5)
                factor = 0.55 + (0.30 * noise)
            else:
                use_accent = seam or diagonal
                factor = 0.72 + (0.24 * noise)
            color = accent_color if use_accent else base_color
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
    texture_artifacts.append(image.filepath_raw)
    return image


def material(name, color, metallic=0.0, roughness=0.55, emission=None, texture=None):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1.0)
    mat.material_type = "VERTEX_MATERIAL"
    mat.ambient = (0.30, 0.30, 0.30, 0.0)
    mat.specular = (0.18, 0.18, 0.18) if metallic > 0.1 else (0.04, 0.04, 0.04)
    mat.surface_type = "1" if metallic > 0.5 else "13"
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get("Principled BSDF")
    if shader:
        shader.inputs["Base Color"].default_value = (*color, 1.0)
        shader.inputs["Metallic"].default_value = metallic
        shader.inputs["Roughness"].default_value = roughness
        if emission:
            shader.inputs["Emission Color"].default_value = (*emission, 1.0)
            shader.inputs["Emission Strength"].default_value = 3.0
        if texture is not None:
            texture_node = mat.node_tree.nodes.new("ShaderNodeTexImage")
            texture_node.image = texture
            texture_node.location = (-360, 260)
            mat.node_tree.links.new(texture_node.outputs["Color"], shader.inputs["Base Color"])
    return mat


steel_texture = create_texture("ptsteel.tga", (0.12, 0.15, 0.17), (0.35, 0.18, 0.07), "metal")
white_texture = create_texture("ptwhite.tga", (0.72, 0.68, 0.55), (0.20, 0.16, 0.10), "metal")
rubber_texture = create_texture("ptrubber.tga", (0.05, 0.06, 0.065), (0.11, 0.12, 0.12), "rubber")
cyan_texture = create_texture("ptcyan.tga", (0.02, 0.52, 0.62), (0.25, 0.95, 1.0), "emissive")
cable_texture = create_texture("ptcable.tga", (0.38, 0.03, 0.02), (0.82, 0.22, 0.04), "cable")
burn_texture = create_texture("ptburn.tga", (0.10, 0.075, 0.055), (0.015, 0.012, 0.010), "burn")
off_texture = create_texture("ptoff.tga", (0.025, 0.035, 0.04), (0.11, 0.06, 0.025), "burn")

dark_steel = material(
    "PT_DARK_STEEL", (0.72, 0.72, 0.72), metallic=0.75, roughness=0.38, texture=steel_texture
)
off_white = material(
    "PT_WORN_WHITE", (0.82, 0.82, 0.82), metallic=0.35, roughness=0.62, texture=white_texture
)
amber = material("PT_TEAM_AMBER", (0.74, 0.30, 0.025), metallic=0.35, roughness=0.48)
rubber = material(
    "PT_RUBBER", (0.62, 0.62, 0.62), metallic=0.0, roughness=0.9, texture=rubber_texture
)
cyan = material(
    "PT_STATUS_CYAN",
    (0.75, 0.75, 0.75),
    metallic=0.1,
    roughness=0.25,
    emission=(0.0, 0.65, 0.8),
    texture=cyan_texture,
)
cable_red = material(
    "PT_CABLE_RED", (0.75, 0.75, 0.75), metallic=0.15, roughness=0.58, texture=cable_texture
)
burn = material("PT_BURN", (0.68, 0.68, 0.68), metallic=0.1, roughness=0.9, texture=burn_texture)
cyan_off = material("PT_STATUS_OFF", (0.65, 0.65, 0.65), roughness=0.8, texture=off_texture)

vehicle_objects = []
material_objects = {}


def remember(obj, mat=None):
    if mat:
        obj.data.materials.append(mat)
        material_objects.setdefault(mat.name, []).append(obj)
    vehicle_objects.append(obj)
    return obj


def box(name, location, dimensions, mat, bevel=0.06):
    bpy.ops.mesh.primitive_cube_add(location=location)
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
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=location, rotation=rotation)
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


# Main chassis and deliberately asymmetric repaired bodywork.
box("CHASSIS", (0.0, 0.0, 0.62), (3.45, 1.42, 0.34), dark_steel, 0.10)
box("HOOD", (-0.72, 0.0, 0.94), (1.88, 1.30, 0.52), off_white, 0.12)
box("DECK", (0.92, 0.0, 0.91), (1.25, 1.28, 0.30), dark_steel, 0.08)
box("TOP_PANEL", (-0.72, 0.0, 1.23), (1.05, 0.88, 0.08), dark_steel, 0.025)
box("PATCH_L", (-0.58, -0.67, 0.91), (0.68, 0.07, 0.42), amber, 0.025)
box("PATCH_R", (0.28, 0.68, 0.82), (0.52, 0.07, 0.34), off_white, 0.025)

# Wheels, hubs, and simple suspension guards. Vehicle longitudinal axis is X; wheel axles run along Y.
wheel_rotation = (math.radians(90.0), 0.0, 0.0)
for x_label, x in (("F", -1.28), ("R", 1.23)):
    for side_label, y in (("L", -0.82), ("R", 0.82)):
        cylinder(f"WH{x_label}{side_label}", (x, y, 0.48), 0.48, 0.30, rubber, wheel_rotation, vertices=20)
        cylinder(f"HB{x_label}{side_label}", (x, y * 1.01, 0.48), 0.23, 0.315, amber, wheel_rotation, vertices=16)
        box(f"GD{x_label}{side_label}", (x, y * 0.82, 0.82), (0.72, 0.11, 0.12), dark_steel, 0.04)

# Rear battery bank and high-voltage ceramic insulators.
for index, y in enumerate((-0.36, -0.12, 0.12, 0.36)):
    box(f"BAT{index}", (0.94, y, 1.17), (0.44, 0.19, 0.22), amber, 0.025)
for side, y in (("L", -0.52), ("R", 0.52)):
    for level in range(3):
        cylinder(f"IN{side}{level}", (0.54, y, 1.08 + level * 0.09), 0.095 - level * 0.012, 0.055, off_white, vertices=12)

# Folding sensor mast, camera head, antenna, and communications box.
pipe("MAST", (0.72, 0.34, 1.13), (0.72, 0.34, 2.05), 0.055, dark_steel)
box("SENSOR", (0.72, 0.34, 2.12), (0.34, 0.26, 0.24), off_white, 0.035)
box("LENS", (0.54, 0.34, 2.12), (0.025, 0.12, 0.10), cyan, 0.01)
pipe("ANTENNA", (1.33, -0.35, 1.08), (1.33, -0.35, 1.92), 0.018, dark_steel, vertices=8)
box("COMMS", (1.30, 0.32, 1.18), (0.33, 0.32, 0.30), dark_steel, 0.035)

# Front protection, towing hardware, status lamps, and side step.
pipe("BUMPER", (-1.78, -0.62, 0.55), (-1.78, 0.62, 0.55), 0.055, dark_steel)
pipe("BUMPER_L", (-1.78, -0.62, 0.55), (-1.62, -0.62, 0.72), 0.045, dark_steel)
pipe("BUMPER_R", (-1.78, 0.62, 0.55), (-1.62, 0.62, 0.72), 0.045, dark_steel)
box("LAMP_L", (-1.73, -0.43, 0.82), (0.045, 0.28, 0.075), cyan, 0.012)
box("LAMP_R", (-1.73, 0.43, 0.82), (0.045, 0.28, 0.075), cyan, 0.012)
box("STEP", (0.05, -0.83, 0.52), (1.15, 0.18, 0.10), dark_steel, 0.025)
cylinder("TOW", (-1.84, 0.0, 0.47), 0.13, 0.09, amber, wheel_rotation, vertices=12)

# Exposed, clamped power cable rendered as deterministic straight segments.
cable_points = [(-1.0, -0.70, 1.05), (-0.25, -0.72, 1.22), (0.65, -0.70, 1.30), (1.35, -0.61, 1.18)]
for index in range(len(cable_points) - 1):
    pipe(f"CBL{index}", cable_points[index], cable_points[index + 1], 0.035, cable_red, vertices=8)

# Roof/deck rails reinforce the top-down RTS silhouette.
for y in (-0.56, 0.56):
    pipe("RAIL", (0.45, y, 1.34), (1.50, y, 1.34), 0.035, dark_steel)
pipe("RAIL_F", (0.45, -0.56, 1.34), (0.45, 0.56, 1.34), 0.035, dark_steel)
pipe("RAIL_R", (1.50, -0.56, 1.34), (1.50, 0.56, 1.34), 0.035, dark_steel)

# Preview-only ground and camera.
bpy.ops.mesh.primitive_plane_add(size=30.0, location=(0.0, 0.0, 0.0))
ground = bpy.context.active_object
ground.name = "PT_PREVIEW_GROUND"
ground.data.materials.append(material("PT_PREVIEW", (0.025, 0.03, 0.035), roughness=0.95))

bpy.ops.object.camera_add(location=(-5.4, -6.2, 3.9))
camera = bpy.context.active_object
camera.name = "PT_PREVIEW_CAMERA"
camera.data.lens = 58.0
target = Vector((0.0, 0.0, 0.85))
camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
hero_location = camera.location.copy()
hero_rotation = camera.rotation_euler.copy()
bpy.context.scene.camera = camera

scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "TEXTURE"
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.display.shading.cavity_type = "WORLD"
scene.display.shading.show_specular_highlight = True
scene.render.resolution_x = 1280
scene.render.resolution_y = 720
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = preview_path
scene.render.film_transparent = False
scene.world.color = (0.018, 0.022, 0.026)
bpy.ops.render.render(write_still=True)

# A near-orthographic RTS view verifies silhouette and faction-color readability rather than cinematic presentation.
camera.data.type = "ORTHO"
camera.data.ortho_scale = 5.2
camera.location = (0.0, 0.0, 8.0)
camera.rotation_euler = (0.0, 0.0, 0.0)
scene.render.filepath = top_preview_path
bpy.ops.render.render(write_still=True)
camera.data.type = "PERSP"
camera.data.lens = 58.0
camera.location = hero_location
camera.rotation_euler = hero_rotation
scene.render.filepath = preview_path

bpy.data.objects.remove(ground, do_unlink=True)
bpy.data.objects.remove(camera, do_unlink=True)
source_part_count = len(vehicle_objects)


def join_objects(objects, object_name):
    bpy.ops.object.select_all(action="DESELECT")
    existing = [obj for obj in objects if obj.name in bpy.data.objects]
    if not existing:
        fail(f"No source objects available for {object_name}")
    for obj in existing:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = existing[0]
    bpy.ops.object.join()
    joined = bpy.context.active_object
    joined.name = object_name
    joined.data.name = f"{object_name}_M"
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return joined


mesh_specs = [
    (dark_steel, "CRBODY", 0.35),
    (off_white, "CRARMOR", 0.35),
    (rubber, "CRTREAD", 0.30),
    (cyan, "CRGLOW", 0.45),
    (cable_red, "CRCABLE", 0.40),
    # Generals recolours vertex materials only when the render-mesh name begins
    # HOUSECOLOR. Shader-material recolour metadata is not consumed by its loader.
    (amber, "HouseColor", 0.50),
]
lod0_objects = [
    join_objects(material_objects[mat.name], f"{prefix}0")
    for mat, prefix, _ratio in mesh_specs
]

# The HLOD exporter treats root-scene meshes as the highest-detail level and each
# child collection as a successively cheaper level. Keep the collision box at the
# root so it is attached to the authoritative LOD without becoming visible art.
collision_object = box(
    "BOUNDINGBOX",
    (-0.08, 0.0, 1.08),
    (3.80, 1.92, 2.16),
    dark_steel,
    bevel=0.0,
)
collision_object.data.object_type = "BOX"
collision_object.data.box_type = "1"
expected_collision_flags = {"PHYSICAL", "PROJECTILE", "VIS", "VEHICLE"}
collision_object.data.box_collision_types = expected_collision_flags
collision_object.display_type = "WIRE"
collision_object.hide_render = True

lod1_collection = bpy.data.collections.new("LOD1")
scene.collection.children.link(lod1_collection)


def create_lod1(source, object_name, ratio):
    lod = source.copy()
    lod.data = source.data.copy()
    lod.name = object_name
    lod.data.name = f"{object_name}_M"
    lod1_collection.objects.link(lod)
    decimate = lod.modifiers.new("PT_LOD1_DECIMATE", "DECIMATE")
    decimate.ratio = ratio
    decimate.use_collapse_triangulate = True
    bpy.context.view_layer.objects.active = lod
    lod.select_set(True)
    bpy.ops.object.modifier_apply(modifier=decimate.name)
    lod.select_set(False)
    return lod


lod1_objects = [
    create_lod1(lod0, f"{prefix}1", ratio)
    for lod0, (_mat, prefix, ratio) in zip(lod0_objects, mesh_specs, strict=True)
]


def quantize_uvs(objects, digits=5):
    # Blender's primitive/decimate UV calculations can vary by one float LSB
    # between clean processes. W3D stores float32 UVs directly, so quantise the
    # authored source before export to keep runtime containers byte-stable.
    for obj in objects:
        for uv_layer in obj.data.uv_layers:
            for uv_loop in uv_layer.data:
                uv_loop.uv = (
                    round(float(uv_loop.uv.x), digits),
                    round(float(uv_loop.uv.y), digits),
                )


quantize_uvs(lod0_objects + lod1_objects)

runtime_identifiers = [
    *(obj.name for obj in lod0_objects),
    *(obj.name for obj in lod1_objects),
    collision_object.name,
]
if any(len(identifier) > 16 for identifier in runtime_identifiers):
    fail(f"W3D runtime identifier exceeds 16 characters: {runtime_identifiers}")

# The generated blend is the editable export master; the procedural Python file
# remains the non-destructive source for regenerating its constituent parts.
bpy.ops.wm.save_as_mainfile(filepath=blend_path)

lod0_vertex_count = sum(len(obj.data.vertices) for obj in lod0_objects)
lod1_vertex_count = sum(len(obj.data.vertices) for obj in lod1_objects)
if lod1_vertex_count <= 0 or lod1_vertex_count >= lod0_vertex_count:
    fail(
        f"LOD1 simplification failed: lod0={lod0_vertex_count}, "
        f"lod1={lod1_vertex_count}"
    )

export_result = bpy.ops.export_mesh.westwood_w3d(filepath=w3d_path, export_mode="HM")
if export_result != {"FINISHED"} or not os.path.isfile(w3d_path):
    fail(f"W3D export failed: {export_result}")

# Produce a separate, deterministic damage-state HLOD. The production game data
# will switch to this model at the REALLYDAMAGED threshold; keeping the state in
# its own W3D matches the engine's model-condition path and avoids runtime mesh edits.
for glow_name in ("CRGLOW0", "CRGLOW1"):
    glow_object = bpy.data.objects.get(glow_name)
    if glow_object is None or len(glow_object.data.materials) != 1:
        fail(f"Damage-state source is missing {glow_name}")
    glow_object.data.materials[0] = cyan_off

bpy.ops.mesh.primitive_cube_add(location=(-0.52, 0.08, 1.285), rotation=(0.0, 0.0, math.radians(8.0)))
damage_lod0 = bpy.context.active_object
damage_lod0.name = "CRDMG0"
damage_lod0.dimensions = (0.92, 0.74, 0.055)
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
damage_lod0.data.materials.append(burn)
bevel = damage_lod0.modifiers.new("PT_DAMAGE_BEVEL", "BEVEL")
bevel.width = 0.035
bevel.segments = 1
bpy.context.view_layer.objects.active = damage_lod0
bpy.ops.object.modifier_apply(modifier=bevel.name)

damage_lod1 = damage_lod0.copy()
damage_lod1.data = damage_lod0.data.copy()
damage_lod1.name = "CRDMG1"
lod1_collection.objects.link(damage_lod1)
quantize_uvs([damage_lod0, damage_lod1])

# Knock the damaged sensor housing sideways while retaining the same material
# and topology contract. This is deliberately readable from the RTS camera.
for armor_name in ("CRARMOR0", "CRARMOR1"):
    armor_object = bpy.data.objects.get(armor_name)
    if armor_object is None:
        fail(f"Damage-state source is missing {armor_name}")
    for vertex in armor_object.data.vertices:
        world_z = (armor_object.matrix_world @ vertex.co).z
        if world_z > 1.55:
            vertex.co.y -= 0.20
            vertex.co.z -= 0.22

bpy.ops.wm.save_as_mainfile(filepath=damaged_blend_path)

# Headless/offscreen damage-state evidence. No interactive Blender UI is used.
bpy.ops.mesh.primitive_plane_add(size=30.0, location=(0.0, 0.0, 0.0))
damage_ground = bpy.context.active_object
damage_ground.name = "PT_DAMAGE_GROUND"
damage_ground.data.materials.append(material("PT_DAMAGE_PREVIEW", (0.025, 0.03, 0.035), roughness=0.95))
bpy.ops.object.camera_add(location=(-5.4, -6.2, 3.9))
damage_camera = bpy.context.active_object
damage_camera.name = "PT_DAMAGE_CAMERA"
damage_camera.data.lens = 58.0
damage_camera.rotation_euler = (Vector((0.0, 0.0, 0.85)) - damage_camera.location).to_track_quat("-Z", "Y").to_euler()
scene.camera = damage_camera
scene.render.filepath = damaged_preview_path
bpy.ops.render.render(write_still=True)
bpy.data.objects.remove(damage_ground, do_unlink=True)
bpy.data.objects.remove(damage_camera, do_unlink=True)

damaged_export_result = bpy.ops.export_mesh.westwood_w3d(filepath=damaged_w3d_path, export_mode="HM")
if damaged_export_result != {"FINISHED"} or not os.path.isfile(damaged_w3d_path):
    fail(f"Damaged W3D export failed: {damaged_export_result}")

# Re-import the generated runtime file in the same clean process to catch malformed or empty exports immediately.
clear_scene_geometry()
import_result = bpy.ops.import_mesh.westwood_w3d(filepath=w3d_path)
imported_meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
imported_boxes = [obj for obj in imported_meshes if obj.data.object_type == "BOX"]
imported_render_meshes = [obj for obj in imported_meshes if obj.data.object_type != "BOX"]
imported_render_names = sorted(obj.name for obj in imported_render_meshes)
expected_render_names = sorted(
    [f"{prefix}0" for _mat, prefix, _ratio in mesh_specs]
    + [f"{prefix}1" for _mat, prefix, _ratio in mesh_specs]
)
imported_house_color_meshes = sorted(
    obj.name for obj in imported_render_meshes if obj.name.casefold().startswith("housecolor")
)
invalid_material_counts = {
    obj.name: len(obj.data.materials)
    for obj in imported_render_meshes
    if len(obj.data.materials) != 1
}
empty_render_meshes = sorted(
    obj.name for obj in imported_render_meshes if len(obj.data.vertices) == 0
)
imported_collision_flags = (
    set(imported_boxes[0].data.box_collision_types) - {"DEFAULT"}
    if len(imported_boxes) == 1
    else set()
)
imported_vertex_count = sum(len(obj.data.vertices) for obj in imported_meshes)
max_material_passes_per_render_mesh = max(
    len(obj.data.materials) for obj in imported_render_meshes
)
imported_texture_files = sorted(
    {
        os.path.basename(node.image.filepath or node.image.name).lower()
        for obj in imported_render_meshes
        for imported_material in obj.data.materials
        if imported_material is not None and imported_material.use_nodes
        for node in imported_material.node_tree.nodes
        if node.type == "TEX_IMAGE" and node.image is not None
    }
)
expected_texture_files = sorted(
    os.path.basename(path).lower()
    for path in texture_artifacts
    if os.path.basename(path).lower() not in {"ptburn.tga", "ptoff.tga"}
)
imported_lod_collections = [
    collection.name
    for collection in bpy.data.collections
    if any(obj.type == "MESH" and obj.data.object_type != "BOX" for obj in collection.objects)
]
if (
    import_result != {"FINISHED"}
    or imported_render_names != expected_render_names
    or len(imported_boxes) != 1
    or imported_collision_flags != expected_collision_flags
    or imported_house_color_meshes != ["HouseColor0", "HouseColor1"]
    or invalid_material_counts
    or empty_render_meshes
    or imported_texture_files != expected_texture_files
):
    fail(
        f"W3D re-import failed: result={import_result}, meshes={len(imported_meshes)}, "
        f"render_meshes={imported_render_names}, boxes={len(imported_boxes)}, "
        f"collision_flags={sorted(imported_collision_flags)}, "
        f"house_color_meshes={imported_house_color_meshes}, "
        f"invalid_material_counts={invalid_material_counts}, "
        f"empty_render_meshes={empty_render_meshes}, "
        f"textures={imported_texture_files}, expected_textures={expected_texture_files}, "
        f"vertices={imported_vertex_count}"
    )

# Validate the independent damaged HLOD and its explicit burn/power-off texture payload.
clear_scene_geometry()
damaged_import_result = bpy.ops.import_mesh.westwood_w3d(filepath=damaged_w3d_path)
damaged_meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
damaged_boxes = [obj for obj in damaged_meshes if obj.data.object_type == "BOX"]
damaged_render_meshes = [obj for obj in damaged_meshes if obj.data.object_type != "BOX"]
damaged_render_names = sorted(obj.name for obj in damaged_render_meshes)
empty_damaged_render_meshes = sorted(
    obj.name for obj in damaged_render_meshes if len(obj.data.vertices) == 0
)
expected_damaged_render_names = sorted(expected_render_names + ["CRDMG0", "CRDMG1"])
damaged_texture_files = sorted(
    {
        os.path.basename(node.image.filepath or node.image.name).lower()
        for obj in damaged_render_meshes
        for imported_material in obj.data.materials
        if imported_material is not None and imported_material.use_nodes
        for node in imported_material.node_tree.nodes
        if node.type == "TEX_IMAGE" and node.image is not None
    }
)
if (
    damaged_import_result != {"FINISHED"}
    or damaged_render_names != expected_damaged_render_names
    or len(damaged_boxes) != 1
    or empty_damaged_render_meshes
    or "ptburn.tga" not in damaged_texture_files
    or "ptoff.tga" not in damaged_texture_files
):
    fail(
        f"Damaged W3D re-import failed: result={damaged_import_result}, "
        f"render_meshes={damaged_render_names}, boxes={len(damaged_boxes)}, "
        f"empty_render_meshes={empty_damaged_render_meshes}, "
        f"textures={damaged_texture_files}"
    )


def sha256(path):
    with open(path, "rb") as source_file:
        return hashlib.sha256(source_file.read()).hexdigest()


result = {
    "blend": {"path": os.path.relpath(blend_path, output_root), "sha256": sha256(blend_path)},
    "damaged_blend": {
        "path": os.path.relpath(damaged_blend_path, output_root),
        "sha256": sha256(damaged_blend_path),
    },
    "damaged_preview": {
        "path": os.path.relpath(damaged_preview_path, output_root),
        "sha256": sha256(damaged_preview_path),
    },
    "damaged_w3d": {
        "path": os.path.relpath(damaged_w3d_path, output_root),
        "sha256": sha256(damaged_w3d_path),
    },
    "blender_version": bpy.app.version_string,
    "concept_asset_id": "PT-CONCEPT-FG-COURIER-001",
    "export_result": sorted(export_result),
    "import_result": sorted(import_result),
    "imported_box_count": len(imported_boxes),
    "imported_collision_flags": sorted(imported_collision_flags),
    "imported_lod_collections": sorted(imported_lod_collections),
    "imported_mesh_count": len(imported_meshes),
    "imported_render_mesh_count": len(imported_render_meshes),
    "imported_house_color_meshes": imported_house_color_meshes,
    "imported_texture_files": imported_texture_files,
    "damaged_import_result": sorted(damaged_import_result),
    "damaged_render_mesh_count": len(damaged_render_meshes),
    "damaged_texture_files": damaged_texture_files,
    "max_material_passes_per_render_mesh": max_material_passes_per_render_mesh,
    "imported_vertex_count": imported_vertex_count,
    "lod0_vertex_count": lod0_vertex_count,
    "lod1_vertex_count": lod1_vertex_count,
    "source_part_count": source_part_count,
    "textures": [
        {"path": os.path.relpath(path, output_root), "sha256": sha256(path)}
        for path in sorted(texture_artifacts)
    ],
    "plugin_version": ".".join(str(part) for part in io_mesh_w3d.VERSION),
    "preview": {"path": os.path.relpath(preview_path, output_root), "sha256": sha256(preview_path)},
    "top_preview": {
        "path": os.path.relpath(top_preview_path, output_root),
        "sha256": sha256(top_preview_path),
    },
    "w3d": {"path": os.path.relpath(w3d_path, output_root), "sha256": sha256(w3d_path)},
}

with open(result_path, "w", encoding="utf-8") as result_file:
    json.dump(result, result_file, indent=2, sort_keys=True)
    result_file.write("\n")

print("TEMPEST_COURIER_RESULT", json.dumps(result, sort_keys=True))
