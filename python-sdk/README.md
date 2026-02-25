# NovaBridge Python SDK

Zero-dependency Python client for NovaBridge.

## Usage

```python
from novabridge import NovaBridge

ue5 = NovaBridge(host="localhost", port=30010)
print(ue5.health())

ue5.spawn("PointLight", label="Sun", x=0, y=0, z=500)
ue5.viewport_screenshot(save_path="scene.png")

# Optional auth mode
secured = NovaBridge(host="localhost", port=30010, api_key="replace-with-secret")
print(secured.health())

# Optional role pinning
operator = NovaBridge(host="localhost", port=30010, role="automation")
print(operator.caps())
print(operator.events())  # WebSocket discovery for /nova/events

# Structured plan execution
plan = {
    "steps": [
        {"action": "spawn", "params": {"type": "PointLight", "label": "LaunchSmokeLight", "x": 0, "y": 0, "z": 260}},
        {"action": "set", "params": {"target": "LaunchSmokeLight", "props": {"PointLightComponent.Intensity": 50000}}},
    ]
}
print(operator.execute_plan(plan["steps"], plan_id="demo-plan"))

# Runtime mode example (experimental):
runtime = NovaBridge(host="localhost", port=30020)
runtime.runtime_pair("123456")  # stores token in runtime.runtime_token
print(runtime.health())
print(runtime.audit(limit=10))
```

## Features

- Scene operations: list, spawn, transform, delete, set-property
- Capability discovery + audit trail
- Event channel discovery (`/nova/events`)
- Structured plan execution + undo
- Runtime pairing helper (`/runtime/pair`) + runtime token header support
- Runtime audit support (`/audit`)
- Asset import (OBJ and FBX)
- Viewport screenshots (JSON base64 or raw PNG)
- Camera controls + show flag overrides
- Material creation
- Console command execution
