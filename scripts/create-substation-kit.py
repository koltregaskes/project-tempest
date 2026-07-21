"""Create original Substation 9 unit and structure W3D assets in headless Blender."""

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


def build_sentry():
    objects, groups, box, cylinder, pipe = make_builder()
    cylinder("STBASE", (0.0, 0.0, 0.22), 1.30, 0.44, steel, vertices=24)
    cylinder("STPLINTH", (0.0, 0.0, 0.56), 0.82, 0.34, white, vertices=20)
    box("STSTEM", (0.0, 0.0, 1.12), (0.66, 0.66, 0.96), steel,
        rotation=(0.0, 0.0, math.radians(45)), bevel=0.10)
    cylinder("STYAW", (0.0, 0.0, 1.58), 0.74, 0.32, steel, vertices=24)
    box("STSHIELD", (0.0, -0.12, 1.78), (1.48, 1.00, 0.50), white, bevel=0.12)
    for side in (-1.0, 1.0):
        x = side * 0.42
        box(f"STRAIL{int(side)}", (x, -1.10, 1.92), (0.24, 2.25, 0.24), steel, bevel=0.055)
        box(f"STCAP{int(side)}", (x, -2.20, 1.92), (0.36, 0.34, 0.36), white, bevel=0.065)
        cylinder(
            f"STARC{int(side)}", (x, -2.39, 1.92), 0.11, 0.20, cyan,
            rotation=(math.radians(90), 0.0, 0.0), vertices=16
        )
    cylinder("STSENSOR", (0.0, -0.48, 2.18), 0.24, 0.18, cyan,
        rotation=(math.radians(90), 0.0, 0.0), vertices=20)
    for index, angle in enumerate((45.0, 135.0, 225.0, 315.0)):
        radians = math.radians(angle)
        start = (math.cos(radians) * 0.62, math.sin(radians) * 0.62, 0.38)
        end = (math.cos(radians) * 1.28, math.sin(radians) * 1.28, 0.10)
        pipe(f"STBRACE{index}", start, end, 0.075, steel, vertices=10)
        box(
            f"STFOOT{index}", end, (0.48, 0.30, 0.18), white,
            rotation=(0.0, 0.0, radians), bevel=0.035
        )
    return objects, groups


def build_pylon():
    objects, groups, box, cylinder, pipe = make_builder()
    cylinder("PYBASE", (0.0, 0.0, 0.20), 1.42, 0.40, steel, vertices=24)
    cylinder("PYCORE", (0.0, 0.0, 0.66), 0.78, 0.64, magenta, vertices=20)
    box("PYHEART", (0.0, 0.0, 1.52), (0.74, 0.74, 1.35), steel,
        rotation=(0.0, 0.0, math.radians(45)), bevel=0.10)
    for level, z in enumerate((1.08, 1.52, 1.96, 2.40)):
        cylinder(f"PYRING{level}", (0.0, 0.0, z), 0.56 - (level * 0.055), 0.12, magenta, vertices=20)
    for index, angle in enumerate((0.0, 120.0, 240.0)):
        radians = math.radians(angle)
        foot = (math.cos(radians) * 1.52, math.sin(radians) * 1.52, 0.10)
        shoulder = (math.cos(radians) * 0.32, math.sin(radians) * 0.32, 2.28)
        pipe(f"PYLEG{index}", foot, shoulder, 0.115, steel, vertices=12)
        box(
            f"PYFIN{index}",
            (math.cos(radians) * 1.18, math.sin(radians) * 1.18, 1.02),
            (0.24, 0.78, 1.18),
            magenta,
            rotation=(math.radians(-10), 0.0, radians),
            bevel=0.055,
        )
        emitter = (math.cos(radians) * 0.92, math.sin(radians) * 0.92, 2.72)
        pipe(f"PYARM{index}", shoulder, emitter, 0.075, steel, vertices=10)
        cylinder(f"PYGLOW{index}", emitter, 0.18, 0.22, cyan, vertices=16)
    cylinder("PYCROWN", (0.0, 0.0, 2.78), 0.48, 0.24, magenta, vertices=20)
    pipe("PYSPIRE", (0.0, 0.0, 2.80), (0.0, 0.0, 4.30), 0.065, cyan, vertices=12)
    box("PYBEACON", (0.0, 0.0, 4.38), (0.32, 0.32, 0.32), magenta,
        rotation=(math.radians(35), 0.0, math.radians(45)), bevel=0.045)
    return objects, groups


def build_fabricator_rig():
    """Author the Freegrid repair/construction rig as an asymmetric utility crawler."""
    objects, groups, box, cylinder, pipe = make_builder()
    box("FRDECK", (0.0, 0.0, 0.58), (3.15, 2.15, 0.34), steel, bevel=0.10)
    box("FRCAB", (-0.78, -0.12, 1.10), (1.32, 1.62, 0.92), white,
        rotation=(0.0, 0.0, math.radians(-8)), bevel=0.13)
    box("FRBED", (0.78, 0.08, 0.91), (1.22, 1.72, 0.42), steel, bevel=0.08)
    for side in (-1.0, 1.0):
        y = side * 1.02
        box(f"FRTRACK{int(side)}", (0.0, y, 0.42), (3.45, 0.38, 0.48), steel, bevel=0.09)
        cylinder(f"FRWHEEL{int(side)}", (-0.92, y, 0.42), 0.32, 0.42, white,
            rotation=(math.radians(90), 0.0, 0.0), vertices=16)
    pipe("FRBOOM", (0.72, 0.28, 1.18), (1.86, -0.52, 2.03), 0.11, steel, vertices=10)
    pipe("FRTOOL", (1.86, -0.52, 2.03), (2.33, -0.72, 1.36), 0.09, steel, vertices=10)
    cylinder("FRARC", (2.36, -0.74, 1.28), 0.20, 0.22, cyan, vertices=16)
    box("FRSIGN", (-0.82, -0.96, 1.16), (0.82, 0.14, 0.34), cyan, bevel=0.035)
    return objects, groups


def build_lancer():
    """Author the fast Freegrid anti-armour team as a narrow twin-rail skiff."""
    objects, groups, box, cylinder, pipe = make_builder()
    box("LNKEEL", (0.0, 0.0, 0.55), (3.70, 1.48, 0.38), steel, bevel=0.11)
    box("LNCAB", (-0.78, 0.0, 0.96), (1.45, 1.22, 0.68), white, bevel=0.13)
    for side in (-1.0, 1.0):
        y = side * 0.62
        box(f"LNRAIL{int(side)}", (0.72, y, 1.16), (2.42, 0.18, 0.18), steel, bevel=0.035)
        cylinder(f"LNMUZZ{int(side)}", (1.95, y, 1.16), 0.13, 0.22, cyan,
            rotation=(0.0, math.radians(90), 0.0), vertices=14)
        box(f"LNFIN{int(side)}", (-1.12, y * 1.28, 0.73), (0.76, 0.18, 0.62), white,
            rotation=(math.radians(-8), 0.0, 0.0), bevel=0.04)
    cylinder("LNSENSOR", (-0.52, -0.58, 1.34), 0.18, 0.16, cyan,
        rotation=(math.radians(90), 0.0, 0.0), vertices=16)
    box("LNTEAM", (-0.10, 0.0, 0.78), (0.72, 1.54, 0.14), cyan, bevel=0.025)
    return objects, groups


def build_coil_carrier():
    """Author the heavy Freegrid Coil Carrier as a broad capacitor hauler."""
    objects, groups, box, cylinder, pipe = make_builder()
    box("CCCHASSIS", (0.0, 0.0, 0.62), (4.55, 2.75, 0.52), steel, bevel=0.14)
    box("CCCAB", (-1.38, -0.12, 1.28), (1.45, 2.15, 1.18), white, bevel=0.16)
    for side in (-1.0, 1.0):
        y = side * 1.28
        box(f"CCTRACK{int(side)}", (0.0, y, 0.48), (4.75, 0.42, 0.58), steel, bevel=0.10)
    for index, x in enumerate((-0.72, 0.34, 1.40)):
        cylinder(f"CCCOIL{index}", (x, 0.0, 1.40), 0.52, 1.72, cyan,
            rotation=(math.radians(90), 0.0, 0.0), vertices=20)
        cylinder(f"CCCAP{index}", (x, -0.92, 1.40), 0.34, 0.14, white,
            rotation=(math.radians(90), 0.0, 0.0), vertices=16)
    pipe("CCBUS0", (-0.72, 0.88, 1.40), (1.40, 0.88, 1.40), 0.08, steel, vertices=10)
    box("CCTEAM", (-1.42, -1.14, 1.35), (0.82, 0.14, 0.42), cyan, bevel=0.035)
    return objects, groups


def build_warden():
    """Author the Chorus Warden as a compact four-legged interception machine."""
    objects, groups, box, cylinder, pipe = make_builder()
    box("WDCORE", (0.0, 0.0, 1.18), (1.72, 1.48, 0.72), steel,
        rotation=(0.0, 0.0, math.radians(45)), bevel=0.13)
    cylinder("WDEYE", (0.0, -0.83, 1.22), 0.25, 0.16, magenta,
        rotation=(math.radians(90), 0.0, 0.0), vertices=18)
    for index, angle in enumerate((45.0, 135.0, 225.0, 315.0)):
        radians = math.radians(angle)
        hip = (math.cos(radians) * 0.62, math.sin(radians) * 0.62, 1.02)
        knee = (math.cos(radians + 0.18) * 1.24, math.sin(radians + 0.18) * 1.24, 0.58)
        foot = (math.cos(radians + 0.30) * 1.64, math.sin(radians + 0.30) * 1.64, 0.16)
        pipe(f"WDLEG{index}", hip, knee, 0.12, steel, vertices=10)
        pipe(f"WDSHIN{index}", knee, foot, 0.10, magenta, vertices=10)
        box(f"WDFOOT{index}", foot, (0.48, 0.24, 0.18), steel,
            rotation=(0.0, 0.0, radians + 0.30), bevel=0.035)
    cylinder("WDCROWN", (0.0, 0.0, 1.68), 0.38, 0.22, cyan, vertices=18)
    return objects, groups


def build_harrower():
    """Author the Chorus Harrower as a large three-fold siege organism-machine."""
    objects, groups, box, cylinder, pipe = make_builder()
    cylinder("HACORE", (0.0, 0.0, 1.62), 1.10, 1.10, steel, vertices=24)
    cylinder("HAHEART", (0.0, 0.0, 1.78), 0.62, 1.32, magenta, vertices=22)
    for index, angle in enumerate((0.0, 120.0, 240.0)):
        radians = math.radians(angle)
        shoulder = (math.cos(radians) * 0.72, math.sin(radians) * 0.72, 1.72)
        knee = (math.cos(radians + 0.22) * 1.82, math.sin(radians + 0.22) * 1.82, 0.90)
        foot = (math.cos(radians + 0.38) * 2.55, math.sin(radians + 0.38) * 2.55, 0.18)
        pipe(f"HAARM{index}", shoulder, knee, 0.18, steel, vertices=12)
        pipe(f"HALEG{index}", knee, foot, 0.15, magenta, vertices=12)
        box(f"HABLADE{index}", foot, (0.88, 0.24, 0.28), steel,
            rotation=(0.0, 0.0, radians + 0.38), bevel=0.045)
        emitter = (math.cos(radians) * 1.30, math.sin(radians) * 1.30, 2.28)
        cylinder(f"HANODE{index}", emitter, 0.24, 0.24, cyan, vertices=16)
    pipe("HASPIRE", (0.0, 0.0, 2.18), (0.0, 0.0, 3.58), 0.10, cyan, vertices=12)
    box("HAAPEX", (0.0, 0.0, 3.72), (0.42, 0.42, 0.42), magenta,
        rotation=(math.radians(35), math.radians(35), math.radians(45)), bevel=0.05)
    return objects, groups


def build_machine_nest():
    """Author the Chorus production anchor as a low radial hatch and wave emitter."""
    objects, groups, box, cylinder, pipe = make_builder()
    cylinder("MNBASE", (0.0, 0.0, 0.28), 2.85, 0.56, steel, vertices=30)
    cylinder("MNHATCH", (0.0, 0.0, 0.78), 1.65, 0.62, magenta, vertices=24)
    for index, angle in enumerate((0.0, 60.0, 120.0, 180.0, 240.0, 300.0)):
        radians = math.radians(angle)
        inner = (math.cos(radians) * 1.15, math.sin(radians) * 1.15, 0.72)
        outer = (math.cos(radians + 0.16) * 2.72, math.sin(radians + 0.16) * 2.72, 0.22)
        pipe(f"MNRIB{index}", inner, outer, 0.15, steel, vertices=12)
        box(f"MNCLAW{index}", outer, (0.78, 0.32, 0.26), magenta,
            rotation=(0.0, 0.0, radians + 0.16), bevel=0.05)
        node = (math.cos(radians) * 1.82, math.sin(radians) * 1.82, 1.06)
        cylinder(f"MNNODE{index}", node, 0.20, 0.20, cyan, vertices=16)
    cylinder("MNCROWN", (0.0, 0.0, 1.38), 0.72, 0.28, steel, vertices=24)
    pipe("MNBEAM", (0.0, 0.0, 1.34), (0.0, 0.0, 2.72), 0.09, cyan, vertices=12)
    return objects, groups


def build_relay_core():
    """Author the low, asymmetric Freegrid command anchor for the player start."""
    objects, groups, box, cylinder, pipe = make_builder()
    cylinder("RCBASE", (0.0, 0.0, 0.24), 2.15, 0.48, steel, vertices=28)
    box("RCDECK", (0.15, 0.0, 0.62), (4.70, 3.45, 0.42), steel, bevel=0.14)
    box("RCCAB", (-0.55, 0.18, 1.38), (2.40, 2.35, 1.42), white,
        rotation=(0.0, 0.0, math.radians(-8)), bevel=0.16)
    box("RCSERVICE", (1.55, 0.55, 1.18), (1.30, 1.60, 1.05), steel,
        rotation=(0.0, 0.0, math.radians(10)), bevel=0.12)
    box("RCSHIELD", (-0.62, -1.18, 1.45), (2.15, 0.22, 0.92), white,
        rotation=(math.radians(7), 0.0, math.radians(-8)), bevel=0.06)
    for side in (-1.0, 1.0):
        y = side * 1.50
        box(f"RCRAIL{int(side)}", (0.10, y, 0.92), (3.80, 0.18, 0.32), steel, bevel=0.04)
        box(f"RCFOOT{int(side)}", (-1.82, y, 0.31), (0.62, 0.74, 0.28), white, bevel=0.05)
    box("RCMAST", (0.95, -0.18, 2.28), (0.46, 0.46, 2.70), steel,
        rotation=(0.0, 0.0, math.radians(45)), bevel=0.07)
    for index, z in enumerate((1.45, 2.08, 2.70, 3.30)):
        cylinder(f"RCCOIL{index}", (0.95, -0.18, z), 0.40 - index * 0.035, 0.11, cyan, vertices=18)
    pipe("RCCABLE0", (-1.55, 0.86, 0.72), (1.05, 1.22, 1.03), 0.075, cyan, vertices=10)
    pipe("RCCABLE1", (-1.35, -0.82, 0.70), (1.50, -1.05, 1.05), 0.075, cyan, vertices=10)
    for index, angle in enumerate((-30.0, 75.0, 165.0)):
        radians = math.radians(angle)
        start = (0.95, -0.18, 3.18)
        end = (0.95 + math.cos(radians) * 1.05, -0.18 + math.sin(radians) * 1.05, 3.82)
        pipe(f"RCARM{index}", start, end, 0.065, steel, vertices=10)
        cylinder(f"RCLAMP{index}", end, 0.15, 0.20, cyan, vertices=14)
    return objects, groups


def build_fabricator_bay():
    """Author an open, visibly asymmetric Freegrid workshop and vehicle gantry."""
    objects, groups, box, cylinder, pipe = make_builder()
    box("FBPAD", (0.0, 0.0, 0.20), (6.60, 5.20, 0.40), steel, bevel=0.12)
    box("FBSHOP", (-1.70, 0.60, 1.52), (2.55, 3.35, 2.55), white,
        rotation=(0.0, 0.0, math.radians(-5)), bevel=0.18)
    box("FBBACK", (1.35, 1.80, 1.55), (3.05, 0.52, 2.75), steel, bevel=0.10)
    for index, x in enumerate((-2.55, 0.25, 2.55)):
        box(f"FBPOST{index}", (x, -1.75, 2.05), (0.38, 0.38, 3.70), steel, bevel=0.07)
    box("FBGANTRY", (0.0, -1.75, 3.72), (5.55, 0.50, 0.46), steel, bevel=0.08)
    box("FBCRANE", (1.35, -1.75, 3.25), (0.58, 0.72, 0.72), white, bevel=0.10)
    pipe("FBHOOK", (1.35, -1.75, 2.95), (1.35, -1.75, 1.28), 0.055, cyan, vertices=10)
    box("FBBED", (0.75, -0.15, 0.58), (3.70, 2.20, 0.34), white, bevel=0.07)
    for side in (-1.0, 1.0):
        y = side * 2.38
        box(f"FBRAIL{int(side)}", (0.0, y, 0.58), (5.90, 0.22, 0.46), steel, bevel=0.04)
        cylinder(f"FBLAMP{int(side)}", (2.58, y, 1.08), 0.18, 0.24, cyan, vertices=14)
    box("FBSIGN", (-1.75, -1.18, 2.25), (1.36, 0.16, 0.58), cyan,
        rotation=(math.radians(4), 0.0, math.radians(-5)), bevel=0.04)
    pipe("FBCABLE0", (-2.75, 1.70, 0.62), (0.20, 1.94, 1.26), 0.08, cyan, vertices=10)
    pipe("FBCABLE1", (-2.62, -0.92, 0.64), (2.28, -0.88, 0.86), 0.08, cyan, vertices=10)
    return objects, groups


def build_chorus_spire():
    """Author the tall five-fold Chorus victory target and map-scale silhouette."""
    objects, groups, box, cylinder, pipe = make_builder()
    cylinder("CSBASE", (0.0, 0.0, 0.28), 2.45, 0.56, steel, vertices=30)
    cylinder("CSWELL", (0.0, 0.0, 0.78), 1.45, 0.72, magenta, vertices=25)
    box("CSHEART", (0.0, 0.0, 2.15), (1.18, 1.18, 2.55), steel,
        rotation=(0.0, 0.0, math.radians(45)), bevel=0.16)
    for level, z in enumerate((1.20, 1.82, 2.50, 3.18, 3.90, 4.62)):
        cylinder(f"CSRING{level}", (0.0, 0.0, z), 1.08 - level * 0.09, 0.14, magenta, vertices=25)
    for index, angle in enumerate((0.0, 72.0, 144.0, 216.0, 288.0)):
        radians = math.radians(angle)
        foot = (math.cos(radians) * 2.70, math.sin(radians) * 2.70, 0.15)
        knee = (math.cos(radians + 0.22) * 1.55, math.sin(radians + 0.22) * 1.55, 2.35)
        crown = (math.cos(radians + 0.45) * 0.70, math.sin(radians + 0.45) * 0.70, 5.35)
        pipe(f"CSLEG{index}", foot, knee, 0.16, steel, vertices=12)
        pipe(f"CSARM{index}", knee, crown, 0.12, steel, vertices=12)
        box(f"CSFIN{index}", (knee[0], knee[1], 2.65), (0.30, 1.00, 1.55), magenta,
            rotation=(math.radians(-12), 0.0, radians + 0.22), bevel=0.07)
        cylinder(f"CSNODE{index}", crown, 0.22, 0.28, cyan, vertices=16)
    cylinder("CSCROWN", (0.0, 0.0, 5.48), 0.82, 0.40, magenta, vertices=25)
    pipe("CSBEAM", (0.0, 0.0, 5.42), (0.0, 0.0, 7.85), 0.11, cyan, vertices=14)
    box("CSAPEX", (0.0, 0.0, 8.00), (0.50, 0.50, 0.50), magenta,
        rotation=(math.radians(35), math.radians(35), math.radians(45)), bevel=0.06)
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
    if ratio < 0.999:
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

clear_scene()
sentry_parts, sentry_groups = build_sentry()
results.append(export_asset(
    {"slug": "sentry", "runtime": "sentry", "source_parts": ("Freegrid", "ArcSentry")},
    sentry_parts,
    sentry_groups,
    ((steel, "STBODY", 0.52, 0.26), (white, "STARMOR", 0.50, 0.24),
     (cyan, "HouseColor", 0.55, 0.28)),
    (0.0, -0.45, 1.12),
    (3.2, 4.6, 2.4),
    6.0,
    1.2,
))

clear_scene()
pylon_parts, pylon_groups = build_pylon()
results.append(export_asset(
    {"slug": "pylon", "runtime": "pylon", "source_parts": ("Chorus", "SignalPylon")},
    pylon_parts,
    pylon_groups,
    ((steel, "PYBODY", 0.52, 0.26), (magenta, "PYMAG", 0.50, 0.24),
     (cyan, "PYGLOW", 0.55, 0.28)),
    (0.0, 0.0, 2.20),
    (3.2, 3.2, 4.5),
    5.8,
    2.2,
))

clear_scene()
relay_core_parts, relay_core_groups = build_relay_core()
results.append(export_asset(
    {"slug": "relaycore", "runtime": "relaycore", "source_parts": ("Freegrid", "RelayCore")},
    relay_core_parts,
    relay_core_groups,
    ((steel, "RCBODY", 0.52, 0.26), (white, "RCARMOR", 0.50, 0.24),
     (cyan, "HouseColor", 0.55, 0.28)),
    (0.10, 0.0, 1.85),
    (5.5, 4.2, 3.8),
    7.2,
    1.9,
))

clear_scene()
fabricator_parts, fabricator_groups = build_fabricator_bay()
results.append(export_asset(
    {"slug": "fabricbay", "runtime": "fabricbay", "source_parts": ("Freegrid", "FabricatorBay")},
    fabricator_parts,
    fabricator_groups,
    ((steel, "FBBODY", 0.52, 0.26), (white, "FBARMOR", 0.50, 0.24),
     (cyan, "HouseColor", 0.55, 0.28)),
    (0.0, 0.0, 1.95),
    (6.8, 5.4, 4.1),
    8.0,
    2.0,
))

clear_scene()
spire_parts, spire_groups = build_chorus_spire()
results.append(export_asset(
    {"slug": "spire", "runtime": "spire", "source_parts": ("Chorus", "Spire")},
    spire_parts,
    spire_groups,
    ((steel, "CSBODY", 0.52, 0.26), (magenta, "CSMAG", 1.0, 1.0),
     (cyan, "CSGLOW", 0.55, 0.28)),
    (0.0, 0.0, 4.00),
    (5.8, 5.8, 8.2),
    10.0,
    4.0,
))

clear_scene()
fabricator_rig_parts, fabricator_rig_groups = build_fabricator_rig()
results.append(export_asset(
    {"slug": "fabricrig", "runtime": "fabricrig", "source_parts": ("Freegrid", "FabricatorRig")},
    fabricator_rig_parts,
    fabricator_rig_groups,
    ((steel, "FRBODY", 0.52, 0.26), (white, "FRARMOR", 0.50, 0.24),
     (cyan, "HouseColor", 0.55, 0.28)),
    (0.0, 0.0, 0.88),
    (4.9, 3.0, 2.1),
    6.0,
    1.0,
))

clear_scene()
lancer_parts, lancer_groups = build_lancer()
results.append(export_asset(
    {"slug": "lancer", "runtime": "lancer", "source_parts": ("Freegrid", "LancerCrew")},
    lancer_parts,
    lancer_groups,
    ((steel, "LNBODY", 0.52, 0.26), (white, "LNARMOR", 0.50, 0.24),
     (cyan, "HouseColor", 0.55, 0.28)),
    (0.0, 0.0, 0.72),
    (4.3, 2.2, 1.6),
    5.4,
    0.8,
))

clear_scene()
coil_parts, coil_groups = build_coil_carrier()
results.append(export_asset(
    {"slug": "coil", "runtime": "coil", "source_parts": ("Freegrid", "CoilCarrier")},
    coil_parts,
    coil_groups,
    ((steel, "CCBODY", 0.52, 0.26), (white, "CCARMOR", 0.50, 0.24),
     (cyan, "HouseColor", 0.55, 0.28)),
    (0.0, 0.0, 0.98),
    (5.4, 3.5, 2.2),
    6.8,
    1.1,
))

clear_scene()
warden_parts, warden_groups = build_warden()
results.append(export_asset(
    {"slug": "warden", "runtime": "warden", "source_parts": ("Chorus", "Warden")},
    warden_parts,
    warden_groups,
    ((steel, "WDBODY", 0.52, 0.26), (magenta, "WDMAG", 0.50, 0.24),
     (cyan, "WDGLOW", 0.55, 0.28)),
    (0.0, 0.0, 0.92),
    (4.0, 4.0, 1.9),
    5.2,
    1.0,
))

clear_scene()
harrower_parts, harrower_groups = build_harrower()
results.append(export_asset(
    {"slug": "harrower", "runtime": "harrower", "source_parts": ("Chorus", "Harrower")},
    harrower_parts,
    harrower_groups,
    ((steel, "HABODY", 0.52, 0.26), (magenta, "HAMAG", 0.50, 0.24),
     (cyan, "HAGLOW", 0.55, 0.28)),
    (0.0, 0.0, 1.42),
    (5.8, 5.8, 3.2),
    7.2,
    1.6,
))

clear_scene()
nest_parts, nest_groups = build_machine_nest()
results.append(export_asset(
    {"slug": "nest", "runtime": "nest", "source_parts": ("Chorus", "MachineNest")},
    nest_parts,
    nest_groups,
    ((steel, "MNBODY", 0.52, 0.26), (magenta, "MNMAG", 0.50, 0.24),
     (cyan, "MNGLOW", 0.55, 0.28)),
    (0.0, 0.0, 0.78),
    (6.2, 6.2, 2.8),
    7.8,
    1.1,
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
