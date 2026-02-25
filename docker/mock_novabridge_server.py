#!/usr/bin/env python3
"""NovaBridge container mock server.

Provides a lightweight /nova API surface for SDK and integration testing in
environments where Unreal Engine is not available.
"""

from __future__ import annotations

import base64
import json
import os
import time
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, List, Optional
from urllib.parse import parse_qs, urlparse


PNG_1X1 = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMBAF8W5m8AAAAASUVORK5CYII="
)


@dataclass
class MockState:
    actors: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    audit: List[Dict[str, Any]] = field(default_factory=list)
    actor_counter: int = 0

    def next_actor_name(self, prefix: str = "Actor") -> str:
        self.actor_counter += 1
        return f"{prefix}_{self.actor_counter}"


STATE = MockState()
DEFAULT_MODE = os.environ.get("NOVABRIDGE_DOCKER_MODE", "editor")
VERSION = os.environ.get("NOVABRIDGE_VERSION", "1.0.0")
REQUIRED_API_KEY = os.environ.get("NOVABRIDGE_API_KEY", "").strip()


def _json_bytes(payload: Dict[str, Any]) -> bytes:
    return json.dumps(payload, separators=(",", ":")).encode("utf-8")


def _now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def _resolve_api_key(headers: BaseHTTPRequestHandler) -> str:
    api_key = headers.headers.get("X-API-Key", "").strip()
    if api_key:
        return api_key
    authorization = headers.headers.get("Authorization", "").strip()
    if authorization.lower().startswith("bearer "):
        return authorization[7:].strip()
    return ""


def _make_caps(mode: str) -> List[Dict[str, Any]]:
    return [
        {
            "action": "spawn",
            "roles": ["admin", "automation"],
            "allowedClasses": [
                "StaticMeshActor",
                "PointLight",
                "DirectionalLight",
                "SpotLight",
                "CameraActor",
            ],
            "bounds": {
                "min": {"x": -50000, "y": -50000, "z": -50000},
                "max": {"x": 50000, "y": 50000, "z": 50000},
            },
        },
        {"action": "delete", "roles": ["admin", "automation"]},
        {"action": "set", "roles": ["admin", "automation"]},
        {"action": "screenshot", "roles": ["admin", "automation", "read_only"]},
        {"action": "executePlan", "roles": ["admin", "automation"]},
        {"action": "undo", "roles": ["admin", "automation"]},
        {"action": "scene.list", "roles": ["admin", "automation", "read_only"]},
        {"action": "scene.get", "roles": ["admin", "automation", "read_only"]},
        {"action": "scene.set-property", "roles": ["admin", "automation"]},
        {"action": "viewport.camera.get", "roles": ["admin", "automation", "read_only"]},
        {"action": "viewport.camera.set", "roles": ["admin", "automation"]},
    ]


class Handler(BaseHTTPRequestHandler):
    server_version = "NovaBridgeMock/1.0"
    protocol_version = "HTTP/1.1"

    def _send_json(self, payload: Dict[str, Any], status: int = 200) -> None:
        body = _json_bytes(payload)
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-API-Key, X-NovaBridge-Role")
        self.end_headers()
        self.wfile.write(body)

    def _send_png(self, png_bytes: bytes) -> None:
        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(png_bytes)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(png_bytes)

    def _read_json(self) -> Optional[Dict[str, Any]]:
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length <= 0:
            return {}
        raw = self.rfile.read(length)
        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError:
            self._send_json({"status": "error", "error": "Invalid JSON body"}, status=400)
            return None
        if not isinstance(parsed, dict):
            self._send_json({"status": "error", "error": "JSON body must be an object"}, status=400)
            return None
        return parsed

    def _require_auth(self) -> bool:
        if not REQUIRED_API_KEY:
            return True
        provided = _resolve_api_key(self)
        if provided == REQUIRED_API_KEY:
            return True
        self._send_json(
            {
                "status": "error",
                "error": "Unauthorized. Provide X-API-Key or Authorization: Bearer <key>.",
            },
            status=401,
        )
        return False

    def _audit(self, route: str, action: str, status: str, message: str) -> None:
        STATE.audit.append(
            {
                "timestamp_utc": _now_iso(),
                "route": route,
                "action": action,
                "status": status,
                "message": message,
            }
        )
        if len(STATE.audit) > 500:
            STATE.audit = STATE.audit[-500:]

    def do_OPTIONS(self) -> None:  # noqa: N802
        self._send_json({"status": "ok"})

    def do_GET(self) -> None:  # noqa: N802
        if not self._require_auth():
            return

        parsed = urlparse(self.path)
        route = parsed.path
        query = parse_qs(parsed.query)

        if route == "/nova/health":
            payload = {
                "status": "ok",
                "version": VERSION,
                "mode": DEFAULT_MODE,
                "engine": "mock",
                "routes": 12,
                "project_name": "NovaBridgeDockerMock",
            }
            self._send_json(payload)
            return

        if route == "/nova/caps":
            role = self.headers.get("X-NovaBridge-Role", "automation") or "automation"
            self._send_json(
                {
                    "status": "ok",
                    "version": VERSION,
                    "mode": DEFAULT_MODE,
                    "role": role,
                    "capabilities": _make_caps(DEFAULT_MODE),
                }
            )
            return

        if route == "/nova/scene/list":
            self._send_json(
                {
                    "status": "ok",
                    "count": len(STATE.actors),
                    "actors": list(STATE.actors.values()),
                }
            )
            return

        if route == "/nova/viewport/screenshot":
            if query.get("format", [""])[0].lower() in {"raw", "png"}:
                self._send_png(PNG_1X1)
                return
            self._send_json(
                {
                    "status": "ok",
                    "format": "png",
                    "width": 1,
                    "height": 1,
                    "bytes": len(PNG_1X1),
                    "image": base64.b64encode(PNG_1X1).decode("ascii"),
                }
            )
            return

        if route == "/nova/events":
            self._send_json(
                {
                    "status": "ok",
                    "ws_url": "ws://localhost:30012",
                    "supported_types": ["audit", "spawn", "delete", "plan_step", "plan_complete", "error"],
                }
            )
            return

        if route == "/nova/audit":
            limit = 50
            raw_limit = query.get("limit", ["50"])[0]
            try:
                limit = max(1, min(500, int(raw_limit)))
            except ValueError:
                pass
            entries = STATE.audit[-limit:]
            self._send_json({"status": "ok", "count": len(entries), "entries": entries})
            return

        self._send_json({"status": "error", "error": f"Route not found: {route}"}, status=404)

    def do_POST(self) -> None:  # noqa: N802
        if not self._require_auth():
            return

        parsed = urlparse(self.path)
        route = parsed.path
        body = self._read_json()
        if body is None:
            return

        if route == "/nova/runtime/pair":
            role = body.get("role", "automation")
            self._send_json(
                {
                    "status": "ok",
                    "mode": "runtime",
                    "role": role,
                    "token": "mock-runtime-token",
                    "token_expires_utc": _now_iso(),
                }
            )
            return

        if route == "/nova/scene/spawn":
            actor_class = str(body.get("class") or body.get("type") or "StaticMeshActor")
            label = str(body.get("label") or STATE.next_actor_name(actor_class))
            actor_name = STATE.next_actor_name(label)
            actor = {
                "name": actor_name,
                "label": label,
                "class": actor_class,
                "location": {
                    "x": float(body.get("x", 0.0)),
                    "y": float(body.get("y", 0.0)),
                    "z": float(body.get("z", 0.0)),
                },
            }
            STATE.actors[actor_name] = actor
            self._audit(route, "scene.spawn", "success", f"Spawned {actor_name}")
            self._send_json({"status": "ok", **actor})
            return

        if route == "/nova/scene/delete":
            name = str(body.get("name") or body.get("target") or "")
            if not name:
                self._send_json({"status": "error", "error": "Missing actor name"}, status=400)
                return
            removed_name = None
            for actor_name, actor in list(STATE.actors.items()):
                if actor_name == name or actor.get("label") == name:
                    removed_name = actor_name
                    del STATE.actors[actor_name]
                    break
            if not removed_name:
                self._send_json({"status": "error", "error": f"Actor not found: {name}"}, status=404)
                return
            self._audit(route, "scene.delete", "success", f"Deleted {removed_name}")
            self._send_json({"status": "ok", "name": removed_name})
            return

        if route == "/nova/undo":
            self._send_json({"status": "ok", "message": "Mock undo completed"})
            return

        if route == "/nova/executePlan":
            steps = body.get("steps", [])
            if not isinstance(steps, list):
                self._send_json({"status": "error", "error": "steps must be an array"}, status=400)
                return
            results: List[Dict[str, Any]] = []
            for index, step in enumerate(steps):
                action = str((step or {}).get("action", "")).lower().strip()
                params = (step or {}).get("params") or {}
                if action == "spawn":
                    spawn_body = {
                        "class": params.get("class") or params.get("type") or "StaticMeshActor",
                        "label": params.get("label"),
                        "x": params.get("x", 0.0),
                        "y": params.get("y", 0.0),
                        "z": params.get("z", 0.0),
                    }
                    actor_class = str(spawn_body["class"])
                    label = str(spawn_body.get("label") or STATE.next_actor_name(actor_class))
                    actor_name = STATE.next_actor_name(label)
                    STATE.actors[actor_name] = {
                        "name": actor_name,
                        "label": label,
                        "class": actor_class,
                        "location": {
                            "x": float(spawn_body.get("x", 0.0)),
                            "y": float(spawn_body.get("y", 0.0)),
                            "z": float(spawn_body.get("z", 0.0)),
                        },
                    }
                    results.append({"step": index, "status": "success", "message": "Spawned actor", "object_id": actor_name})
                elif action == "delete":
                    name = str(params.get("name") or params.get("target") or "")
                    deleted = False
                    for actor_name, actor in list(STATE.actors.items()):
                        if actor_name == name or actor.get("label") == name:
                            del STATE.actors[actor_name]
                            deleted = True
                            break
                    if deleted:
                        results.append({"step": index, "status": "success", "message": f"Deleted {name}"})
                    else:
                        results.append({"step": index, "status": "error", "message": f"Actor not found: {name}"})
                elif action in {"set", "call", "screenshot"}:
                    results.append({"step": index, "status": "success", "message": f"Executed {action}"})
                else:
                    results.append({"step": index, "status": "error", "message": f"Unsupported action: {action}"})

            success_count = sum(1 for item in results if item.get("status") == "success")
            error_count = len(results) - success_count
            plan_id = str(body.get("plan_id") or f"mock-{int(time.time())}")
            self._audit(route, "executePlan", "success", f"Plan {plan_id} complete")
            self._send_json(
                {
                    "status": "ok",
                    "plan_id": plan_id,
                    "mode": DEFAULT_MODE,
                    "step_count": len(results),
                    "success_count": success_count,
                    "error_count": error_count,
                    "results": results,
                }
            )
            return

        self._send_json({"status": "error", "error": f"Route not found: {route}"}, status=404)

    def log_message(self, format: str, *args: Any) -> None:  # noqa: A003
        return


def main() -> None:
    host = os.environ.get("NOVABRIDGE_DOCKER_HOST", "0.0.0.0")
    port = int(os.environ.get("NOVABRIDGE_DOCKER_PORT", "8080"))
    server = ThreadingHTTPServer((host, port), Handler)
    print(f"[novabridge-mock] listening on {host}:{port} mode={DEFAULT_MODE} version={VERSION}")
    server.serve_forever()


if __name__ == "__main__":
    main()
