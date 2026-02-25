# NovaBridge Buyer Guide

## What You Get

- UE5 plugin with schema-driven HTTP control endpoints across scene, assets, viewport, sequencer, optimization, and PCG.
- Runtime module with token pairing, role policy, route limits, and localhost-only enforcement.
- Python SDK (sync + async + CLI) and MCP server.
- Public test surface and CI workflows.
- Packaging and release tooling including Docker harness and wheel artifacts.

## Best Fit

- Teams building AI-assisted UE tooling pipelines.
- Studios needing deterministic scene automation and plan execution.
- Developers integrating LLM agents with Unreal Editor or packaged runtime workflows.

## Current Release Tier

- **`v1.0.1` Ship-Ready**
- Validated platforms: macOS, Windows Win64, Linux (x64 + ARM64 build workflows)

## Not Included

- Managed cloud hosting.
- Enterprise SLA by default.
- Unreal Engine binaries inside Docker images.

## Quick Verification

1. Run `./scripts/setup.sh` (or `./scripts/setup_win.ps1`).
2. Check health:
   - `curl http://127.0.0.1:30010/nova/health`
3. Check capabilities:
   - `curl http://127.0.0.1:30010/nova/caps`
4. Execute one plan:
   - `curl -X POST http://127.0.0.1:30010/nova/executePlan -H "Content-Type: application/json" -d '{"steps":[{"action":"spawn","params":{"type":"PointLight","label":"BuyerGuideLight","x":0,"y":0,"z":220}}]}'`
