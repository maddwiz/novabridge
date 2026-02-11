#!/usr/bin/env python3
"""Spawn a light and capture a screenshot."""

import sys
from pathlib import Path

SDK_DIR = Path(__file__).resolve().parents[2] / "python-sdk"
sys.path.insert(0, str(SDK_DIR))

from novabridge import NovaBridge  # noqa: E402


ue5 = NovaBridge()
print("health:", ue5.health())
print("project:", ue5.project_info())

ue5.spawn("PointLight", label="HelloLight", x=0, y=0, z=300)
shot = ue5.viewport_screenshot(save_path="hello_world.png")
print("screenshot:", shot)
