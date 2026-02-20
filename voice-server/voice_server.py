#!/usr/bin/env python3
"""NovaBridge Voice Command Server.

Provides speech-to-text transcription and optional command execution against NovaBridge.
"""

from __future__ import annotations

import json
import os
import re
import sys
import tempfile
from pathlib import Path
from typing import Any

from flask import Flask, jsonify, request

SDK_DIR = Path(__file__).resolve().parents[1] / "python-sdk"
if str(SDK_DIR) not in sys.path:
    sys.path.insert(0, str(SDK_DIR))

from novabridge import NovaBridge  # noqa: E402

app = Flask(__name__)

WHISPER_MODE = os.environ.get("NOVABRIDGE_WHISPER_MODE", "local").lower()
WHISPER_MODEL = os.environ.get("NOVABRIDGE_WHISPER_MODEL", "base")
OPENAI_API_KEY = os.environ.get("OPENAI_API_KEY", "")
VOICE_PORT = int(os.environ.get("NOVABRIDGE_VOICE_PORT", "30012"))

nb = NovaBridge(
    host=os.environ.get("NOVABRIDGE_HOST", "localhost"),
    port=int(os.environ.get("NOVABRIDGE_PORT", "30010")),
    api_key=os.environ.get("NOVABRIDGE_API_KEY"),
)

_whisper_model = None


def get_whisper_model():
    global _whisper_model
    if _whisper_model is None:
        import whisper  # type: ignore

        _whisper_model = whisper.load_model(WHISPER_MODEL)
    return _whisper_model


def transcribe_file(audio_path: str) -> str:
    if WHISPER_MODE == "api" and OPENAI_API_KEY:
        from openai import OpenAI  # type: ignore

        client = OpenAI(api_key=OPENAI_API_KEY)
        with open(audio_path, "rb") as f:
            result = client.audio.transcriptions.create(model="whisper-1", file=f)
        return (result.text or "").strip()

    model = get_whisper_model()
    result = model.transcribe(audio_path)
    return str(result.get("text", "")).strip()


def parse_voice_intent(text: str) -> list[dict[str, Any]]:
    text_lower = text.lower().strip()
    commands: list[dict[str, Any]] = []

    spawn_match = re.search(r"spawn\s+(\d+)?\s*([\w\s]+?)(?:\s+at\s+(.+))?$", text_lower)
    if spawn_match:
        count = int(spawn_match.group(1) or 1)
        actor_type = spawn_match.group(2).strip()
        class_map = {
            "light": "PointLight",
            "point light": "PointLight",
            "directional light": "DirectionalLight",
            "sun": "DirectionalLight",
            "spot light": "SpotLight",
            "spotlight": "SpotLight",
            "cube": "StaticMeshActor",
            "box": "StaticMeshActor",
            "camera": "CameraActor",
            "fog": "ExponentialHeightFog",
            "sky light": "SkyLight",
            "skylight": "SkyLight",
        }
        actor_class = class_map.get(actor_type, actor_type)
        for i in range(min(count, 50)):
            commands.append(
                {
                    "method": "POST",
                    "route": "/scene/spawn",
                    "body": {"class": actor_class, "x": i * 200, "y": 0, "z": 100},
                }
            )

    if "screenshot" in text_lower or "take a picture" in text_lower or "capture" in text_lower:
        commands.append({"method": "GET", "route": "/viewport/screenshot"})

    if "list" in text_lower and ("scene" in text_lower or "actors" in text_lower):
        commands.append({"method": "GET", "route": "/scene/list"})

    delete_match = re.search(r"delete\s+(.+)", text_lower)
    if delete_match:
        commands.append({"method": "POST", "route": "/scene/delete", "body": {"name": delete_match.group(1).strip()}})

    if "build lighting" in text_lower or "bake lighting" in text_lower:
        commands.append({"method": "POST", "route": "/build/lighting", "body": {}})

    if not commands:
        commands.append({"type": "unrecognized", "text": text})

    return commands


def execute_commands(commands: list[dict[str, Any]]) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for cmd in commands:
        if cmd.get("type") == "unrecognized":
            results.append({"status": "skipped", "reason": "Could not parse command"})
            continue
        try:
            result = nb._request(cmd["method"], cmd["route"], cmd.get("body"))  # noqa: SLF001
            results.append(result)
        except Exception as exc:  # noqa: BLE001
            results.append({"status": "error", "error": str(exc)})
    return results


def extract_audio_or_text() -> tuple[str | None, str | None, tempfile.NamedTemporaryFile | None]:
    audio_path: str | None = None
    text: str | None = None
    tmp_file: tempfile.NamedTemporaryFile | None = None

    if "audio" in request.files:
        audio_file = request.files["audio"]
        tmp_file = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
        audio_file.save(tmp_file.name)
        audio_path = tmp_file.name
    else:
        data = request.get_json(silent=True) or {}
        audio_path = data.get("audio_path")
        text = data.get("text")

    return audio_path, text, tmp_file


@app.get("/voice/health")
def health():
    return jsonify(
        {
            "status": "ok",
            "whisper_mode": WHISPER_MODE,
            "whisper_model": WHISPER_MODEL,
            "novabridge_host": nb.host,
            "novabridge_port": nb.port,
        }
    )


@app.post("/voice/transcribe")
def transcribe():
    audio_path, text, tmp_file = extract_audio_or_text()
    del text
    try:
        if not audio_path or not os.path.exists(audio_path):
            return jsonify({"status": "error", "error": "No audio provided"}), 400
        transcript = transcribe_file(audio_path)
        return jsonify({"status": "ok", "text": transcript, "mode": WHISPER_MODE})
    finally:
        if tmp_file:
            try:
                os.unlink(tmp_file.name)
            except OSError:
                pass


@app.post("/voice/command")
def voice_command():
    audio_path, text, tmp_file = extract_audio_or_text()
    try:
        if text:
            commands = parse_voice_intent(text)
            results = execute_commands(commands)
            return jsonify({"status": "ok", "text": text, "commands": commands, "results": results})

        if not audio_path or not os.path.exists(audio_path):
            return jsonify({"status": "error", "error": "No audio or text provided"}), 400

        transcript = transcribe_file(audio_path)
        commands = parse_voice_intent(transcript)
        results = execute_commands(commands)
        return jsonify({"status": "ok", "text": transcript, "commands": commands, "results": results})
    finally:
        if tmp_file:
            try:
                os.unlink(tmp_file.name)
            except OSError:
                pass


if __name__ == "__main__":
    print(json.dumps({"status": "starting", "port": VOICE_PORT, "mode": WHISPER_MODE, "model": WHISPER_MODEL}))
    app.run(host="0.0.0.0", port=VOICE_PORT, debug=False)
