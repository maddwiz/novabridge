#!/usr/bin/env python3
"""Run a step-by-step UE5 build sequence for on-screen recording demos."""

from __future__ import annotations

import argparse
import math
import pathlib
import sys
import time
from typing import Any, Callable

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK_DIR = ROOT / "python-sdk"
if str(SDK_DIR) not in sys.path:
    sys.path.insert(0, str(SDK_DIR))

from novabridge import NovaBridge, NovaBridgeError  # noqa: E402


def retry_call(fn: Callable[..., Any], *args: Any, **kwargs: Any) -> Any:
    retries = kwargs.pop("_retries", 5)
    delay = kwargs.pop("_delay", 1.0)
    last_error: Exception | None = None
    for _ in range(retries):
        try:
            return fn(*args, **kwargs)
        except Exception as exc:  # pragma: no cover - integration utility
            last_error = exc
            time.sleep(delay)
    if last_error is not None:
        raise last_error
    raise RuntimeError("retry_call failed without exception")


def banner(title: str) -> None:
    print(f"\n=== {title} ===")


def pause(seconds: float) -> None:
    time.sleep(seconds)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a recordable UE5 demo scene via NovaBridge.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=30010)
    parser.add_argument("--pause", type=float, default=1.4, help="Seconds between visible build steps.")
    parser.add_argument("--camera-frames", type=int, default=48, help="Frames for final orbit.")
    parser.add_argument(
        "--cleanup",
        action="store_true",
        help="Delete demo actors when finished (default: keep scene in editor).",
    )
    args = parser.parse_args()

    ue = NovaBridge(host=args.host, port=args.port, timeout=120)
    prefix = f"LiveDemo_{time.strftime('%Y%m%d_%H%M%S')}"
    created_assets: list[str] = []
    spawned_actors: list[str] = []

    banner("Health")
    health = retry_call(ue.health)
    print("health:", health)

    banner("Create Primitive Assets")
    primitive_specs = [("plane", 2200), ("cube", 160), ("sphere", 230)]
    primitive_paths: dict[str, str] = {}
    for shape_name, size in primitive_specs:
        print(f"choose shape: {shape_name} (size={size})")
        asset_name = f"{prefix}_{shape_name.capitalize()}"
        created = retry_call(
            ue._post,  # type: ignore[attr-defined]
            "/mesh/primitive",
            {"type": shape_name, "name": asset_name, "path": "/Game", "size": size},
        )
        primitive_paths[shape_name] = created["path"]
        created_assets.append(created["path"])
        print("created asset:", created["path"])
        pause(args.pause)

    banner("Set Stage Camera")
    retry_call(
        ue.set_camera,
        location={"x": 980, "y": -880, "z": 520},
        rotation={"pitch": -18, "yaw": 42, "roll": 0},
        fov=65,
        show_flags={"Grid": False, "BSP": False, "Selection": False},
    )
    pause(args.pause)

    banner("Build Scene")
    ground = retry_call(ue.spawn, "StaticMeshActor", label=f"{prefix}_Ground", x=0, y=0, z=-50)
    spawned_actors.append(ground["label"])
    retry_call(ue.set_property, ground["label"], "StaticMeshComponent0.StaticMesh", primitive_paths["plane"])
    print("spawned ground:", ground["label"])
    pause(args.pause)

    core = retry_call(ue.spawn, "StaticMeshActor", label=f"{prefix}_Core", x=0, y=0, z=120)
    spawned_actors.append(core["label"])
    retry_call(ue.set_property, core["label"], "StaticMeshComponent0.StaticMesh", primitive_paths["sphere"])
    retry_call(ue.transform, core["label"], scale={"x": 1.25, "y": 1.25, "z": 1.25})
    print("spawned core:", core["label"])
    pause(args.pause)

    for idx in range(8):
        angle = (2 * math.pi * idx) / 8
        x = 430 * math.cos(angle)
        y = 430 * math.sin(angle)
        z = 65 + 30 * math.sin(angle * 2)
        name = f"{prefix}_Cube_{idx}"
        actor = retry_call(ue.spawn, "StaticMeshActor", label=name, x=x, y=y, z=z, yaw=math.degrees(angle))
        spawned_actors.append(actor["label"])
        retry_call(ue.set_property, actor["label"], "StaticMeshComponent0.StaticMesh", primitive_paths["cube"])
        retry_call(ue.transform, actor["label"], scale={"x": 0.95, "y": 0.95, "z": 1.3})
        print("spawned shape:", actor["label"])
        pause(args.pause * 0.6)

    key_light = retry_call(ue.spawn, "PointLight", label=f"{prefix}_KeyLight", x=380, y=-260, z=380)
    fill_light = retry_call(ue.spawn, "PointLight", label=f"{prefix}_FillLight", x=-380, y=320, z=260)
    rim_light = retry_call(ue.spawn, "PointLight", label=f"{prefix}_RimLight", x=0, y=0, z=560)
    spawned_actors.extend([key_light["label"], fill_light["label"], rim_light["label"]])
    retry_call(ue.set_property, key_light["label"], "PointLightComponent0.Intensity", 22000)
    retry_call(ue.set_property, fill_light["label"], "PointLightComponent0.Intensity", 13000)
    retry_call(ue.set_property, rim_light["label"], "PointLightComponent0.Intensity", 9000)
    print("placed lights")
    pause(args.pause)

    banner("Final Orbit")
    frames = max(8, args.camera_frames)
    for i in range(frames):
        t = i / frames
        angle = 2 * math.pi * t
        radius = 1030
        x = radius * math.cos(angle)
        y = radius * math.sin(angle)
        z = 320 + 45 * math.sin(angle * 1.5)
        yaw = math.degrees(math.atan2(-y, -x))
        pitch = -13 + 2 * math.sin(angle)
        retry_call(
            ue.set_camera,
            location={"x": x, "y": y, "z": z},
            rotation={"pitch": pitch, "yaw": yaw, "roll": 0},
            fov=60,
            show_flags={"Grid": False, "BSP": False, "Selection": False},
            _retries=2,
            _delay=0.4,
        )
        pause(0.08)

    out_path = ROOT / "examples" / "demo-scene" / "live_operator_demo_end.png"
    screenshot = retry_call(ue.viewport_screenshot, width=1920, height=1080, raw=True, save_path=str(out_path))
    banner("Complete")
    print("screenshot:", screenshot)
    print("saved:", out_path)
    print("actors left in scene:", len(spawned_actors))

    if args.cleanup:
        banner("Cleanup")
        for actor_name in reversed(spawned_actors):
            try:
                retry_call(ue.delete_actor, actor_name, _retries=2, _delay=0.3)
                print("deleted:", actor_name)
            except NovaBridgeError:
                pass


if __name__ == "__main__":
    main()
