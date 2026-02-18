#!/usr/bin/env python3
"""Build a familiar gaming battlestation scene for live UE recording demos."""

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


def spawn_mesh_actor(
    ue: NovaBridge,
    spawned_actors: list[str],
    *,
    label: str,
    mesh_path: str,
    location: dict[str, float],
    scale: dict[str, float],
    rotation: dict[str, float] | None = None,
) -> str:
    actor = retry_call(
        ue.spawn,
        "StaticMeshActor",
        label=label,
        x=location.get("x", 0.0),
        y=location.get("y", 0.0),
        z=location.get("z", 0.0),
        pitch=(rotation or {}).get("pitch", 0.0),
        yaw=(rotation or {}).get("yaw", 0.0),
        roll=(rotation or {}).get("roll", 0.0),
    )
    spawned_actors.append(actor["label"])
    retry_call(ue.set_property, actor["label"], "StaticMeshComponent0.StaticMesh", mesh_path)
    retry_call(ue.transform, actor["label"], scale=scale)
    return actor["label"]


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a gaming battlestation scene via NovaBridge.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=30010)
    parser.add_argument("--pause", type=float, default=0.9, help="Seconds between visible build steps.")
    parser.add_argument("--camera-frames", type=int, default=120, help="Frames for final orbit.")
    parser.add_argument(
        "--cleanup",
        action="store_true",
        help="Delete demo actors when finished (default: keep scene in editor).",
    )
    args = parser.parse_args()

    ue = NovaBridge(host=args.host, port=args.port, timeout=120)
    prefix = f"GamingDemo_{time.strftime('%Y%m%d_%H%M%S')}"
    spawned_actors: list[str] = []

    banner("Health")
    health = retry_call(ue.health)
    print("health:", health)

    banner("Create Primitive Assets")
    primitive_specs = [("plane", 2600), ("cube", 120), ("sphere", 120)]
    primitive_paths: dict[str, str] = {}
    for shape_name, size in primitive_specs:
        asset_name = f"{prefix}_{shape_name.capitalize()}"
        created = retry_call(
            ue._post,  # type: ignore[attr-defined]
            "/mesh/primitive",
            {"type": shape_name, "name": asset_name, "path": "/Game", "size": size},
        )
        primitive_paths[shape_name] = created["path"]
        print("created asset:", created["path"])
        pause(args.pause)

    banner("Set Wide Camera")
    retry_call(
        ue.set_camera,
        location={"x": 1120, "y": -1100, "z": 640},
        rotation={"pitch": -16, "yaw": 38, "roll": 0},
        fov=60,
        show_flags={"Grid": False, "BSP": False, "Selection": False},
    )
    pause(args.pause)

    banner("Build Room + Desk")
    floor = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_Floor",
        mesh_path=primitive_paths["plane"],
        location={"x": 0, "y": 0, "z": -56},
        scale={"x": 1.3, "y": 1.3, "z": 1.0},
    )
    print("spawned:", floor)
    back_wall = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_BackWall",
        mesh_path=primitive_paths["cube"],
        location={"x": 0, "y": 820, "z": 250},
        scale={"x": 18.0, "y": 0.25, "z": 5.5},
    )
    print("spawned:", back_wall)
    side_wall = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_SideWall",
        mesh_path=primitive_paths["cube"],
        location={"x": -1080, "y": 0, "z": 250},
        scale={"x": 0.25, "y": 14.0, "z": 5.5},
    )
    print("spawned:", side_wall)

    desk = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_DeskTop",
        mesh_path=primitive_paths["cube"],
        location={"x": 0, "y": 210, "z": 95},
        scale={"x": 6.2, "y": 2.1, "z": 0.22},
    )
    print("spawned:", desk)

    for idx, (lx, ly) in enumerate([(-280, 130), (280, 130), (-280, 290), (280, 290)]):
        leg = spawn_mesh_actor(
            ue,
            spawned_actors,
            label=f"{prefix}_DeskLeg_{idx}",
            mesh_path=primitive_paths["cube"],
            location={"x": lx, "y": ly, "z": 12},
            scale={"x": 0.2, "y": 0.2, "z": 1.35},
        )
        print("spawned:", leg)
        pause(args.pause * 0.35)

    banner("Build Battlestation Props")
    # Dual monitor setup
    for idx, x in enumerate([-170, 170]):
        monitor = spawn_mesh_actor(
            ue,
            spawned_actors,
            label=f"{prefix}_Monitor_{idx}",
            mesh_path=primitive_paths["cube"],
            location={"x": x, "y": 350, "z": 212},
            scale={"x": 1.85, "y": 0.08, "z": 1.12},
            rotation={"pitch": 0, "yaw": -7 if idx == 0 else 7, "roll": 0},
        )
        stand = spawn_mesh_actor(
            ue,
            spawned_actors,
            label=f"{prefix}_MonitorStand_{idx}",
            mesh_path=primitive_paths["cube"],
            location={"x": x, "y": 320, "z": 152},
            scale={"x": 0.16, "y": 0.16, "z": 0.82},
        )
        print("spawned:", monitor, stand)
        pause(args.pause * 0.45)

    keyboard = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_Keyboard",
        mesh_path=primitive_paths["cube"],
        location={"x": -40, "y": 150, "z": 102},
        scale={"x": 1.95, "y": 0.52, "z": 0.06},
    )
    mouse = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_Mouse",
        mesh_path=primitive_paths["sphere"],
        location={"x": 190, "y": 145, "z": 103},
        scale={"x": 0.25, "y": 0.32, "z": 0.11},
    )
    print("spawned:", keyboard, mouse)

    pc_tower = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_Tower",
        mesh_path=primitive_paths["cube"],
        location={"x": 420, "y": 210, "z": 130},
        scale={"x": 0.95, "y": 1.15, "z": 2.05},
    )
    print("spawned:", pc_tower)

    for idx, x in enumerate([-360, 360]):
        speaker = spawn_mesh_actor(
            ue,
            spawned_actors,
            label=f"{prefix}_Speaker_{idx}",
            mesh_path=primitive_paths["cube"],
            location={"x": x, "y": 300, "z": 130},
            scale={"x": 0.45, "y": 0.35, "z": 0.9},
        )
        print("spawned:", speaker)
        pause(args.pause * 0.35)

    chair_seat = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_ChairSeat",
        mesh_path=primitive_paths["cube"],
        location={"x": 0, "y": -70, "z": 50},
        scale={"x": 1.9, "y": 1.6, "z": 0.2},
    )
    chair_back = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_ChairBack",
        mesh_path=primitive_paths["cube"],
        location={"x": 0, "y": -160, "z": 128},
        scale={"x": 1.9, "y": 0.22, "z": 1.25},
    )
    chair_base = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_ChairBase",
        mesh_path=primitive_paths["sphere"],
        location={"x": 0, "y": -70, "z": 8},
        scale={"x": 0.28, "y": 0.28, "z": 0.15},
    )
    print("spawned:", chair_seat, chair_back, chair_base)
    pause(args.pause)

    banner("Add Signature Backdrop Elements")
    for idx, x in enumerate([-320, 0, 320]):
        panel = spawn_mesh_actor(
            ue,
            spawned_actors,
            label=f"{prefix}_NeonPanel_{idx}",
            mesh_path=primitive_paths["cube"],
            location={"x": x, "y": 804, "z": 290},
            scale={"x": 0.22, "y": 0.08, "z": 2.15},
        )
        print("spawned:", panel)
        pause(args.pause * 0.3)

    banner("Lighting")
    key_light = retry_call(ue.spawn, "PointLight", label=f"{prefix}_KeyLight", x=540, y=-210, z=390)
    fill_light = retry_call(ue.spawn, "PointLight", label=f"{prefix}_FillLight", x=-520, y=190, z=290)
    monitor_glow_l = retry_call(ue.spawn, "PointLight", label=f"{prefix}_MonitorGlowL", x=-170, y=370, z=215)
    monitor_glow_r = retry_call(ue.spawn, "PointLight", label=f"{prefix}_MonitorGlowR", x=170, y=370, z=215)
    rim_light = retry_call(ue.spawn, "PointLight", label=f"{prefix}_RimLight", x=0, y=720, z=430)
    spawned_actors.extend(
        [key_light["label"], fill_light["label"], monitor_glow_l["label"], monitor_glow_r["label"], rim_light["label"]]
    )

    retry_call(ue.set_property, key_light["label"], "PointLightComponent0.Intensity", 30000)
    retry_call(ue.set_property, fill_light["label"], "PointLightComponent0.Intensity", 14000)
    retry_call(ue.set_property, monitor_glow_l["label"], "PointLightComponent0.Intensity", 7500)
    retry_call(ue.set_property, monitor_glow_r["label"], "PointLightComponent0.Intensity", 7500)
    retry_call(ue.set_property, rim_light["label"], "PointLightComponent0.Intensity", 9000)
    print("placed lights")
    pause(args.pause)

    banner("Cinematic Reveal")
    reveal_cameras = [
        ({"x": 760, "y": -560, "z": 360}, {"pitch": -13, "yaw": 35, "roll": 0}, 56),
        ({"x": -680, "y": -320, "z": 300}, {"pitch": -10, "yaw": 20, "roll": 0}, 53),
        ({"x": 0, "y": -760, "z": 280}, {"pitch": -9, "yaw": 0, "roll": 0}, 50),
    ]
    for cam_loc, cam_rot, cam_fov in reveal_cameras:
        retry_call(
            ue.set_camera,
            location=cam_loc,
            rotation=cam_rot,
            fov=cam_fov,
            show_flags={"Grid": False, "BSP": False, "Selection": False},
        )
        pause(args.pause * 1.2)

    banner("Final Orbit")
    frames = max(12, args.camera_frames)
    for i in range(frames):
        t = i / frames
        angle = 2 * math.pi * t
        radius = 980
        x = radius * math.cos(angle)
        y = 220 + radius * math.sin(angle)
        z = 295 + 42 * math.sin(angle * 1.2)
        yaw = math.degrees(math.atan2(220 - y, -x))
        pitch = -10 + 2 * math.sin(angle)
        retry_call(
            ue.set_camera,
            location={"x": x, "y": y, "z": z},
            rotation={"pitch": pitch, "yaw": yaw, "roll": 0},
            fov=54,
            show_flags={"Grid": False, "BSP": False, "Selection": False},
            _retries=2,
            _delay=0.35,
        )
        pause(0.07)

    out_path = ROOT / "examples" / "demo-scene" / "live_operator_demo_gaming_end.png"
    screenshot = retry_call(ue.viewport_screenshot, width=1920, height=1080, raw=True, save_path=str(out_path))

    banner("Complete")
    print("screenshot:", screenshot)
    print("saved:", out_path)
    print("actors left in scene:", len(spawned_actors))

    if args.cleanup:
        banner("Cleanup")
        for actor_name in reversed(spawned_actors):
            try:
                retry_call(ue.delete_actor, actor_name, _retries=2, _delay=0.25)
                print("deleted:", actor_name)
            except NovaBridgeError:
                pass


if __name__ == "__main__":
    main()
