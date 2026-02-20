bl_info = {
    "name": "NovaBridge LiveLink",
    "author": "NovaBridge",
    "version": (0, 1, 0),
    "blender": (3, 0, 0),
    "location": "View3D > Sidebar > NovaBridge",
    "description": "Bidirectional Blender <-> UE5 transform sync via NovaBridge LiveLink server",
    "category": "3D View",
}

import bpy
import json
import threading
import urllib.request
from bpy.app.handlers import persistent

SYNC_URL = "http://127.0.0.1:30013"
_tracked_objects = {}
_remote_update = False


def _hash_transform(obj):
    return (
        round(obj.location.x, 5),
        round(obj.location.y, 5),
        round(obj.location.z, 5),
        round(obj.rotation_euler.x, 5),
        round(obj.rotation_euler.y, 5),
        round(obj.rotation_euler.z, 5),
        round(obj.scale.x, 5),
        round(obj.scale.y, 5),
        round(obj.scale.z, 5),
    )


def _send_changes(changes):
    try:
        payload = json.dumps({"source": "blender", "changes": changes}).encode("utf-8")
        req = urllib.request.Request(
            f"{SYNC_URL}/sync/push",
            data=payload,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        urllib.request.urlopen(req, timeout=1.5)
    except Exception:
        pass


@persistent
def on_depsgraph_update(scene, depsgraph):
    del scene
    global _remote_update
    if _remote_update:
        return

    changes = []
    for update in depsgraph.updates:
        obj = getattr(update, "id", None)
        if not isinstance(obj, bpy.types.Object):
            continue
        if obj.type != "MESH":
            continue

        tx_hash = _hash_transform(obj)
        if _tracked_objects.get(obj.name) == tx_hash:
            continue
        _tracked_objects[obj.name] = tx_hash
        changes.append(
            {
                "name": obj.name,
                "location": [obj.location.x, obj.location.y, obj.location.z],
                "rotation": [obj.rotation_euler.x, obj.rotation_euler.y, obj.rotation_euler.z],
                "scale": [obj.scale.x, obj.scale.y, obj.scale.z],
            }
        )

    if changes:
        threading.Thread(target=_send_changes, args=(changes,), daemon=True).start()


def _poll_ue5_changes():
    global _remote_update
    try:
        req = urllib.request.Request(f"{SYNC_URL}/sync/pull?target=blender", method="GET")
        with urllib.request.urlopen(req, timeout=1.5) as resp:
            payload = json.loads(resp.read().decode("utf-8"))
        incoming = payload.get("changes", [])
        if incoming:
            _remote_update = True
            try:
                for change in incoming:
                    name = change.get("name")
                    obj = bpy.data.objects.get(name)
                    if not obj:
                        continue
                    loc = change.get("location")
                    rot = change.get("rotation")
                    scale = change.get("scale")
                    if loc and len(loc) == 3:
                        obj.location.x, obj.location.y, obj.location.z = loc
                    if rot and len(rot) == 3:
                        obj.rotation_euler.x, obj.rotation_euler.y, obj.rotation_euler.z = rot
                    if scale and len(scale) == 3:
                        obj.scale.x, obj.scale.y, obj.scale.z = scale
                    _tracked_objects[obj.name] = _hash_transform(obj)
            finally:
                _remote_update = False
    except Exception:
        pass
    return 0.1


def register():
    if on_depsgraph_update not in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.append(on_depsgraph_update)
    bpy.app.timers.register(_poll_ue5_changes, first_interval=1.0, persistent=True)


def unregister():
    if on_depsgraph_update in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(on_depsgraph_update)
    try:
        bpy.app.timers.unregister(_poll_ue5_changes)
    except Exception:
        pass

