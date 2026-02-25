# NovaBridge

> **v1.0.1 - Now Ship-Ready**
>
> NovaBridge is a schema-driven, capability-based, permissioned UE5 control layer with Editor + Runtime support, public test surface, CI checks, Docker tooling, and Python/MCP integrations.

NovaBridge gives AI agents structured HTTP control over Unreal Engine 5 for scene operations, asset operations, viewport control, sequencer, optimization, PCG hooks, and plan execution.

## Core Architecture

```
AI Agent
  -> HTTP / MCP / SDK
    -> NovaBridgeCore (schema, caps, policy, guardrails, audit, plan engine)
      -> NovaBridgeEditor (editor-only actions)
      -> NovaBridgeRuntime (runtime-safe action subset)
```

## Launch Status

- Version: `v1.0.1`
- Editor mode: production-ready
- Runtime mode: production-ready (token-gated + localhost-only)
- WebSocket events: available in editor + runtime modes
- Public tests: C++ automation tests + Python SDK + MCP + Studio tests

## One-Command Onboarding

macOS/Linux:

```bash
./scripts/setup.sh
```

Windows PowerShell:

```powershell
./scripts/setup_win.ps1
```

These scripts:
- copy `NovaBridge/` into the default project plugin path
- print the exact launch command
- auto-launch UnrealEditor when a local engine binary is detected

## Quick Start (Manual)

1. Copy `NovaBridge/` to your UE5 project `Plugins/` directory.
2. Build the project.
3. Launch Editor with NovaBridge enabled:

```bash
UnrealEditor YourProject.uproject -RenderOffScreen -nosplash -unattended -nopause -NovaBridgePort=30010
```

4. Verify:

```bash
curl http://127.0.0.1:30010/nova/health
```

## Runtime Mode (Packaged Builds)

Enable runtime server in packaged/game process:

```bash
YourGame -NovaBridgeRuntime=1 -NovaBridgeRuntimePort=30020
```

Runtime security model:
- localhost-only request acceptance
- pairing endpoint for short-lived token generation
- per-role endpoint permissions (`admin`, `automation`, `read_only`)
- rate limits and spawn guardrails
- audit trail and event stream support

Pairing example:

```bash
curl -X POST http://127.0.0.1:30020/nova/runtime/pair \
  -H "Content-Type: application/json" \
  -d '{"code":"123456","role":"automation"}'
```

## Docker (Recommended For Fast Evaluation)

```bash
docker run --rm -p 8080:8080 -v ./MyProject:/project ghcr.io/maddwiz/novabridge:latest
```

Notes:
- The Docker image ships a **mock NovaBridge API harness** for SDK/MCP/automation testing.
- Unreal Engine binaries are not bundled in Docker images.
- Set `NOVABRIDGE_API_KEY` in container env to require API-key auth.

## Pre-Built Binaries

Release workflow publishes:
- source package zip
- Python SDK wheel
- optional self-hosted plugin binaries for macOS/Windows/Linux targets
- Docker image to `ghcr.io/maddwiz/novabridge`

See `.github/workflows/release.yml` for release build matrix and artifact publishing.

## Major Endpoints

- `GET /nova/health`
- `GET /nova/caps`
- `POST /nova/executePlan`
- `POST /nova/undo`
- `GET /nova/events`
- `GET /nova/audit`
- `POST /nova/scene/spawn`
- `POST /nova/scene/delete`
- `POST /nova/scene/set-property`
- `GET|POST /nova/scene/get`
- `GET /nova/scene/list`
- `GET|POST /nova/viewport/*`
- `POST /nova/sequencer/*`
- `POST /nova/pcg/generate`

Full reference: [docs/API.md](docs/API.md)

## Python SDK + CLI

Install locally:

```bash
cd python-sdk
python -m pip install .
```

Use CLI:

```bash
novabridge-cli --host 127.0.0.1 --port 30010 health
novabridge-cli execute-plan --plan-file ./examples/plan.json
novabridge-cli spawn-actor PointLight --label LaunchSmokeLight --x 0 --y 0 --z 260
```

SDK includes:
- sync client (`novabridge.py`)
- async client (`novabridge_async.py`)
- pydantic models (`novabridge_models.py`)
- examples (`python-sdk/examples/`)

## MCP Server

Location: `mcp-server/`

MCP now exposes health, caps, executePlan, undo, runtime pairing, scene tools, viewport tools, sequencer, PCG, and optimization tools through FastMCP wrappers.

## Testing

Run fast CI-equivalent checks locally:

```bash
python3 scripts/ci/validate_novabridge_cpp.py
python3 -m unittest discover -s python-sdk/tests -p 'test_*.py'
python3 -m unittest discover -s mcp-server/tests -p 'test_*.py'
node --test assistant-server/tests/*.test.js
cd novabridge-studio && npm test && npm run build
```

Unreal automation tests (self-hosted runner / local UE setup):

```bash
scripts/ci/run_automation_tests_mac.sh
```

Cross-platform validation handoffs:
- Windows Codex: [docs/HANDOFF_WINDOWS_CODEX_VALIDATION.md](docs/HANDOFF_WINDOWS_CODEX_VALIDATION.md)
- NVIDIA Spark Codex: [docs/HANDOFF_NVIDIA_SPARK_CODEX_VALIDATION.md](docs/HANDOFF_NVIDIA_SPARK_CODEX_VALIDATION.md)

## Packaging

Create release bundle + wheel:

```bash
./scripts/package_release.sh 1.0.1
```

Build Docker image during packaging:

```bash
NOVABRIDGE_BUILD_DOCKER=1 NOVABRIDGE_DOCKER_IMAGE=ghcr.io/maddwiz/novabridge ./scripts/package_release.sh 1.0.1
```

## Experimental Sidecars

These are explicitly experimental and not part of supported core plugin guarantees:
- `ai-gen-server/`
- `voice-server/`
- `livelink-server/`
- `assistant-server/`

## Security Defaults

- localhost-first networking
- optional API key (`X-API-Key`)
- role-based policy gates
- runtime token gating
- audit trail and event stream telemetry

## License

Proprietary - All rights reserved.
See [LICENSE](LICENSE), [EULA.txt](EULA.txt), and [SUPPORT.md](SUPPORT.md).
