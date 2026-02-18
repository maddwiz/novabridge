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


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def spawn_mesh_actor(
    ue: NovaBridge,
    spawned_actors: list[str],
    *,
    label: str,
    mesh_path: str,
    location: dict[str, float],
    scale: dict[str, float],
    rotation: dict[str, float] | None = None,
    build_steps: int = 10,
    build_step_delay: float = 0.10,
    rise_height: float = 520.0,
) -> str:
    final_location = {
        "x": location.get("x", 0.0),
        "y": location.get("y", 0.0),
        "z": location.get("z", 0.0),
    }
    start_location = {"x": final_location["x"], "y": final_location["y"], "z": final_location["z"] + rise_height}
    final_scale = {"x": scale.get("x", 1.0), "y": scale.get("y", 1.0), "z": scale.get("z", 1.0)}
    start_scale = {
        "x": max(0.01, final_scale["x"] * 0.015),
        "y": max(0.01, final_scale["y"] * 0.015),
        "z": max(0.01, final_scale["z"] * 0.015),
    }

    actor = retry_call(
        ue.spawn,
        "StaticMeshActor",
        label=label,
        x=start_location["x"],
        y=start_location["y"],
        z=start_location["z"],
        pitch=(rotation or {}).get("pitch", 0.0),
        yaw=(rotation or {}).get("yaw", 0.0),
        roll=(rotation or {}).get("roll", 0.0),
    )
    spawned_actors.append(actor["label"])
    retry_call(ue.set_property, actor["label"], "StaticMeshComponent0.StaticMesh", mesh_path)
    retry_call(ue.transform, actor["label"], location=start_location, scale=start_scale)
    pause(max(0.06, build_step_delay))

    for step in range(1, max(2, build_steps) + 1):
        t = step / float(max(2, build_steps))
        loc = {
            "x": lerp(start_location["x"], final_location["x"], t),
            "y": lerp(start_location["y"], final_location["y"], t),
            "z": lerp(start_location["z"], final_location["z"], t),
        }
        scl = {
            "x": lerp(start_scale["x"], final_scale["x"], t),
            "y": lerp(start_scale["y"], final_scale["y"], t),
            "z": lerp(start_scale["z"], final_scale["z"], t),
        }
        retry_call(ue.transform, actor["label"], location=loc, scale=scl, _retries=2, _delay=0.25)
        pause(build_step_delay)
    return actor["label"]


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a recordable UE5 demo scene via NovaBridge.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=30010)
    parser.add_argument("--pause", type=float, default=1.0, help="Seconds between visible build steps.")
    parser.add_argument("--camera-frames", type=int, default=96, help="Frames for final orbit.")
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
    print("waiting for NovaBridge health...")
    health = retry_call(ue.health, _retries=120, _delay=2.0)
    print("health:", health)

    banner("Create Primitive Assets")
    primitive_specs = [("plane", 2400), ("cube", 120), ("sphere", 120)]
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

    banner("Set Stage Camera (Wide)")
    retry_call(
        ue.set_camera,
        location={"x": 980, "y": -980, "z": 580},
        rotation={"pitch": -17, "yaw": 42, "roll": 0},
        fov=62,
        show_flags={"Grid": False, "BSP": False, "Selection": False},
    )
    pause(args.pause)

    banner("Build Familiar Scene: Cozy Desk Setup")
    floor = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_Floor",
        mesh_path=primitive_paths["plane"],
        location={"x": 0, "y": 0, "z": -55},
        scale={"x": 1.2, "y": 1.2, "z": 1.0},
    )
    print("spawned floor:", floor)

    back_wall = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_BackWall",
        mesh_path=primitive_paths["cube"],
        location={"x": 0, "y": 760, "z": 220},
        scale={"x": 16.0, "y": 0.25, "z": 5.0},
    )
    print("spawned back wall:", back_wall)

    left_wall = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_LeftWall",
        mesh_path=primitive_paths["cube"],
        location={"x": -960, "y": 0, "z": 220},
        scale={"x": 0.25, "y": 12.5, "z": 5.0},
    )
    print("spawned side wall:", left_wall)
    pause(args.pause)

    banner("Build Desk")
    desk_top = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_DeskTop",
        mesh_path=primitive_paths["cube"],
        location={"x": 0, "y": 220, "z": 94},
        scale={"x": 5.2, "y": 1.9, "z": 0.22},
    )
    print("spawned desk top:", desk_top)

    leg_offsets = [(-230, 140), (230, 140), (-230, 300), (230, 300)]
    for idx, (lx, ly) in enumerate(leg_offsets):
        leg = spawn_mesh_actor(
            ue,
            spawned_actors,
            label=f"{prefix}_DeskLeg_{idx}",
            mesh_path=primitive_paths["cube"],
            location={"x": lx, "y": ly, "z": 12},
            scale={"x": 0.2, "y": 0.2, "z": 1.35},
        )
        print("spawned desk leg:", leg)
        pause(args.pause * 0.35)

    banner("Build Monitor + Accessories")
    monitor_base = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_MonitorBase",
        mesh_path=primitive_paths["cube"],
        location={"x": 0, "y": 300, "z": 108},
        scale={"x": 1.1, "y": 0.45, "z": 0.08},
    )
    print("spawned monitor base:", monitor_base)

    monitor_stand = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_MonitorStand",
        mesh_path=primitive_paths["cube"],
        location={"x": 0, "y": 310, "z": 154},
        scale={"x": 0.16, "y": 0.16, "z": 0.82},
    )
    print("spawned monitor stand:", monitor_stand)

    monitor_screen = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_MonitorScreen",
        mesh_path=primitive_paths["cube"],
        location={"x": 0, "y": 344, "z": 205},
        scale={"x": 2.2, "y": 0.08, "z": 1.25},
    )
    print("spawned monitor:", monitor_screen)

    keyboard = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_Keyboard",
        mesh_path=primitive_paths["cube"],
        location={"x": -20, "y": 165, "z": 102},
        scale={"x": 1.6, "y": 0.5, "z": 0.06},
    )
    print("spawned keyboard:", keyboard)

    mouse = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_Mouse",
        mesh_path=primitive_paths["sphere"],
        location={"x": 170, "y": 160, "z": 103},
        scale={"x": 0.22, "y": 0.30, "z": 0.10},
    )
    print("spawned mouse:", mouse)
    pause(args.pause)

    banner("Build Chair + Small Props")
    chair_seat = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_ChairSeat",
        mesh_path=primitive_paths["cube"],
        location={"x": 0, "y": -60, "z": 48},
        scale={"x": 1.7, "y": 1.5, "z": 0.2},
    )
    print("spawned chair seat:", chair_seat)

    chair_back = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_ChairBack",
        mesh_path=primitive_paths["cube"],
        location={"x": 0, "y": -140, "z": 120},
        scale={"x": 1.7, "y": 0.2, "z": 1.2},
    )
    print("spawned chair back:", chair_back)

    chair_center = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_ChairCenter",
        mesh_path=primitive_paths["cube"],
        location={"x": 0, "y": -60, "z": 12},
        scale={"x": 0.25, "y": 0.25, "z": 0.9},
    )
    print("spawned chair center:", chair_center)

    mug = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_Mug",
        mesh_path=primitive_paths["sphere"],
        location={"x": -180, "y": 170, "z": 107},
        scale={"x": 0.24, "y": 0.24, "z": 0.34},
    )
    print("spawned mug:", mug)

    plant_pot = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_PlantPot",
        mesh_path=primitive_paths["cube"],
        location={"x": 280, "y": 320, "z": 112},
        scale={"x": 0.35, "y": 0.35, "z": 0.3},
    )
    print("spawned plant pot:", plant_pot)

    plant_top = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_PlantTop",
        mesh_path=primitive_paths["sphere"],
        location={"x": 280, "y": 320, "z": 152},
        scale={"x": 0.55, "y": 0.55, "z": 0.55},
    )
    print("spawned plant top:", plant_top)
    pause(args.pause)

    banner("Lighting")
    key_light = retry_call(ue.spawn, "PointLight", label=f"{prefix}_KeyLight", x=420, y=-180, z=360)
    fill_light = retry_call(ue.spawn, "PointLight", label=f"{prefix}_FillLight", x=-420, y=210, z=260)
    desk_light = retry_call(ue.spawn, "PointLight", label=f"{prefix}_DeskLight", x=40, y=280, z=210)
    rim_light = retry_call(ue.spawn, "PointLight", label=f"{prefix}_RimLight", x=0, y=640, z=420)
    spawned_actors.extend([key_light["label"], fill_light["label"], desk_light["label"], rim_light["label"]])
    retry_call(ue.set_property, key_light["label"], "PointLightComponent0.Intensity", 26000)
    retry_call(ue.set_property, fill_light["label"], "PointLightComponent0.Intensity", 12000)
    retry_call(ue.set_property, desk_light["label"], "PointLightComponent0.Intensity", 9000)
    retry_call(ue.set_property, rim_light["label"], "PointLightComponent0.Intensity", 7000)
    print("placed lights")
    pause(args.pause)

    banner("Reveal Shots")
    reveal_cameras = [
        ({"x": 620, "y": -480, "z": 330}, {"pitch": -14, "yaw": 34, "roll": 0}, 58),
        ({"x": -520, "y": -180, "z": 270}, {"pitch": -10, "yaw": 18, "roll": 0}, 53),
        ({"x": 0, "y": -720, "z": 260}, {"pitch": -9, "yaw": 0, "roll": 0}, 52),
    ]
    for cam_loc, cam_rot, cam_fov in reveal_cameras:
        retry_call(
            ue.set_camera,
            location=cam_loc,
            rotation=cam_rot,
            fov=cam_fov,
            show_flags={"Grid": False, "BSP": False, "Selection": False},
        )
        pause(args.pause * 1.25)

    banner("Final Orbit")
    frames = max(8, args.camera_frames)
    for i in range(frames):
        t = i / frames
        angle = 2 * math.pi * t
        radius = 890
        x = radius * math.cos(angle)
        y = radius * math.sin(angle)
        z = 260 + 36 * math.sin(angle * 1.4)
        yaw = math.degrees(math.atan2(220 - y, -x))
        pitch = -10 + 2.2 * math.sin(angle)
        retry_call(
            ue.set_camera,
            location={"x": x, "y": y, "z": z},
            rotation={"pitch": pitch, "yaw": yaw, "roll": 0},
            fov=56,
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
