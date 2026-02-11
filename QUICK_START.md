# NovaBridge Quick Start

1. Copy `NovaBridge/` into your UE project plugins directory.
2. Launch UE with a `.uproject` path (use `NovaBridgeDefault` or `NovaBridgeDemo` if needed).
3. Confirm API health:
   - `curl http://localhost:30010/nova/health`
   - `curl http://localhost:30010/nova/project/info`
4. (Optional) Install Python SDK usage:
   - `python examples/python/01_hello_world.py`
5. (Optional) Start MCP server:
   - `python mcp-server/novabridge_mcp.py`
6. (Optional) Build a release package:
   - `./scripts/package_release.sh v1.0.0`
