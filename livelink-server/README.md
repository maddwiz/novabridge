# NovaBridge Blender LiveLink Server

Bidirectional transform sync bridge between Blender addon and NovaBridge UE5 API.

## Run

```bash
pip install flask
python livelink-server/sync_server.py
```

Default port: `30013`

## Endpoints

- `GET /sync/health`
- `POST /sync/push`
- `GET /sync/pull?target=blender|ue5`

## Blender Addon

Install `blender/addons/novabridge_livelink.py` in Blender.

The addon pushes changed mesh transforms and polls updates from this server every 100ms.
