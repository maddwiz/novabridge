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


if __name__ == "__main__":
    mcp.run()
