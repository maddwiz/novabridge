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

## Release Status

- Current release: `v0.9.0` (Early Access)
- Validated now: Linux ARM64, macOS, Windows Win64
- In validation: Linux x86_64

## Security Defaults

- NovaBridge uses Unreal HTTP Server's default listener bind address (`127.0.0.1`).
- It is designed for local Unreal Editor workflows on the same machine.
- API key authentication is enabled automatically when `NOVABRIDGE_API_KEY` or `-NovaBridgeApiKey=<key>` is set.
- If no API key is configured, NovaBridge allows requests and logs a startup warning.

## API Endpoints

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
- `NOVABRIDGE_API_KEY` (optional shared secret for API access)

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
- v0.9.1 bug-fix handoff: [docs/BUG_FIX_HANDOFF_v0.9.1.md](docs/BUG_FIX_HANDOFF_v0.9.1.md)
- Fab launch checklist: [docs/FAB_MARKET_LAUNCH_CHECKLIST.md](docs/FAB_MARKET_LAUNCH_CHECKLIST.md)
- Fab listing copy draft: [docs/FAB_LISTING_COPY.md](docs/FAB_LISTING_COPY.md)
- Self-hosted CI runbook: [docs/CI_SELF_HOSTED.md](docs/CI_SELF_HOSTED.md)
- Runner bootstrap: [docs/RUNNER_SETUP.md](docs/RUNNER_SETUP.md)

## Packaging

Create a distribution zip:

```bash
./scripts/package_release.sh 0.9.0
```

Output is written to `dist/NovaBridge-v0.9.0.zip`.

## Platform Status

- Linux (ARM64): validated
- Linux (x86_64): pending native validation
- Windows (Win64): validated
- macOS (Mac): validated

## License

Proprietary - All rights reserved. See [LICENSE](LICENSE).
