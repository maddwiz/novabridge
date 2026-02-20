#!/usr/bin/env python3
"""NovaBridge AI 3D generation sidecar (Meshy-first)."""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

import requests
from flask import Flask, jsonify, request

SDK_DIR = Path(__file__).resolve().parents[1] / "python-sdk"
if str(SDK_DIR) not in sys.path:
    sys.path.insert(0, str(SDK_DIR))

from novabridge import NovaBridge  # noqa: E402

app = Flask(__name__)

PORT = int(os.environ.get("NOVABRIDGE_AI_GEN_PORT", "30014"))
EXPORT_DIR = Path(os.environ.get("NOVABRIDGE_EXPORT_DIR", "/tmp/novabridge-exports"))
EXPORT_DIR.mkdir(parents=True, exist_ok=True)

nb = NovaBridge(
    host=os.environ.get("NOVABRIDGE_HOST", "localhost"),
    port=int(os.environ.get("NOVABRIDGE_PORT", "30010")),
    api_key=os.environ.get("NOVABRIDGE_API_KEY"),
)


def download_file(url: str, dest: Path):
    with requests.get(url, timeout=120, stream=True) as r:
        r.raise_for_status()
        with open(dest, "wb") as f:
            for chunk in r.iter_content(chunk_size=1024 * 64):
                if chunk:
                    f.write(chunk)


def meshy_generate(prompt: str, style: str = "realistic") -> dict[str, Any]:
    api_key = os.environ.get("MESHY_API_KEY", "")
    if not api_key:
        raise RuntimeError("MESHY_API_KEY is required for provider=meshy")

    created = requests.post(
        "https://api.meshy.ai/v2/text-to-3d",
        headers={"Authorization": f"Bearer {api_key}"},
        json={"mode": "preview", "prompt": prompt, "art_style": style},
        timeout=60,
    )
    created.raise_for_status()
    payload = created.json()
    task_id = payload.get("result") or payload.get("id") or payload.get("task_id")
    if not task_id:
        raise RuntimeError(f"Meshy did not return task id: {payload}")

    result = None
    for _ in range(60):
        time.sleep(5)
        poll = requests.get(
            f"https://api.meshy.ai/v2/text-to-3d/{task_id}",
            headers={"Authorization": f"Bearer {api_key}"},
            timeout=60,
        )
        poll.raise_for_status()
        result = poll.json()
        status = (result.get("status") or "").upper()
        if status in {"SUCCEEDED", "FAILED"}:
            break

    if not result or (result.get("status") or "").upper() != "SUCCEEDED":
        raise RuntimeError(f"Meshy task failed: {result}")

    model_urls = result.get("model_urls", {})
    model_url = model_urls.get("glb") or model_urls.get("obj") or model_urls.get("fbx")
    if not model_url:
        raise RuntimeError(f"Meshy returned no model url: {result}")
    return {"task_id": task_id, "result": result, "model_url": model_url}


@app.get("/ai/health")
def health():
    return jsonify({"status": "ok", "provider_default": "meshy", "export_dir": str(EXPORT_DIR), "port": PORT})


@app.post("/ai/generate")
def generate():
    body = request.get_json(silent=True) or {}
    prompt = str(body.get("prompt", "")).strip()
    provider = str(body.get("provider", "meshy")).lower()
    style = str(body.get("style", "realistic"))
    asset_name = str(body.get("asset_name") or f"ai_model_{int(time.time())}")
    import_to_ue5 = bool(body.get("import_to_ue5", True))

    if not prompt:
        return jsonify({"status": "error", "error": "prompt is required"}), 400

    try:
        if provider != "meshy":
            return jsonify({"status": "error", "error": f"Provider '{provider}' not implemented yet. Supported: meshy"}), 400

        generated = meshy_generate(prompt, style=style)
        model_url = generated["model_url"]
        ext = Path(urlparse(model_url).path).suffix or ".glb"
        local_path = EXPORT_DIR / f"{asset_name}{ext}"
        download_file(model_url, local_path)

        result: dict[str, Any] = {
            "status": "ok",
            "provider": provider,
            "prompt": prompt,
            "file_path": str(local_path),
            "source_url": model_url,
            "task_id": generated["task_id"],
        }

        if import_to_ue5:
            # Note: OBJ is imported directly; non-OBJ conversion is expected via blender/openclaw toolchain.
            if local_path.suffix.lower() != ".obj":
                result["note"] = "Generated file is not OBJ. Convert to OBJ via blender_to_ue5 or nova-blender tool before import."
            else:
                import_result = nb.import_asset(str(local_path), asset_name=asset_name, destination="/Game", scale=100.0)
                result["ue5_import"] = import_result

        return jsonify(result)
    except Exception as exc:  # noqa: BLE001
        return jsonify({"status": "error", "error": str(exc)}), 500


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=PORT, debug=False)
