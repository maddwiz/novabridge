import json
from pathlib import Path

from novabridge import NovaBridge

client = NovaBridge(host="127.0.0.1", port=30010)
scene = client.scene_list()
Path("scene_snapshot.json").write_text(json.dumps(scene, indent=2), encoding="utf-8")
print({"status": "ok", "saved_to": "scene_snapshot.json", "count": scene.get("count")})
