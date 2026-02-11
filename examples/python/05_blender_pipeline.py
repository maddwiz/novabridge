#!/usr/bin/env python3
"""Example Blender-to-UE5 pipeline using local Blender + NovaBridge import."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK_DIR = ROOT / "python-sdk"
if str(SDK_DIR) not in sys.path:
    sys.path.insert(0, str(SDK_DIR))

from novabridge import NovaBridge  # noqa: E402


def main() -> None:
    ue5 = NovaBridge()

    with tempfile.TemporaryDirectory(prefix="novabridge-blender-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        obj_path = tmp_dir / "pipeline_mesh.obj"
        script_path = tmp_dir / "gen.py"
        script_path.write_text(
            """
import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, location=(0,0,0))
bpy.ops.wm.obj_export(filepath=r'""" + str(obj_path).replace("\\", "/") + """', export_selected_objects=False, export_uv=True, export_normals=True, export_materials=False, forward_axis='NEGATIVE_Y', up_axis='Z', global_scale=1.0)
print('[Nova] OBJ written')
""".strip()
        )

        subprocess.run(["/usr/bin/blender", "--background", "--python", str(script_path)], check=True)

        imported = ue5.import_asset(str(obj_path), asset_name="PipelineSphere", destination="/Game", scale=100)
        print(json.dumps(imported, indent=2))

        cleanup = ue5._post("/asset/delete", {"path": "/Game/PipelineSphere.PipelineSphere"})
        print(json.dumps(cleanup, indent=2))


if __name__ == "__main__":
    main()
