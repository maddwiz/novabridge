#!/usr/bin/env python3
"""NovaBridge Blender LiveLink sync server."""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path
from typing import Any

from flask import Flask, jsonify, request

SDK_DIR = Path(__file__).resolve().parents[1] / "python-sdk"
if str(SDK_DIR) not in sys.path:
    sys.path.insert(0, str(SDK_DIR))

from novabridge import NovaBridge  # noqa: E402

app = Flask(__name__)

PORT = int(os.environ.get("NOVABRIDGE_LIVELINK_PORT", "30013"))
TARGET_FPS = float(os.environ.get("NOVABRIDGE_LIVELINK_PULL_FPS", "8"))
POLL_INTERVAL = 1.0 / max(TARGET_FPS, 1.0)

nb = NovaBridge(
    host=os.environ.get("NOVABRIDGE_HOST", "localhost"),
    port=int(os.environ.get("NOVABRIDGE_PORT", "30010")),
    api_key=os.environ.get("NOVABRIDGE_API_KEY"),
)

pending_for_blender: list[dict[str, Any]] = []
pending_for_ue5: list[dict[str, Any]] = []
last_ue5_poll = 0.0
last_ue5_snapshot: dict[str, tuple[float, float, float]] = {}


def blender_to_ue5(loc: list[float]) -> dict[str, float]:
    # Blender meters -> UE cm and X/Y swap.
    return {"x": float(loc[1]) * 100.0, "y": float(loc[0]) * 100.0, "z": float(loc[2]) * 100.0}


def ue5_to_blender(loc: dict[str, Any]) -> list[float]:
    return [float(loc.get("y", 0.0)) / 100.0, float(loc.get("x", 0.0)) / 100.0, float(loc.get("z", 0.0)) / 100.0]


def poll_ue5_if_needed():
    global last_ue5_poll, last_ue5_snapshot
    now = time.time()
    if now - last_ue5_poll < POLL_INTERVAL:
        return
    last_ue5_poll = now

    try:
        result = nb.scene_list()
        actors = result.get("actors", [])
        changes: list[dict[str, Any]] = []
        next_snapshot: dict[str, tuple[float, float, float]] = {}
        for actor in actors:
            label = actor.get("label")
            tx = actor.get("transform", {})
            loc = tx.get("location", {})
            if not label or not isinstance(loc, dict):
                continue
            key = (float(loc.get("x", 0.0)), float(loc.get("y", 0.0)), float(loc.get("z", 0.0)))
            next_snapshot[label] = key
            if last_ue5_snapshot.get(label) != key:
                changes.append({"name": label, "location": ue5_to_blender(loc)})
        if changes:
            pending_for_blender.extend(changes)
        last_ue5_snapshot = next_snapshot
    except Exception:
        pass


@app.get("/sync/health")
def health():
    return jsonify({"status": "ok", "port": PORT, "pending_for_blender": len(pending_for_blender), "pending_for_ue5": len(pending_for_ue5)})


@app.post("/sync/push")
def push():
    payload = request.get_json(silent=True) or {}
    source = str(payload.get("source", "")).lower()
    changes = payload.get("changes", [])
    if not isinstance(changes, list):
        return jsonify({"status": "error", "error": "changes must be a list"}), 400

    if source == "blender":
        for change in changes:
            name = change.get("name")
            loc = change.get("location")
            if not name or not isinstance(loc, list) or len(loc) != 3:
                continue
            ue_loc = blender_to_ue5(loc)
            try:
                nb.transform(name, location=ue_loc)
            except Exception:
                # Actor may not exist in UE yet; queue for future extensions.
                pending_for_ue5.append({"name": name, "location": ue_loc})
    elif source == "ue5":
        pending_for_blender.extend(changes)
    else:
        return jsonify({"status": "error", "error": "source must be blender or ue5"}), 400

    return jsonify({"status": "ok", "accepted": len(changes)})


@app.get("/sync/pull")
def pull():
    target = str(request.args.get("target", "")).lower()
    if target not in {"blender", "ue5"}:
        return jsonify({"status": "error", "error": "target must be blender or ue5"}), 400

    poll_ue5_if_needed()

    if target == "blender":
        out = list(pending_for_blender)
        pending_for_blender.clear()
    else:
        out = list(pending_for_ue5)
        pending_for_ue5.clear()
    return jsonify({"status": "ok", "changes": out})


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=PORT, debug=False)
