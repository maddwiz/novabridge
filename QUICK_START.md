# NovaBridge Quick Start

1. Copy `NovaBridge/` into your UE project plugins directory.
2. Launch UE with a `.uproject` path (use `NovaBridgeDefault` or `NovaBridgeDemo` if needed).
   - Linux service helper: `./scripts/install_linux_service.sh`
3. Confirm API health:
   - `curl http://localhost:30010/nova/health`
   - `curl http://localhost:30010/nova/project/info`
   - Optional auth mode: set `NOVABRIDGE_API_KEY` and call with header `X-API-Key`.
4. (Optional) Install Python SDK usage:
   - `python examples/python/01_hello_world.py`
5. (Optional) Start MCP server:
   - `python mcp-server/novabridge_mcp.py`
6. (Optional) Build a release package:
   - `./scripts/package_release.sh v0.9.0`
