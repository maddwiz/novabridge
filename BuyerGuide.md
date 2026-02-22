# NovaBridge Buyer Guide

## What You Get

- UE5 plugin with 30 HTTP endpoints for scene, assets, materials, camera, blueprint, and commands
- Blender-to-UE pipeline extension
- Python SDK
- MCP server
- Example scripts and demo project scaffold

## Best Fit

- Teams building AI-driven UE automation
- Tooling teams who need programmable UE editor control
- Rapid scene generation / synthetic media workflows

## Current Release Tier

- `v0.9.0` Early Access
- Validated platforms: Linux ARM64, Windows Win64, macOS
- In validation: Linux x86_64

## Not Included Yet

- Linux x86_64 native runtime validation
- Production SLA / enterprise support terms
- Managed cloud hosting

## Quick Verification

1. Launch Unreal Editor with your project loaded.
2. Confirm log: `NovaBridge server listening on 127.0.0.1:30010`
3. Run:
   - `curl http://127.0.0.1:30010/nova/health`
4. Expect:
   - `{ "status": "ok" }`
