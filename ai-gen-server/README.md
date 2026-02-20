# NovaBridge AI Gen Server

Text-to-3D sidecar service (Meshy-first) for prompt -> model generation.

## Install

```bash
python -m venv .venv
. .venv/bin/activate
pip install -r ai-gen-server/requirements.txt
```

## Run

```bash
python ai-gen-server/ai_gen_server.py
```

Default URL: `http://localhost:30014`

## Environment

- `MESHY_API_KEY` (required for provider `meshy`)
- `NOVABRIDGE_HOST` (default `localhost`)
- `NOVABRIDGE_PORT` (default `30010`)
- `NOVABRIDGE_API_KEY` (optional)
- `NOVABRIDGE_AI_GEN_PORT` (default `30014`)
- `NOVABRIDGE_EXPORT_DIR` (default `/tmp/novabridge-exports`)

## Endpoint

`POST /ai/generate`

```json
{
  "prompt": "futuristic drone",
  "provider": "meshy",
  "asset_name": "DroneA",
  "style": "realistic",
  "import_to_ue5": true
}
```

