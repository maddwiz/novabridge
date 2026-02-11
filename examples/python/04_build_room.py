#!/usr/bin/env python3
"""Build a tiny lit room using NovaBridge primitives and properties."""

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

    floor = ue5.spawn("StaticMeshActor", label="RoomFloor", z=-10)
    key = ue5.spawn("PointLight", label="RoomKeyLight", x=0, y=0, z=280)
    fill = ue5.spawn("PointLight", label="RoomFillLight", x=180, y=120, z=220)

    ue5.set_property(key["label"], "PointLightComponent0.Intensity", 15000)
    ue5.set_property(fill["label"], "PointLightComponent0.Intensity", 8000)

    ue5.set_camera(
        location={"x": 360, "y": 50, "z": 160},
        rotation={"pitch": -12, "yaw": 190, "roll": 0},
        fov=70,
        show_flags={"Grid": False},
    )

    out = ROOT / "examples" / "python" / "room.png"
    print(ue5.viewport_screenshot(save_path=str(out), raw=True))
    print("saved:", out)

    for actor in (floor, key, fill):
        ue5.delete_actor(actor["label"])


if __name__ == "__main__":
    main()
