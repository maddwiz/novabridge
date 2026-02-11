#!/usr/bin/env python3
"""Build a simple showcase scene with NovaBridge and save a screenshot."""

from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK_DIR = ROOT / "python-sdk"
if str(SDK_DIR) not in sys.path:
    sys.path.insert(0, str(SDK_DIR))

from novabridge import NovaBridge  # noqa: E402


def main() -> None:
    ue5 = NovaBridge()

    print("health:", ue5.health())
    print("project:", ue5.project_info())

    floor = ue5.spawn("StaticMeshActor", label="DemoFloor", z=-20)
    light = ue5.spawn("PointLight", label="DemoLight", z=300)

    ue5.set_property(light["label"], "PointLightComponent0.Intensity", 12000)
    ue5.set_property(light["label"], "PointLightComponent0.LightColor", "(R=255,G=230,B=200,A=255)")

    ue5.set_camera(
        location={"x": 420, "y": 0, "z": 180},
        rotation={"pitch": -10, "yaw": 180, "roll": 0},
        fov=65,
        show_flags={"Grid": False, "BSP": False},
    )

    out = ROOT / "examples" / "demo-scene" / "demo_scene.png"
    result = ue5.viewport_screenshot(save_path=str(out), raw=True)
    print("screenshot:", result)
    print("saved:", out)

    # cleanup demo actors created by this script
    ue5.delete_actor(floor["label"])
    ue5.delete_actor(light["label"])


if __name__ == "__main__":
    main()
