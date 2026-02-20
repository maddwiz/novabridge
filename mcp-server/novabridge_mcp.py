#!/usr/bin/env python3
"""NovaBridge MCP server.

Requires:
  pip install mcp
"""

from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Any, Dict, Optional


SDK_DIR = Path(__file__).resolve().parents[1] / "python-sdk"
if str(SDK_DIR) not in sys.path:
    sys.path.insert(0, str(SDK_DIR))

from novabridge import NovaBridge, NovaBridgeError  # noqa: E402

try:
    from mcp.server.fastmcp import FastMCP
except Exception as exc:  # pragma: no cover - runtime dependency check
    raise SystemExit(
        "Missing dependency: mcp\n"
        "Install with: pip install mcp\n"
        f"Import error: {exc}"
    )


HOST = os.environ.get("NOVABRIDGE_HOST", "localhost")
PORT = int(os.environ.get("NOVABRIDGE_PORT", "30010"))
API_KEY = os.environ.get("NOVABRIDGE_API_KEY")

mcp = FastMCP("novabridge")
client = NovaBridge(host=HOST, port=PORT, api_key=API_KEY)


def _wrap(callable_):
    try:
        return callable_()
    except NovaBridgeError as exc:
        return {"status": "error", "error": str(exc)}
    except Exception as exc:  # pragma: no cover - defensive
        return {"status": "error", "error": f"Unexpected error: {exc}"}


@mcp.tool()
def ue5_health() -> Dict[str, Any]:
    """Check whether UE5 + NovaBridge are available."""
    return _wrap(client.health)


@mcp.tool()
def ue5_project_info() -> Dict[str, Any]:
    """Get currently loaded UE5 project details."""
    return _wrap(client.project_info)


@mcp.tool()
def ue5_scene_list() -> Dict[str, Any]:
    """List actors in the current level."""
    return _wrap(client.scene_list)


@mcp.tool()
def ue5_spawn(
    actor_class: str,
    label: Optional[str] = None,
    x: float = 0.0,
    y: float = 0.0,
    z: float = 0.0,
    pitch: float = 0.0,
    yaw: float = 0.0,
    roll: float = 0.0,
) -> Dict[str, Any]:
    """Spawn an actor (PointLight, StaticMeshActor, CameraActor, etc.)."""
    return _wrap(
        lambda: client.spawn(
            actor_class,
            label=label,
            x=x,
            y=y,
            z=z,
            pitch=pitch,
            yaw=yaw,
            roll=roll,
        )
    )


@mcp.tool()
def ue5_transform(
    name: str,
    location: Optional[Dict[str, float]] = None,
    rotation: Optional[Dict[str, float]] = None,
    scale: Optional[Dict[str, float]] = None,
) -> Dict[str, Any]:
    """Set actor transform fields."""
    return _wrap(lambda: client.transform(name, location=location, rotation=rotation, scale=scale))


@mcp.tool()
def ue5_delete(name: str) -> Dict[str, Any]:
    """Delete actor by name/label."""
    return _wrap(lambda: client.delete_actor(name))


@mcp.tool()
def ue5_get_actor(name: str) -> Dict[str, Any]:
    """Get actor properties/components."""
    return _wrap(lambda: client.get_actor(name))


@mcp.tool()
def ue5_set_property(name: str, prop: str, value: str) -> Dict[str, Any]:
    """Set actor/component property."""
    return _wrap(lambda: client.set_property(name, prop, value))


@mcp.tool()
def ue5_import_asset(
    file_path: str,
    asset_name: Optional[str] = None,
    destination: str = "/Game",
    scale: Optional[float] = None,
) -> Dict[str, Any]:
    """Import OBJ/FBX file into UE5 Content Browser."""
    return _wrap(
        lambda: client.import_asset(
            file_path,
            asset_name=asset_name,
            destination=destination,
            scale=scale,
        )
    )


@mcp.tool()
def ue5_screenshot(
    save_path: Optional[str] = None,
    width: Optional[int] = None,
    height: Optional[int] = None,
    raw: bool = False,
) -> Dict[str, Any]:
    """Capture viewport screenshot."""
    return _wrap(lambda: client.viewport_screenshot(save_path=save_path, width=width, height=height, raw=raw))


@mcp.tool()
def ue5_set_camera(
    location: Optional[Dict[str, float]] = None,
    rotation: Optional[Dict[str, float]] = None,
    fov: Optional[float] = None,
    show_flags: Optional[Dict[str, bool]] = None,
) -> Dict[str, Any]:
    """Set viewport camera and optional SceneCapture show flags."""
    return _wrap(lambda: client.set_camera(location=location, rotation=rotation, fov=fov, show_flags=show_flags))


@mcp.tool()
def ue5_exec(command: str) -> Dict[str, Any]:
    """Execute UE5 console command."""
    return _wrap(lambda: client.exec_command(command))


@mcp.tool()
def ue5_stream_start() -> Dict[str, Any]:
    """Start live WebSocket viewport stream."""
    return _wrap(client.stream_start)


@mcp.tool()
def ue5_stream_stop() -> Dict[str, Any]:
    """Stop live WebSocket viewport stream."""
    return _wrap(client.stream_stop)


@mcp.tool()
def ue5_stream_config(fps: int = 10, width: int = 640, height: int = 360, quality: int = 50) -> Dict[str, Any]:
    """Configure stream settings (fps, resolution, jpeg quality)."""
    return _wrap(lambda: client.stream_config(fps=fps, width=width, height=height, quality=quality))


@mcp.tool()
def ue5_stream_status() -> Dict[str, Any]:
    """Get stream status and ws:// endpoint."""
    return _wrap(client.stream_status)


@mcp.tool()
def ue5_pcg_list_graphs() -> Dict[str, Any]:
    """List available PCG graph assets."""
    return _wrap(client.pcg_list_graphs)


@mcp.tool()
def ue5_pcg_create_volume(
    graph_path: str,
    x: float = 0.0,
    y: float = 0.0,
    z: float = 0.0,
    size_x: float = 5000.0,
    size_y: float = 5000.0,
    size_z: float = 1000.0,
    label: Optional[str] = None,
) -> Dict[str, Any]:
    """Create a PCG volume and bind a graph."""
    return _wrap(
        lambda: client.pcg_create_volume(
            graph_path=graph_path,
            x=x,
            y=y,
            z=z,
            size_x=size_x,
            size_y=size_y,
            size_z=size_z,
            label=label,
        )
    )


@mcp.tool()
def ue5_pcg_generate(actor_name: str, seed: Optional[int] = None, force_regenerate: bool = True) -> Dict[str, Any]:
    """Trigger PCG generation on a volume/component actor."""
    return _wrap(lambda: client.pcg_generate(actor_name, seed=seed, force_regenerate=force_regenerate))


@mcp.tool()
def ue5_pcg_set_param(actor_name: str, param_name: str, value: Any, param_type: Optional[str] = None) -> Dict[str, Any]:
    """Set supported PCG parameter overrides (seed/activated)."""
    return _wrap(lambda: client.pcg_set_param(actor_name, param_name, value=value, param_type=param_type))


@mcp.tool()
def ue5_pcg_cleanup(actor_name: str) -> Dict[str, Any]:
    """Cleanup generated PCG output for an actor."""
    return _wrap(lambda: client.pcg_cleanup(actor_name))


@mcp.tool()
def ue5_sequencer_create(
    name: str,
    path: str = "/Game",
    duration_seconds: float = 10.0,
    fps: int = 30,
) -> Dict[str, Any]:
    """Create a level sequence asset."""
    return _wrap(lambda: client.sequencer_create(name=name, path=path, duration_seconds=duration_seconds, fps=fps))


@mcp.tool()
def ue5_sequencer_add_track(sequence: str, actor_name: str, track_type: str = "transform") -> Dict[str, Any]:
    """Bind actor and add a sequencer track."""
    return _wrap(lambda: client.sequencer_add_track(sequence=sequence, actor_name=actor_name, track_type=track_type))


@mcp.tool()
def ue5_sequencer_set_keyframe(
    sequence: str,
    actor_name: str,
    time: float,
    track_type: str = "transform",
    location: Optional[Dict[str, float]] = None,
    rotation: Optional[Dict[str, float]] = None,
    scale: Optional[Dict[str, float]] = None,
) -> Dict[str, Any]:
    """Set keyframe for a sequencer track."""
    return _wrap(
        lambda: client.sequencer_set_keyframe(
            sequence=sequence,
            actor_name=actor_name,
            time=time,
            track_type=track_type,
            location=location,
            rotation=rotation,
            scale=scale,
        )
    )


@mcp.tool()
def ue5_sequencer_play(sequence: str, loop: bool = False, start_time: float = 0.0) -> Dict[str, Any]:
    """Play a sequence."""
    return _wrap(lambda: client.sequencer_play(sequence, loop=loop, start_time=start_time))


@mcp.tool()
def ue5_sequencer_stop(sequence: Optional[str] = None) -> Dict[str, Any]:
    """Stop one sequence (or all players if sequence omitted)."""
    return _wrap(lambda: client.sequencer_stop(sequence))


@mcp.tool()
def ue5_sequencer_scrub(sequence: str, time: float) -> Dict[str, Any]:
    """Scrub a sequence to a specific time."""
    return _wrap(lambda: client.sequencer_scrub(sequence=sequence, time=time))


@mcp.tool()
def ue5_sequencer_render(sequence: str, output_path: Optional[str] = None, fps: int = 24, duration_seconds: float = 5.0) -> Dict[str, Any]:
    """Render sequence to PNG frame sequence."""
    return _wrap(
        lambda: client.sequencer_render(
            sequence=sequence,
            output_path=output_path,
            fps=fps,
            duration_seconds=duration_seconds,
        )
    )


@mcp.tool()
def ue5_sequencer_info() -> Dict[str, Any]:
    """Get active sequencer players and current times."""
    return _wrap(client.sequencer_info)


@mcp.tool()
def ue5_optimize_nanite(mesh_path: Optional[str] = None, actor_name: Optional[str] = None, enable: bool = True) -> Dict[str, Any]:
    """Enable/disable Nanite on a static mesh."""
    return _wrap(lambda: client.optimize_nanite(mesh_path=mesh_path, actor_name=actor_name, enable=enable))


@mcp.tool()
def ue5_optimize_lod(mesh_path: str, num_lods: int = 4, reduction_per_level: float = 0.5) -> Dict[str, Any]:
    """Generate LODs on a static mesh."""
    return _wrap(lambda: client.optimize_lod(mesh_path=mesh_path, num_lods=num_lods, reduction_per_level=reduction_per_level))


@mcp.tool()
def ue5_optimize_lumen(enabled: bool = True, quality: str = "high") -> Dict[str, Any]:
    """Configure Lumen GI/reflection settings."""
    return _wrap(lambda: client.optimize_lumen(enabled=enabled, quality=quality))


@mcp.tool()
def ue5_optimize_stats() -> Dict[str, Any]:
    """Collect scene optimization metrics."""
    return _wrap(client.optimize_stats)


@mcp.tool()
def ue5_optimize_textures(path: str = "/Game", max_size: int = 2048, compression: str = "default") -> Dict[str, Any]:
    """Bulk optimize texture max size/compression."""
    return _wrap(lambda: client.optimize_textures(path=path, max_size=max_size, compression=compression))


@mcp.tool()
def ue5_optimize_collision(mesh_path: Optional[str] = None, actor_name: Optional[str] = None, type: str = "complex") -> Dict[str, Any]:
    """Generate/update collision settings for a mesh."""
    return _wrap(lambda: client.optimize_collision(mesh_path=mesh_path, actor_name=actor_name, type=type))


if __name__ == "__main__":
    mcp.run()
