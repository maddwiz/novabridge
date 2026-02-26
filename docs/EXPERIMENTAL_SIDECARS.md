# Experimental Sidecar Servers

These sidecars are included as **optional prototypes** and are not part of the supported
core NovaBridge plugin contract:

- `ai-gen-server/`
- `voice-server/`
- `livelink-server/`

`assistant-server/` is not experimental; it is a supported companion service for Studio/planner flows.

## Support Expectations

- No stability or compatibility guarantees between releases.
- Minimal validation compared with the core plugin/editor/runtime modules.
- External API/provider dependencies may break behavior without notice.

## Commercial Release Guidance

- Do not market these as production features unless they are explicitly promoted in release notes.
- Keep user-facing claims focused on core plugin/API capabilities.
- If shipping these sidecars to customers, label them as optional experimental utilities.
- Release zips should prioritize core components (plugin, SDK, MCP, assistant companion) and treat these as source-only examples unless explicitly bundled.
