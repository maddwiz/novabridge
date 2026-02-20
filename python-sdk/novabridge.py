"""NovaBridge Python SDK.

Zero-dependency HTTP client for controlling the NovaBridge UE5 plugin.
"""

from __future__ import annotations

import base64
import json
import os
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Any, Dict, Optional


class NovaBridgeError(RuntimeError):
    """Raised when NovaBridge returns an HTTP or protocol error."""


@dataclass
class NovaBridge:
    host: str = "localhost"
    port: int = 30010
    timeout: int = 60
    api_key: Optional[str] = None

    @property
    def base_url(self) -> str:
        return f"http://{self.host}:{self.port}/nova"

    def _request(self, method: str, route: str, data: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        body = None
        headers = {}
        if data is not None:
            body = json.dumps(data).encode("utf-8")
            headers["Content-Type"] = "application/json"
            headers["Content-Length"] = str(len(body))
        if self.api_key:
            headers["X-API-Key"] = self.api_key

        req = urllib.request.Request(
            f"{self.base_url}{route}",
            data=body,
            headers=headers,
            method=method,
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                payload = resp.read()
                if not payload:
                    return {"status": "ok"}
                return json.loads(payload)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise NovaBridgeError(f"HTTP {exc.code}: {detail}") from exc
        except urllib.error.URLError as exc:
            raise NovaBridgeError(f"Connection failed: {exc.reason}") from exc
        except json.JSONDecodeError as exc:
            raise NovaBridgeError(f"Invalid JSON response: {exc}") from exc

    def _get(self, route: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        if params:
            route = f"{route}?{urllib.parse.urlencode(params)}"
        return self._request("GET", route)

    def _post(self, route: str, data: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        return self._request("POST", route, data or {})

    def health(self) -> Dict[str, Any]:
        return self._get("/health")

    def project_info(self) -> Dict[str, Any]:
        return self._get("/project/info")

    def scene_list(self) -> Dict[str, Any]:
        return self._get("/scene/list")

    def spawn(
        self,
        actor_class: str,
        *,
        x: float = 0.0,
        y: float = 0.0,
        z: float = 0.0,
        pitch: float = 0.0,
        yaw: float = 0.0,
        roll: float = 0.0,
        label: Optional[str] = None,
    ) -> Dict[str, Any]:
        data: Dict[str, Any] = {
            "class": actor_class,
            "x": x,
            "y": y,
            "z": z,
            "pitch": pitch,
            "yaw": yaw,
            "roll": roll,
        }
        if label:
            data["label"] = label
        return self._post("/scene/spawn", data)

    def delete_actor(self, name: str) -> Dict[str, Any]:
        return self._post("/scene/delete", {"name": name})

    def transform(
        self,
        name: str,
        *,
        location: Optional[Dict[str, float]] = None,
        rotation: Optional[Dict[str, float]] = None,
        scale: Optional[Dict[str, float]] = None,
    ) -> Dict[str, Any]:
        data: Dict[str, Any] = {"name": name}
        if location is not None:
            data["location"] = location
        if rotation is not None:
            data["rotation"] = rotation
        if scale is not None:
            data["scale"] = scale
        return self._post("/scene/transform", data)

    def get_actor(self, name: str) -> Dict[str, Any]:
        return self._post("/scene/get", {"name": name})

    def set_property(self, name: str, prop: str, value: Any) -> Dict[str, Any]:
        return self._post("/scene/set-property", {"name": name, "property": prop, "value": str(value)})

    def import_asset(
        self,
        file_path: str,
        *,
        asset_name: Optional[str] = None,
        destination: str = "/Game",
        scale: Optional[float] = None,
    ) -> Dict[str, Any]:
        data: Dict[str, Any] = {
            "file_path": file_path,
            "destination": destination,
        }
        if asset_name:
            data["asset_name"] = asset_name
        if scale is not None:
            data["scale"] = float(scale)
        return self._post("/asset/import", data)

    def viewport_screenshot(
        self,
        *,
        width: Optional[int] = None,
        height: Optional[int] = None,
        save_path: Optional[str] = None,
        raw: bool = False,
    ) -> Dict[str, Any]:
        params: Dict[str, Any] = {}
        if width:
            params["width"] = int(width)
        if height:
            params["height"] = int(height)
        if raw:
            params["format"] = "raw"

        if raw:
            route = "/viewport/screenshot"
            if params:
                route += f"?{urllib.parse.urlencode(params)}"
            req = urllib.request.Request(f"{self.base_url}{route}", method="GET")
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                data = resp.read()
                if save_path:
                    with open(save_path, "wb") as out:
                        out.write(data)
                return {
                    "status": "ok",
                    "format": "png",
                    "bytes": len(data),
                    "saved_to": save_path,
                }

        result = self._get("/viewport/screenshot", params or None)
        if save_path and "image" in result:
            with open(save_path, "wb") as out:
                out.write(base64.b64decode(result["image"]))
            result["saved_to"] = save_path
        return result

    def set_camera(
        self,
        *,
        location: Optional[Dict[str, float]] = None,
        rotation: Optional[Dict[str, float]] = None,
        fov: Optional[float] = None,
        show_flags: Optional[Dict[str, bool]] = None,
    ) -> Dict[str, Any]:
        data: Dict[str, Any] = {}
        if location is not None:
            data["location"] = location
        if rotation is not None:
            data["rotation"] = rotation
        if fov is not None:
            data["fov"] = float(fov)
        if show_flags is not None:
            data["show_flags"] = show_flags
        return self._post("/viewport/camera/set", data)

    def get_camera(self) -> Dict[str, Any]:
        return self._get("/viewport/camera/get")

    def create_material(self, name: str, *, path: str = "/Game", color: Optional[Dict[str, float]] = None) -> Dict[str, Any]:
        data: Dict[str, Any] = {"name": name, "path": path}
        if color is not None:
            data["color"] = color
        return self._post("/material/create", data)

    def exec_command(self, command: str) -> Dict[str, Any]:
        return self._post("/exec/command", {"command": command})

    # Stream
    def stream_start(self) -> Dict[str, Any]:
        return self._post("/stream/start", {})

    def stream_stop(self) -> Dict[str, Any]:
        return self._post("/stream/stop", {})

    def stream_config(
        self,
        *,
        fps: int = 10,
        width: int = 640,
        height: int = 360,
        quality: int = 50,
    ) -> Dict[str, Any]:
        return self._post(
            "/stream/config",
            {"fps": int(fps), "width": int(width), "height": int(height), "quality": int(quality)},
        )

    def stream_status(self) -> Dict[str, Any]:
        return self._get("/stream/status")

    # PCG
    def pcg_list_graphs(self) -> Dict[str, Any]:
        return self._get("/pcg/list-graphs")

    def pcg_create_volume(
        self,
        *,
        graph_path: str,
        x: float = 0.0,
        y: float = 0.0,
        z: float = 0.0,
        size_x: float = 5000.0,
        size_y: float = 5000.0,
        size_z: float = 1000.0,
        label: Optional[str] = None,
    ) -> Dict[str, Any]:
        data: Dict[str, Any] = {
            "graph_path": graph_path,
            "x": x,
            "y": y,
            "z": z,
            "size_x": size_x,
            "size_y": size_y,
            "size_z": size_z,
        }
        if label:
            data["label"] = label
        return self._post("/pcg/create-volume", data)

    def pcg_generate(self, actor_name: str, *, seed: Optional[int] = None, force_regenerate: bool = True) -> Dict[str, Any]:
        data: Dict[str, Any] = {"actor_name": actor_name, "force_regenerate": force_regenerate}
        if seed is not None:
            data["seed"] = int(seed)
        return self._post("/pcg/generate", data)

    def pcg_set_param(
        self,
        actor_name: str,
        param_name: str,
        *,
        value: Any,
        param_type: Optional[str] = None,
    ) -> Dict[str, Any]:
        data: Dict[str, Any] = {"actor_name": actor_name, "param_name": param_name, "value": value}
        if param_type:
            data["param_type"] = param_type
        return self._post("/pcg/set-param", data)

    def pcg_cleanup(self, actor_name: str) -> Dict[str, Any]:
        return self._post("/pcg/cleanup", {"actor_name": actor_name})

    # Sequencer
    def sequencer_create(
        self,
        *,
        name: str,
        path: str = "/Game",
        duration_seconds: float = 10.0,
        fps: int = 30,
    ) -> Dict[str, Any]:
        return self._post(
            "/sequencer/create",
            {"name": name, "path": path, "duration_seconds": duration_seconds, "fps": int(fps)},
        )

    def sequencer_add_track(
        self,
        *,
        sequence: str,
        actor_name: str,
        track_type: str = "transform",
    ) -> Dict[str, Any]:
        return self._post(
            "/sequencer/add-track",
            {"sequence": sequence, "actor_name": actor_name, "track_type": track_type},
        )

    def sequencer_set_keyframe(
        self,
        *,
        sequence: str,
        actor_name: str,
        time: float,
        track_type: str = "transform",
        location: Optional[Dict[str, float]] = None,
        rotation: Optional[Dict[str, float]] = None,
        scale: Optional[Dict[str, float]] = None,
    ) -> Dict[str, Any]:
        data: Dict[str, Any] = {
            "sequence": sequence,
            "actor_name": actor_name,
            "time": float(time),
            "track_type": track_type,
        }
        if location is not None:
            data["location"] = location
        if rotation is not None:
            data["rotation"] = rotation
        if scale is not None:
            data["scale"] = scale
        return self._post("/sequencer/set-keyframe", data)

    def sequencer_play(self, sequence: str, *, loop: bool = False, start_time: float = 0.0) -> Dict[str, Any]:
        return self._post("/sequencer/play", {"sequence": sequence, "loop": loop, "start_time": start_time})

    def sequencer_stop(self, sequence: Optional[str] = None) -> Dict[str, Any]:
        if sequence:
            return self._post("/sequencer/stop", {"sequence": sequence})
        return self._post("/sequencer/stop", {})

    def sequencer_scrub(self, *, sequence: str, time: float) -> Dict[str, Any]:
        return self._post("/sequencer/scrub", {"sequence": sequence, "time": float(time)})

    def sequencer_render(
        self,
        *,
        sequence: str,
        output_path: Optional[str] = None,
        fps: int = 24,
        duration_seconds: float = 5.0,
    ) -> Dict[str, Any]:
        data: Dict[str, Any] = {"sequence": sequence, "fps": int(fps), "duration_seconds": float(duration_seconds)}
        if output_path:
            data["output_path"] = output_path
        return self._post("/sequencer/render", data)

    def sequencer_info(self) -> Dict[str, Any]:
        return self._get("/sequencer/info")

    # Optimize
    def optimize_nanite(
        self,
        *,
        mesh_path: Optional[str] = None,
        actor_name: Optional[str] = None,
        enable: bool = True,
    ) -> Dict[str, Any]:
        data: Dict[str, Any] = {"enable": enable}
        if mesh_path:
            data["mesh_path"] = mesh_path
        if actor_name:
            data["actor_name"] = actor_name
        return self._post("/optimize/nanite", data)

    def optimize_lod(self, *, mesh_path: str, num_lods: int = 4, reduction_per_level: float = 0.5) -> Dict[str, Any]:
        return self._post(
            "/optimize/lod",
            {"mesh_path": mesh_path, "num_lods": int(num_lods), "reduction_per_level": float(reduction_per_level)},
        )

    def optimize_lumen(self, *, enabled: bool = True, quality: str = "high") -> Dict[str, Any]:
        return self._post("/optimize/lumen", {"enabled": enabled, "quality": quality})

    def optimize_stats(self) -> Dict[str, Any]:
        return self._get("/optimize/stats")

    def optimize_textures(self, *, path: str = "/Game", max_size: int = 2048, compression: str = "default") -> Dict[str, Any]:
        return self._post(
            "/optimize/textures",
            {"path": path, "max_size": int(max_size), "compression": compression},
        )

    def optimize_collision(
        self,
        *,
        mesh_path: Optional[str] = None,
        actor_name: Optional[str] = None,
        type: str = "complex",
    ) -> Dict[str, Any]:
        data: Dict[str, Any] = {"type": type}
        if mesh_path:
            data["mesh_path"] = mesh_path
        if actor_name:
            data["actor_name"] = actor_name
        return self._post("/optimize/collision", data)

    def ai_generate_3d(
        self,
        prompt: str,
        *,
        provider: str = "meshy",
        asset_name: Optional[str] = None,
        style: str = "realistic",
        import_to_ue5: bool = True,
        server_url: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Call an external AI-generation sidecar and optionally auto-import to UE5.

        Sidecar default URL: NOVABRIDGE_AI_GEN_URL or http://localhost:30014
        """
        target = (server_url or os.environ.get("NOVABRIDGE_AI_GEN_URL") or "http://localhost:30014").rstrip("/")
        body = {
            "prompt": prompt,
            "provider": provider,
            "asset_name": asset_name,
            "style": style,
            "import_to_ue5": import_to_ue5,
        }
        req = urllib.request.Request(
            f"{target}/ai/generate",
            data=json.dumps(body).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                payload = resp.read()
                return json.loads(payload or b"{}")
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise NovaBridgeError(f"AI generate HTTP {exc.code}: {detail}") from exc
        except urllib.error.URLError as exc:
            raise NovaBridgeError(f"AI generate connection failed: {exc.reason}") from exc
