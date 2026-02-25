# NovaBridge

HTTP API bridge giving AI agents full programmatic control over Unreal Engine 5.

## What it does

NovaBridge is a UE5 control plugin with an editor-first HTTP surface for scene manipulation, asset management, material creation, viewport control, and more. Combined with a Blender integration pipeline, AI agents can autonomously generate 3D content and build scenes in Unreal Engine.

> Editor module is still the primary production path. An experimental runtime module now exists for packaged builds and must be explicitly enabled.

## Architecture

```
AI Agent (any LLM)
  → HTTP calls (or OpenClaw extension tools)
    → NovaBridge Plugin (C++, inside UE5 Editor, port 30010)
      → Unreal Engine 5

AI Agent → Blender (Python/MB-Lab) → OBJ export → NovaBridge import → UE5
```

Runtime path (experimental):

```
AI Agent
  → local runtime HTTP calls (token + pairing)
    → NovaBridgeRuntime module (port 30020 by default, disabled unless -NovaBridgeRuntime=1)
      → packaged UE5 runtime world
```

## Quick Start

1. Copy `NovaBridge/` to your UE5 project's `Plugins/` folder
2. Build the project
3. Launch UE5: `UnrealEditor YourProject.uproject -RenderOffScreen -nosplash -unattended -nopause -NovaBridgePort=30010`
4. Test: `curl http://localhost:30010/nova/health`

Port override: change `-NovaBridgePort=30010` to any open port, then use that same port in your API calls.

For headless startup without an existing project, use `NovaBridgeDefault/NovaBridgeDefault.uproject`.

## Runtime Mode (Experimental)

Enable runtime server in packaged/game processes only when needed:

```bash
YourGame -NovaBridgeRuntime=1 -NovaBridgeRuntimePort=30020
```

Runtime pairing flow:

1. Read pairing code from runtime logs on startup.
2. Exchange code:
```bash
curl -sS -X POST http://127.0.0.1:30020/nova/runtime/pair \
  -H "Content-Type: application/json" \
  -d '{"code":"123456"}'
```
3. Use returned token in `X-NovaBridge-Token` for `/nova/health`, `/nova/caps`, `/nova/executePlan`.

## macOS Smoke Snapshot

Validation screenshot from a live macOS smoke run (health/spawn/delete path):

![macOS NovaBridge smoke screenshot](docs/images/mac-smoke-launchproof.png)

For explicit API-driven control evidence (bind log + health + spawn + delete artifacts), see:
- [docs/AI_CONTROL_PROOF.md](docs/AI_CONTROL_PROOF.md)

Example command used during smoke:

```bash
curl -sS -X POST http://127.0.0.1:30010/nova/scene/spawn \
  -H "Content-Type: application/json" \
  -d '{"class":"PointLight","label":"LaunchSmokeLight","x":0,"y":0,"z":260}'
```

## Release Status

- Current release: `v0.9.5-dev` (Early Access)
- Validated now: Linux ARM64, macOS, Windows Win64
- In validation: Linux x86_64

## Security Defaults

- NovaBridge uses Unreal HTTP Server's default listener bind address (`127.0.0.1`).
- It is designed for local Unreal Editor workflows on the same machine.
- API key authentication is enabled automatically when `NOVABRIDGE_API_KEY` or `-NovaBridgeApiKey=<key>` is set.
- If no API key is configured, NovaBridge allows requests and logs a startup warning.
- Role-based policy is available via `X-NovaBridge-Role` (`admin`, `automation`, `read_only`) or `NOVABRIDGE_DEFAULT_ROLE` / `-NovaBridgeDefaultRole=`.
- Runtime mode requires token auth via `X-NovaBridge-Token` after pairing (`POST /nova/runtime/pair`).
- Runtime requests are localhost-only (non-loopback hosts are rejected).

## Platform Control Additions (v0.9.5-dev)

- `GET /nova/caps` for capability + policy discovery.
- `GET /nova/events` for event channel discovery (`ws://localhost:30012` by default).
- `POST /nova/executePlan` for schema-driven multi-step execution (`spawn`, `delete`, `set`, `screenshot`).
- `POST /nova/undo` for reversible operations (currently tracked spawn actions).
- `GET /nova/audit` for structured in-memory execution/audit trail.
- Runtime now also exposes token-gated `GET /nova/audit`.
- Spawn guardrails for non-admin roles:
  - class allow list
  - transform bounds
  - per-plan and per-minute limits
- Capability discovery is now backed by a shared core registry (`NovaBridgeCore`) used by Editor and Runtime modules.
- Event socket port override: `-NovaBridgeEventsPort=<port>`.

## API Endpoints

Primary API reference lives at [docs/API.md](docs/API.md).

## Recent Fixes

- `POST /nova/asset/import` now accepts optional `scale` (default `100`) to normalize Blender meter-scale OBJ files to UE centimeters.
- `POST /nova/scene/set-property` now includes class-name alias matching for component prefixes (for example, `PointLightComponent0.Intensity` resolves correctly).
- Sequencer scrub fallback on UE `< 5.7` now uses explicit playback params (non-recursive) to avoid stack overflow regressions.
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
- v0.9.1 bug-fix handoff (resolved): [docs/BUG_FIX_HANDOFF_v0.9.1.md](docs/BUG_FIX_HANDOFF_v0.9.1.md)
- Fab launch checklist: [docs/FAB_MARKET_LAUNCH_CHECKLIST.md](docs/FAB_MARKET_LAUNCH_CHECKLIST.md)
- Fab listing copy draft: [docs/FAB_LISTING_COPY.md](docs/FAB_LISTING_COPY.md)
- Lemon Squeezy listing copy draft: [docs/LEMON_SQUEEZY_LISTING_COPY.md](docs/LEMON_SQUEEZY_LISTING_COPY.md)
- Self-hosted CI runbook: [docs/CI_SELF_HOSTED.md](docs/CI_SELF_HOSTED.md)
- Runner bootstrap: [docs/RUNNER_SETUP.md](docs/RUNNER_SETUP.md)

## Packaging

Create a distribution zip:

```bash
./scripts/package_release.sh 0.9.5-dev
```

Output is written to `dist/NovaBridge-v0.9.5-dev.zip`.

## Platform Status

- Linux (ARM64): validated
- Linux (x86_64): pending native validation
- Windows (Win64): validated
- macOS (Mac): validated

## License

Proprietary - All rights reserved. See [LICENSE](LICENSE).
