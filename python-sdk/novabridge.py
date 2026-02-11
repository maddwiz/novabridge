"""NovaBridge Python SDK.

Zero-dependency HTTP client for controlling the NovaBridge UE5 plugin.
"""

from __future__ import annotations

import base64
import json
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
