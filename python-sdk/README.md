# NovaBridge Python SDK

Zero-dependency Python client for NovaBridge.

## Usage

```python
from novabridge import NovaBridge

ue5 = NovaBridge(host="localhost", port=30010)
print(ue5.health())

ue5.spawn("PointLight", label="Sun", x=0, y=0, z=500)
ue5.viewport_screenshot(save_path="scene.png")
```

## Features

- Scene operations: list, spawn, transform, delete, set-property
- Asset import (OBJ and FBX)
- Viewport screenshots (JSON base64 or raw PNG)
- Camera controls + show flag overrides
- Material creation
- Console command execution
