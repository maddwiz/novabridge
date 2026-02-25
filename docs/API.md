# NovaBridge HTTP API (v1.0.0)

Editor base URL:
- `http://127.0.0.1:30010/nova`

Runtime base URL:
- `http://127.0.0.1:30020/nova`

## Global Conventions

- Write routes use `POST` with JSON body.
- Errors return:
  - `{"status":"error","error":"<message>","code":<http_code>}`
- CORS headers are returned on JSON routes.
- Optional API key auth:
  - `X-API-Key: <key>`
  - `Authorization: Bearer <key>`
- Role header (editor and runtime):
  - `X-NovaBridge-Role: admin|automation|read_only`

## Runtime Security Model

Runtime mode (`-NovaBridgeRuntime=1`) enforces:
- localhost-only host acceptance
- pairing flow (`POST /runtime/pair`)
- token auth (`X-NovaBridge-Token`)
- role-gated endpoint access
- per-route rate limits
- spawn guardrails and actor-count limits

## Control Endpoints

- `GET /health`
- `GET /project/info` (editor)
- `GET /caps`
- `GET /events`
- `GET /audit`
- `POST /executePlan`
- `POST /undo`

`GET /caps` returns mode, role, permissions snapshot, and registered capabilities.

## Runtime-Only Control

- `POST /runtime/pair`

Request:

```json
{"code":"123456","role":"automation"}
```

Response:

```json
{"status":"ok","mode":"runtime","role":"automation","token":"..."}
```

## Scene Endpoints

Editor:
- `GET /scene/list`
- `POST /scene/spawn`
- `POST /scene/delete`
- `POST /scene/transform`
- `GET|POST /scene/get`
- `POST /scene/set-property`

Runtime:
- `GET /scene/list`
- `GET|POST /scene/get`
- `POST /scene/set-property`

## Viewport Endpoints

Editor:
- `GET /viewport/screenshot`
- `POST /viewport/camera/set`
- `GET /viewport/camera/get`

Runtime:
- `GET /viewport/screenshot`
- `POST /viewport/camera/set`
- `GET /viewport/camera/get`

`GET /viewport/screenshot?format=raw` returns `image/png` bytes.

## Sequencer Endpoints

Editor:
- `POST /sequencer/create`
- `POST /sequencer/add-track`
- `POST /sequencer/set-keyframe`
- `POST /sequencer/play`
- `POST /sequencer/stop`
- `POST /sequencer/scrub`
- `POST /sequencer/render`
- `GET /sequencer/info`

Runtime:
- `POST /sequencer/play`
- `POST /sequencer/stop`
- `GET /sequencer/info`

## PCG Endpoints

Editor:
- `GET /pcg/list-graphs`
- `POST /pcg/create-volume`
- `POST /pcg/generate`
- `POST /pcg/set-param`
- `POST /pcg/cleanup`

Runtime:
- `POST /pcg/generate` (if PCG module is available in runtime build)

## Asset / Material / Mesh / Build / Stream / Optimize (Editor)

Assets:
- `GET|POST /asset/list`
- `POST /asset/create`
- `POST /asset/duplicate`
- `POST /asset/delete`
- `POST /asset/rename`
- `GET|POST /asset/info`
- `POST /asset/import`

Mesh:
- `POST /mesh/create`
- `GET|POST /mesh/get`
- `POST /mesh/primitive`

Material:
- `POST /material/create`
- `POST /material/set-param`
- `GET|POST /material/get`
- `POST /material/create-instance`

Blueprint / Build / Exec:
- `POST /blueprint/create`
- `POST /blueprint/add-component`
- `POST /blueprint/compile`
- `POST /build/lighting`
- `POST /exec/command`

Stream:
- `POST /stream/start`
- `POST /stream/stop`
- `POST /stream/config`
- `GET /stream/status`

Optimize:
- `POST /optimize/nanite`
- `POST /optimize/lod`
- `POST /optimize/lumen`
- `GET /optimize/stats`
- `POST /optimize/textures`
- `POST /optimize/collision`

## ExecutePlan Actions

Editor supported actions:
- `spawn`
- `delete`
- `set`
- `screenshot`

Runtime supported actions:
- `spawn`
- `delete`
- `set`
- `call`
- `screenshot`

Request shape:

```json
{
  "plan_id": "demo-plan",
  "steps": [
    { "action": "spawn", "params": { "type": "PointLight", "label": "DemoLight", "x": 0, "y": 0, "z": 260 } }
  ]
}
```

## Sample Requests

```bash
curl http://127.0.0.1:30010/nova/health
```

```bash
curl -X POST http://127.0.0.1:30010/nova/executePlan \
  -H "Content-Type: application/json" \
  -d '{"plan_id":"demo","steps":[{"action":"spawn","params":{"type":"PointLight","label":"A","x":0,"y":0,"z":250}}]}'
```

```bash
curl -X POST http://127.0.0.1:30020/nova/runtime/pair \
  -H "Content-Type: application/json" \
  -d '{"code":"123456","role":"automation"}'
```

```bash
curl http://127.0.0.1:30020/nova/caps \
  -H "X-NovaBridge-Token: <token>"
```
