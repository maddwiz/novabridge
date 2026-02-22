# Lemon Squeezy Listing Copy (Draft)

## Product Title

NovaBridge - AI Control API for Unreal Engine 5 Editor

## Short Description

Control UE5 Editor from AI agents through a local HTTP API for scene ops, asset import, viewport capture, and sequencer automation.

## Long Description

NovaBridge is an Unreal Engine 5 Editor plugin that exposes a practical HTTP API for agent-driven worldbuilding and technical art workflows.

Included:
- UE5 plugin (`NovaBridge/`)
- Python SDK
- MCP server
- Blender helper pipeline
- examples and project scaffolds

Editor-only scope:
- Runs inside UE5 Editor with a loaded `.uproject`
- Not intended for packaged runtime game builds

Default networking:
- Local workflow via `127.0.0.1`
- Default port `30010`
- Port override supported: `-NovaBridgePort=30010` (replace `30010` with your chosen open port)

Current release:
- Early Access `v0.9.0`
- Validated: Windows Win64, macOS, Linux ARM64
- In validation: Linux x86_64

## Quick API Proof Snippets

```bash
curl -sS http://127.0.0.1:30010/nova/health
```

```bash
curl -sS -X POST http://127.0.0.1:30010/nova/scene/spawn \
  -H "Content-Type: application/json" \
  -d '{"class":"PointLight","label":"LaunchSmokeLight","x":0,"y":0,"z":260}'
```

```bash
curl -sS -X POST http://127.0.0.1:30010/nova/scene/delete \
  -H "Content-Type: application/json" \
  -d '{"name":"LaunchSmokeLight"}'
```

## Visual Proof Assets

- Screenshot: `docs/images/mac-smoke-launchproof.png`
- Control proof chain: `docs/AI_CONTROL_PROOF.md`
