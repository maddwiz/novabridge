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
