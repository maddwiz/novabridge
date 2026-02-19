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

BUILD_STEPS = 120
BUILD_STEP_DELAY = 0.133
RISE_HEIGHT = 1500.0


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
    material_path: str | None = None,
    build_steps: int | None = None,
    build_step_delay: float | None = None,
    rise_height: float | None = None,
) -> str:
    steps = max(2, build_steps if build_steps is not None else BUILD_STEPS)
    step_delay = max(0.02, build_step_delay if build_step_delay is not None else BUILD_STEP_DELAY)
    rise = max(120.0, rise_height if rise_height is not None else RISE_HEIGHT)

    final_location = {
        "x": location.get("x", 0.0),
        "y": location.get("y", 0.0),
        "z": location.get("z", 0.0),
    }
    start_location = {"x": final_location["x"], "y": final_location["y"], "z": final_location["z"] + rise}
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
    if material_path:
        retry_call(
            ue.set_property,
            actor["label"],
            "StaticMeshComponent0.Material[0]",
            material_path,
            _retries=8,
            _delay=0.25,
        )
    retry_call(ue.transform, actor["label"], location=start_location, scale=start_scale)
    pause(max(0.18, step_delay * 1.5))

    for step in range(1, steps + 1):
        t = step / float(steps)
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
        pause(step_delay)
    pause(max(0.20, step_delay))
    return actor["label"]


def collect_actor_names(scene_payload: Any) -> list[str]:
    names: list[str] = []
    if not isinstance(scene_payload, dict):
        return names
    actors = scene_payload.get("actors")
    if not isinstance(actors, list):
        return names
    for actor in actors:
        if isinstance(actor, str):
            names.append(actor)
            continue
        if not isinstance(actor, dict):
            continue
        label = actor.get("label")
        name = actor.get("name")
        if isinstance(label, str):
            names.append(label)
        elif isinstance(name, str):
            names.append(name)
    return names


def cleanup_previous_demo_actors(ue: NovaBridge) -> int:
    prefixes = ("GamingDemo_", "LiveDemo_")
    scene_payload = retry_call(ue.scene_list, _retries=6, _delay=1.0)
    actor_names = collect_actor_names(scene_payload)
    removed = 0
    for actor_name in actor_names:
        if not actor_name.startswith(prefixes):
            continue
        try:
            retry_call(ue.delete_actor, actor_name, _retries=2, _delay=0.25)
            removed += 1
        except Exception:
            continue
    return removed


def ensure_material(
    ue: NovaBridge,
    *,
    name: str,
    color: dict[str, float],
    path: str = "/Game/NovaBridgeDemoMats",
) -> str:
    try:
        result = retry_call(ue.create_material, name, path=path, color=color, _retries=2, _delay=0.4)
        created_path = result.get("path")
        if isinstance(created_path, str) and created_path:
            return created_path
    except NovaBridgeError as exc:
        if "exists" not in str(exc).lower():
            raise
    material_path = f"{path}/{name}.{name}"
    try:
        retry_call(ue._post, "/material/get", {"path": material_path}, _retries=2, _delay=0.4)  # type: ignore[attr-defined]
    except Exception:
        pass
    return material_path


def build_color_palette(ue: NovaBridge) -> dict[str, str]:
    colors = {
        "floor_charcoal": {"r": 0.07, "g": 0.08, "b": 0.10},
        "wall_navy": {"r": 0.06, "g": 0.10, "b": 0.18},
        "desk_graphite": {"r": 0.11, "g": 0.12, "b": 0.14},
        "metal_dark": {"r": 0.12, "g": 0.13, "b": 0.16},
        "screen_blue": {"r": 0.08, "g": 0.42, "b": 0.96},
        "screen_pink": {"r": 0.96, "g": 0.24, "b": 0.70},
        "tower_magenta": {"r": 0.75, "g": 0.14, "b": 0.62},
        "speaker_cyan": {"r": 0.14, "g": 0.80, "b": 0.86},
        "chair_red": {"r": 0.78, "g": 0.14, "b": 0.14},
        "keyboard_black": {"r": 0.04, "g": 0.04, "b": 0.05},
        "panel_neon": {"r": 0.00, "g": 0.92, "b": 0.84},
    }
    return {
        key: ensure_material(ue, name=f"NBGaming_{key.title().replace('_', '')}", color=value)
        for key, value in colors.items()
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a gaming battlestation scene via NovaBridge.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=30010)
    parser.add_argument("--pause", type=float, default=0.7, help="Seconds between section transitions.")
    parser.add_argument("--camera-frames", type=int, default=144, help="Frames for final orbit.")
    parser.add_argument("--drop-steps", type=int, default=120, help="Transform updates per spawned actor descent.")
    parser.add_argument("--drop-seconds", type=float, default=16.0, help="Seconds each spawned actor takes to descend.")
    parser.add_argument("--drop-height", type=float, default=1500.0, help="Starting Z offset before descent.")
    parser.add_argument(
        "--cleanup",
        action="store_true",
        help="Delete demo actors when finished (default: keep scene in editor).",
    )
    args = parser.parse_args()
    global BUILD_STEPS, BUILD_STEP_DELAY, RISE_HEIGHT
    BUILD_STEPS = max(8, args.drop_steps)
    BUILD_STEP_DELAY = max(0.03, args.drop_seconds / float(BUILD_STEPS))
    RISE_HEIGHT = max(120.0, args.drop_height)

    ue = NovaBridge(host=args.host, port=args.port, timeout=120)
    prefix = f"GamingDemo_{time.strftime('%Y%m%d_%H%M%S')}"
    spawned_actors: list[str] = []

    banner("Health")
    print("waiting for NovaBridge health...")
    print(
        f"build profile: steps={BUILD_STEPS}, step_delay={BUILD_STEP_DELAY:.3f}s, "
        f"descent_seconds={BUILD_STEPS * BUILD_STEP_DELAY:.1f}, rise_height={RISE_HEIGHT:.0f}"
    )
    health = retry_call(ue.health, _retries=120, _delay=2.0)
    print("health:", health)
    banner("Reset Previous Demo Actors")
    removed = cleanup_previous_demo_actors(ue)
    print("removed stale demo actors:", removed)
    pause(min(0.8, args.pause))

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

    banner("Prepare Color Palette")
    palette = build_color_palette(ue)
    print("palette materials:", len(palette))
    pause(args.pause * 0.6)

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
        material_path=palette["floor_charcoal"],
        location={"x": 0, "y": 0, "z": -56},
        scale={"x": 1.3, "y": 1.3, "z": 1.0},
    )
    print("spawned:", floor)
    back_wall = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_BackWall",
        mesh_path=primitive_paths["cube"],
        material_path=palette["wall_navy"],
        location={"x": 0, "y": 820, "z": 250},
        scale={"x": 18.0, "y": 0.25, "z": 5.5},
    )
    print("spawned:", back_wall)
    side_wall = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_SideWall",
        mesh_path=primitive_paths["cube"],
        material_path=palette["wall_navy"],
        location={"x": -1080, "y": 0, "z": 250},
        scale={"x": 0.25, "y": 14.0, "z": 5.5},
    )
    print("spawned:", side_wall)

    desk = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_DeskTop",
        mesh_path=primitive_paths["cube"],
        material_path=palette["desk_graphite"],
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
            material_path=palette["metal_dark"],
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
            material_path=palette["screen_blue"] if idx == 0 else palette["screen_pink"],
            location={"x": x, "y": 350, "z": 212},
            scale={"x": 1.85, "y": 0.08, "z": 1.12},
            rotation={"pitch": 0, "yaw": -7 if idx == 0 else 7, "roll": 0},
        )
        stand = spawn_mesh_actor(
            ue,
            spawned_actors,
            label=f"{prefix}_MonitorStand_{idx}",
            mesh_path=primitive_paths["cube"],
            material_path=palette["metal_dark"],
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
        material_path=palette["keyboard_black"],
        location={"x": -40, "y": 150, "z": 102},
        scale={"x": 1.95, "y": 0.52, "z": 0.06},
    )
    mouse = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_Mouse",
        mesh_path=primitive_paths["sphere"],
        material_path=palette["keyboard_black"],
        location={"x": 190, "y": 145, "z": 103},
        scale={"x": 0.25, "y": 0.32, "z": 0.11},
    )
    print("spawned:", keyboard, mouse)

    pc_tower = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_Tower",
        mesh_path=primitive_paths["cube"],
        material_path=palette["tower_magenta"],
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
            material_path=palette["speaker_cyan"],
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
        material_path=palette["chair_red"],
        location={"x": 0, "y": -70, "z": 50},
        scale={"x": 1.9, "y": 1.6, "z": 0.2},
    )
    chair_back = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_ChairBack",
        mesh_path=primitive_paths["cube"],
        material_path=palette["chair_red"],
        location={"x": 0, "y": -160, "z": 128},
        scale={"x": 1.9, "y": 0.22, "z": 1.25},
    )
    chair_base = spawn_mesh_actor(
        ue,
        spawned_actors,
        label=f"{prefix}_ChairBase",
        mesh_path=primitive_paths["sphere"],
        material_path=palette["metal_dark"],
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
            material_path=palette["panel_neon"],
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

    retry_call(ue.set_property, key_light["label"], "PointLightComponent0.Intensity", 3200)
    retry_call(ue.set_property, fill_light["label"], "PointLightComponent0.Intensity", 4200)
    retry_call(ue.set_property, monitor_glow_l["label"], "PointLightComponent0.Intensity", 2500)
    retry_call(ue.set_property, monitor_glow_r["label"], "PointLightComponent0.Intensity", 2500)
    retry_call(ue.set_property, rim_light["label"], "PointLightComponent0.Intensity", 3200)
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
        pause(0.10)

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
