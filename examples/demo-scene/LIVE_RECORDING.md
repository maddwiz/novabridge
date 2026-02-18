# Live Editor Recording (Mac)

Use this when you want a video that shows Unreal Editor being actively built in front of the viewer.

## 1) Open UE5 normally (GUI mode)

- Launch your NovaBridge project in Unreal Editor.
- Confirm plugin is enabled and `GET /nova/health` works.

## 2) Start screen recording

- Use QuickTime or OBS.
- Record the Unreal Editor window only.

## 3) Run live demo script

From repo root:

```bash
python3 examples/demo-scene/live_operator_demo.py --host 127.0.0.1 --port 30010 --pause 1.6 --camera-frames 64
```

This script visibly performs:

- primitive creation (plane/cube/sphere)
- actor spawning and transforms
- lighting setup
- cinematic camera orbit

It keeps the scene at the end so your recording can linger on the final shot.

Optional cleanup run:

```bash
python3 examples/demo-scene/live_operator_demo.py --cleanup
```

## 4) Stop recording

Recommended export: `1920x1080`, `30fps`.
