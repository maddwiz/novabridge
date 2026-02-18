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
python3 examples/demo-scene/live_operator_demo.py --host 127.0.0.1 --port 30010 --pause 1.0 --camera-frames 96
```

This script visibly performs:

- primitive creation (plane/cube/sphere)
- a familiar scene build: floor/walls, desk, monitor, chair, keyboard, mouse, mug, plant
- staged lighting for a polished reveal
- cinematic camera cuts + final orbit
- animated assembly: each object drops and scales into place (piece-by-piece)
- assembly is intentionally slower now so viewers can clearly see each part being placed

It keeps the scene at the end so your recording can linger on the final shot.

Optional cleanup run:

```bash
python3 examples/demo-scene/live_operator_demo.py --cleanup
```

## Alternative: Gaming Battlestation (More Viral-Friendly)

This variant builds a familiar streamer/gaming setup (dual monitors, tower, chair, speakers, neon wall bars):

```bash
python3 examples/demo-scene/live_operator_demo_gaming.py --host 127.0.0.1 --port 30010 --pause 0.8 --camera-frames 120
```

Optional cleanup run:

```bash
python3 examples/demo-scene/live_operator_demo_gaming.py --cleanup
```

## 4) Stop recording

Recommended export: `1920x1080`, `30fps`.

## Notes for Better Engagement

- Keep total runtime near 60-90 seconds by lowering `--pause` (for example `0.8`).
- Use the final still at `examples/demo-scene/live_operator_demo_end.png` as your thumbnail frame.
