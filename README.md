# NovaBridge

HTTP API bridge giving AI agents full programmatic control over Unreal Engine 5.

## What it does

NovaBridge is a UE5 Editor plugin that exposes 30 HTTP endpoints for scene manipulation, asset management, material creation, viewport control, and more. Combined with a Blender integration pipeline, AI agents can autonomously generate 3D content and build scenes in Unreal Engine.

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

For headless startup without an existing project, use `NovaBridgeDefault/NovaBridgeDefault.uproject`.

## API Endpoints

See [docs/NOVABRIDGE_HANDOFF.md](docs/NOVABRIDGE_HANDOFF.md) for full API reference.
Primary API reference lives at [docs/API.md](docs/API.md).

## Recent Fixes

- `POST /nova/asset/import` now accepts optional `scale` (default `100`) to normalize Blender meter-scale OBJ files to UE centimeters.
- `POST /nova/scene/set-property` now includes class-name alias matching for component prefixes (for example, `PointLightComponent0.Intensity` resolves correctly).
- MB-Lab export cleanup removes non-character scene objects/ground planes before export.

## Blender Extension Configuration

The `extensions/openclaw/nova-blender` bridge now supports environment-based configuration:

- `NOVABRIDGE_BLENDER_PATH` (default platform path)
- `NOVABRIDGE_EXPORT_DIR` (default OS temp dir `novabridge-exports`)
- `NOVABRIDGE_SCRIPTS_DIR` (default auto-detected from extension `scripts/` or repo blender scripts)
- `NOVABRIDGE_OUTPUT_DIR` (default `<NOVABRIDGE_EXPORT_DIR>/output`)
- `NOVABRIDGE_HOST` (default `localhost`)
- `NOVABRIDGE_PORT` (default `30010`)
- `NOVABRIDGE_IMPORT_SCALE` (default `100`)

## New Integrations

- Python SDK: [python-sdk](python-sdk)
- MCP server: [mcp-server](mcp-server)
- Examples: [examples](examples)
- Headless project template: [NovaBridgeDefault](NovaBridgeDefault)
- Demo project scaffold: [NovaBridgeDemo](NovaBridgeDemo)
- Release checklist: [docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md)
- Demo video script: [demo/VIDEO_SCRIPT.md](demo/VIDEO_SCRIPT.md)
- Landing page starter: [site/index.html](site/index.html)

## Setup Guides

- Linux: [docs/SETUP_LINUX.md](docs/SETUP_LINUX.md)
- Windows: [docs/SETUP_WINDOWS.md](docs/SETUP_WINDOWS.md)
- macOS: [docs/SETUP_MAC.md](docs/SETUP_MAC.md)
- Architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- Build status: [docs/BUILD_STATUS.md](docs/BUILD_STATUS.md)
- Self-hosted CI runbook: [docs/CI_SELF_HOSTED.md](docs/CI_SELF_HOSTED.md)

## Packaging

Create a distribution zip:

```bash
./scripts/package_release.sh v1.0.0
```

Output is written to `dist/NovaBridge-v1.0.0.zip`.

## Platforms

- Windows (Win64)
- macOS (Mac)
- Linux (x86_64)
- Linux (ARM64)

## License

Proprietary - All rights reserved. See [LICENSE](LICENSE).
