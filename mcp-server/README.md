# NovaBridge MCP Server

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

Environment variables:

- `NOVABRIDGE_HOST` (default `localhost`)
- `NOVABRIDGE_PORT` (default `30010`)
- `NOVABRIDGE_API_KEY` (optional)

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
