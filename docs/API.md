# NovaBridge HTTP API

Base URL: `http://localhost:30010/nova`

## Conventions

- All write endpoints use `POST` with JSON body.
- Responses are JSON unless explicitly noted.
- Error shape:
  - `{"status":"error","error":"<message>","code":<http_code>}`
- CORS headers are included on all responses.
- Spawn safety limit: `/scene/spawn` enforces max 100 spawns/minute.
- Optional auth: set `NOVABRIDGE_API_KEY` (or `-NovaBridgeApiKey=<key>`) and send:
  - `X-API-Key: <key>` or
  - `Authorization: Bearer <key>`

## Endpoints

### System

- `GET /health`
  - Includes `version` and `api_key_required`
- `GET /project/info`

### Scene

- `GET /scene/list`
- `POST /scene/spawn`
- `POST /scene/delete`
- `POST /scene/transform`
- `GET|POST /scene/get`
- `POST /scene/set-property`

### Assets

- `GET|POST /asset/list`
- `POST /asset/create`
- `POST /asset/duplicate`
- `POST /asset/delete`
- `POST /asset/rename`
- `GET|POST /asset/info`
- `POST /asset/import`
  - Supports `.obj` and `.fbx`
  - OBJ supports `scale` (default `100`)

### Mesh

- `POST /mesh/create`
- `GET|POST /mesh/get`
- `POST /mesh/primitive`

### Materials

- `POST /material/create`
- `POST /material/set-param`
- `GET|POST /material/get`
- `POST /material/create-instance`

### Viewport

- `GET /viewport/screenshot`
  - Query: `width`, `height`, `format`
  - `format=raw` returns `image/png` bytes
- `POST /viewport/camera/set`
  - Supports `location`, `rotation`, `fov`, `show_flags`
- `GET /viewport/camera/get`

### Blueprint

- `POST /blueprint/create`
- `POST /blueprint/add-component`
- `POST /blueprint/compile`

### Build / Console

- `POST /build/lighting`
- `POST /exec/command`

### Stream

- `POST /stream/start`
- `POST /stream/stop`
- `POST /stream/config`
  - body: `fps` (1-30), `width` (64-1920), `height` (64-1080), `quality` (1-100)
- `GET /stream/status`
  - returns `ws_url` (default: `ws://localhost:30011`) and current stream settings

### PCG

- `GET /pcg/list-graphs`
- `POST /pcg/create-volume`
- `POST /pcg/generate`
- `POST /pcg/set-param`
- `POST /pcg/cleanup`

### Sequencer

- `POST /sequencer/create`
- `POST /sequencer/add-track`
- `POST /sequencer/set-keyframe`
- `POST /sequencer/play`
- `POST /sequencer/stop`
- `POST /sequencer/scrub`
- `POST /sequencer/render`
  - v1 output is PNG image sequence in `output_path`
- `GET /sequencer/info`

### Optimize

- `POST /optimize/nanite`
- `POST /optimize/lod`
- `POST /optimize/lumen`
- `GET /optimize/stats`
- `POST /optimize/textures`
- `POST /optimize/collision`

## Request Samples

```bash
curl -s http://localhost:30010/nova/health
```

```bash
curl -s -X POST http://localhost:30010/nova/asset/import \
  -H "Content-Type: application/json" \
  -d '{"file_path":"/tmp/model.obj","asset_name":"ModelA","destination":"/Game","scale":100}'
```

```bash
curl -s "http://localhost:30010/nova/viewport/screenshot?format=raw&width=1920&height=1080" -o shot.png
```

```bash
curl -s -X POST http://localhost:30010/nova/stream/config \
  -H "Content-Type: application/json" \
  -d '{"fps":10,"width":640,"height":360,"quality":50}'
```

```bash
curl -s http://localhost:30010/nova/stream/status
```

```bash
curl -s -X POST http://localhost:30010/nova/pcg/generate \
  -H "Content-Type: application/json" \
  -d '{"actor_name":"PCGVolume_1","seed":1234,"force_regenerate":true}'
```

```bash
curl -s -X POST http://localhost:30010/nova/sequencer/create \
  -H "Content-Type: application/json" \
  -d '{"name":"Cine01","path":"/Game","duration_seconds":8,"fps":30}'
```
