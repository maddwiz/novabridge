#!/usr/bin/env python3
"""Demonstrate SceneCapture show flag controls."""

import sys
from pathlib import Path

SDK_DIR = Path(__file__).resolve().parents[2] / "python-sdk"
sys.path.insert(0, str(SDK_DIR))

from novabridge import NovaBridge  # noqa: E402


ue5 = NovaBridge()
ue5.set_camera(
    location={"x": 300, "y": -300, "z": 220},
    rotation={"pitch": -20, "yaw": 45, "roll": 0},
    fov=60,
    show_flags={"Grid": False, "BSP": False},
)
result = ue5.viewport_screenshot(save_path="camera_clean.png")
print(result)
