#!/usr/bin/env python3
"""
Generate a parametric female character using MB-Lab in Blender.
Run: blender --background --python generate-mblab-female.py -- --output /path/to/output.blend

Available templates: human_female_base, human_male_base, anime_female, anime_male
Available characters: f_af01, f_as01, f_ca01, f_la01 (African, Asian, Caucasian, Latin)

MB-Lab generates a full body with 18,000+ vertices, proper face topology,
and a complete armature for animation.
"""

import bpy
import sys

# Parse args after '--'
argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []

output_path = '/tmp/nova-avatar.blend'
if '--output' in argv:
    output_path = argv[argv.index('--output') + 1]

template = 'human_female_base'
if '--template' in argv:
    template = argv[argv.index('--template') + 1]

character = 'f_ca01'
if '--character' in argv:
    character = argv[argv.index('--character') + 1]

# Enable MB-Lab
bpy.ops.preferences.addon_enable(module='MB-Lab')

# Configure
scene = bpy.context.scene
scene.mblab_template_name = template
scene.mblab_character_name = character

# Generate the character
bpy.ops.mbast.init_character()

def is_flat_plane(obj):
    dims = obj.dimensions
    return dims.z < 0.01 and dims.x > 1.0 and dims.y > 1.0

# Find the generated character mesh (largest mesh by vertex count).
mesh_objs = [obj for obj in bpy.data.objects if obj.type == 'MESH']
char_obj = max(mesh_objs, key=lambda obj: len(obj.data.vertices), default=None)

if char_obj:
    # Keep character mesh + related meshes on the same armature, remove everything else.
    armature_obj = char_obj.parent if char_obj.parent and char_obj.parent.type == 'ARMATURE' else None
    if armature_obj is None:
        for mod in char_obj.modifiers:
            if mod.type == 'ARMATURE' and mod.object:
                armature_obj = mod.object
                break

    keep_names = {char_obj.name}
    if armature_obj:
        keep_names.add(armature_obj.name)
        for obj in bpy.data.objects:
            if obj.type != 'MESH':
                continue
            if obj.parent == armature_obj:
                keep_names.add(obj.name)
                continue
            for mod in obj.modifiers:
                if mod.type == 'ARMATURE' and mod.object == armature_obj:
                    keep_names.add(obj.name)
                    break

    removed = 0
    for obj in list(bpy.data.objects):
        if obj.name in keep_names:
            if obj.type == 'MESH' and is_flat_plane(obj):
                bpy.data.objects.remove(obj, do_unlink=True)
                removed += 1
            continue
        bpy.data.objects.remove(obj, do_unlink=True)
        removed += 1

    verts = len(char_obj.data.vertices)
    faces = len(char_obj.data.polygons)
    print(f"[Nova] Generated: {char_obj.name}")
    print(f"[Nova] Vertices: {verts}, Faces: {faces}")
    print(f"[Nova] Template: {template}, Character: {character}")
    print(f"[Nova] Removed non-character objects: {removed}")
else:
    print('[Nova] Warning: No generated mesh found after MB-Lab init')

# Save
bpy.ops.wm.save_as_mainfile(filepath=output_path)
print(f"[Nova] Saved to: {output_path}")

# Also export FBX for UE5 import
fbx_path = output_path.replace('.blend', '.fbx')
bpy.ops.export_scene.fbx(
    filepath=fbx_path,
    use_selection=False,
    apply_scale_options='FBX_SCALE_ALL',
    mesh_smooth_type='FACE',
)
print(f"[Nova] FBX exported to: {fbx_path}")
