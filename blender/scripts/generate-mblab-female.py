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

# Find the generated character mesh
char_obj = None
for obj in bpy.data.objects:
    if obj.type == 'MESH' and obj.name != 'Cube':
        char_obj = obj
        break

if char_obj:
    verts = len(char_obj.data.vertices)
    faces = len(char_obj.data.polygons)
    print(f"[Nova] Generated: {char_obj.name}")
    print(f"[Nova] Vertices: {verts}, Faces: {faces}")
    print(f"[Nova] Template: {template}, Character: {character}")

    # Delete default cube
    if 'Cube' in bpy.data.objects:
        bpy.data.objects.remove(bpy.data.objects['Cube'], do_unlink=True)

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
