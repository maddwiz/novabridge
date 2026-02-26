# NovaBridge MCP Server

FastMCP bridge exposing NovaBridge HTTP capabilities to MCP-compatible agents.

## Install

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

## Run

```bash
python novabridge_mcp.py
```

## Exposed Tool Categories

- Control: `ue5_health`, `ue5_caps`, `ue5_project_info`
- Assistant/Planner: `ue5_assistant_health`, `ue5_assistant_catalog`, `ue5_assistant_plan`, `ue5_assistant_execute`
- Plan engine: `ue5_execute_plan`, `ue5_undo`, `ue5_runtime_pair`
- Scene: list/spawn/transform/delete/get/set-property
- Viewport: screenshot/camera set/get
- Stream: start/stop/config/status
- PCG: list/create/generate/set-param/cleanup
- Sequencer: create/add-track/keyframe/play/stop/scrub/render/info
- Optimize: nanite/lod/lumen/stats/textures/collision

## Tests

```bash
python3 -m unittest discover -s tests -p 'test_*.py'
```

## Environment Variables

- `NOVABRIDGE_HOST` (default `localhost`)
- `NOVABRIDGE_PORT` (default `30010`)
- `NOVABRIDGE_API_KEY` (optional)
- `NOVABRIDGE_ASSISTANT_HOST` (default `NOVABRIDGE_HOST`)
- `NOVABRIDGE_ASSISTANT_PORT` (default `30016`)

## Claude Desktop Example

```json
{
  "mcpServers": {
    "novabridge": {
      "command": "python",
      "args": ["/absolute/path/to/novabridge/mcp-server/novabridge_mcp.py"]
    }
  }
}
```
