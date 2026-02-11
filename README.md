# NovaBridge

HTTP API bridge giving AI agents full programmatic control over Unreal Engine 5.

## What it does

NovaBridge is a UE5 Editor plugin that exposes 29 HTTP endpoints for scene manipulation, asset management, material creation, viewport control, and more. Combined with a Blender integration pipeline, AI agents can autonomously generate 3D content and build scenes in Unreal Engine.

## Architecture

```
AI Agent (any LLM)
  → HTTP calls (or OpenClaw extension tools)
    → NovaBridge Plugin (C++, inside UE5 Editor, port 30010)
      → Unreal Engine 5

AI Agent → Blender (Python/MB-Lab) → OBJ export → NovaBridge import → UE5
```

## Quick Start

1. Copy `NovaBridge/` to your UE5 project's `Plugins/` folder
2. Build the project
3. Launch UE5: `UnrealEditor YourProject.uproject -RenderOffScreen -nosplash -unattended -nopause`
4. Test: `curl http://localhost:30010/nova/health`

## API Endpoints

See [docs/NOVABRIDGE_HANDOFF.md](docs/NOVABRIDGE_HANDOFF.md) for full API reference.

## Platforms

- Windows (Win64)
- macOS (Mac)
- Linux (x86_64)
- Linux (ARM64)

## License

Proprietary - All rights reserved.
