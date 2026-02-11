# NovaBridge Quick Start

1. Copy `NovaBridge/` into your UE project plugins directory.
2. Launch UE with a `.uproject` path.
3. Confirm API health:
   - `curl http://localhost:30010/nova/health`
4. (Optional) Install Python SDK usage:
   - `python examples/python/01_hello_world.py`
5. (Optional) Start MCP server:
   - `python mcp-server/novabridge_mcp.py`
