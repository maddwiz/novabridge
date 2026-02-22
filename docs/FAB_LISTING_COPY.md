# Fab Listing Copy (Draft)

## Product Title

NovaBridge - AI Scene Control for Unreal Engine 5

## Short Description

Control UE5 Editor from AI agents via HTTP API: scene ops, asset import, materials, viewport capture, sequencer, and automation tooling.

## Long Description

NovaBridge is an Unreal Engine 5 Editor plugin that exposes a production-style HTTP API for AI-driven automation workflows.

It enables agents and tools to:
- spawn and transform actors,
- import assets,
- build materials and blueprints,
- control camera/viewport and capture screenshots,
- drive sequencer and stream status endpoints.

Included extras:
- Python SDK
- MCP server
- Blender integration helpers
- Demo scripts and sample project scaffolds

Release tier: Early Access (`v0.9.0`)

## Supported Platforms (Current)

- Validated: Windows Win64, macOS, Linux ARM64
- In validation: Linux x86_64

## Ideal For

- AI-assisted worldbuilding pipelines
- Tool teams automating Unreal Editor tasks
- Rapid content generation and scene assembly

## Customer Notes

- Runs inside UE5 Editor with a loaded `.uproject`.
- Local-machine workflow by default (`127.0.0.1` listener bind via UE HTTP default).
- Default API port is `30010` (overridable).
- Optional API key security is supported (`NOVABRIDGE_API_KEY` / `-NovaBridgeApiKey`).
- Early Access: validated on Windows Win64, macOS, and Linux ARM64.

## Support

- Primary support: GitHub Issues
- Marketplace support: use marketplace support contact channel
