# NovaBridge HTTP API

Base URL: `http://localhost:30010/nova`

Runtime base URL (experimental, when enabled): `http://localhost:30020/nova`

## Conventions

- All write endpoints use `POST` with JSON body.
- Responses are JSON unless explicitly noted.
- Error shape:
  - `{"status":"error","error":"<message>","code":<http_code>}`
- CORS headers are included on all responses.
- Role-based policy is supported via `X-NovaBridge-Role`:
  - `admin` (full access)
  - `automation` (exec/build restricted)
  - `read_only` (read routes only)
- Default role can be configured with `NOVABRIDGE_DEFAULT_ROLE` or `-NovaBridgeDefaultRole=`.
- Request-level and role-level per-minute rate limits are enforced.
- Optional auth: set `NOVABRIDGE_API_KEY` (or `-NovaBridgeApiKey=<key>`) and send:
  - `X-API-Key: <key>` or
  - `Authorization: Bearer <key>`
- Runtime mode (experimental):
  - Enable with `-NovaBridgeRuntime=1` (or `NOVABRIDGE_RUNTIME=1`)
  - Runtime endpoints enforce localhost host access (`127.0.0.1`, `localhost`, `::1`)
  - Runtime events WebSocket default port is `30022` (`-NovaBridgeRuntimeEventsPort=<port>`)
  - Pair with `POST /runtime/pair` using startup pairing code
  - Send runtime token using `X-NovaBridge-Token: <token>`
  - `POST /executePlan` is rate-limited in runtime mode (default 30 requests/minute)

## Endpoints

### System

- `GET /health`
  - Includes `version`, `mode`, `default_role`, `api_key_required`, `stream_ws_port`, `events_ws_port`
- `GET /project/info`
- `GET /caps`
- `GET /events`
  - Query: `types` (optional comma-separated type filter for metadata counters)
  - Returns event socket metadata:
    - `ws_url`, `ws_port`, `clients`, `clients_with_filters`
    - `pending_events`, `filtered_pending_events`
    - `supported_types`, `pending_by_type`, `subscription_action`
  - Event stream types:
    - `audit`
    - `spawn`
    - `delete`
    - `plan_step`
    - `plan_complete`
    - `error`
  - `POST /executePlan` spawn/delete steps also emit typed `spawn`/`delete` events.
  - WebSocket subscription control:
    - On connect, server sends `{"type":"subscription","status":"ready",...}`
    - Client can narrow stream by sending `{"action":"subscribe","types":["spawn","error"]}`
    - Server replies `{"type":"subscription","status":"ok","filter_enabled":true,...}`
    - `{"action":"clear"}` / `{"action":"all"}` clears filter for that client
    - For strict filtering, wait for `status=ok` before issuing event-producing actions
- `GET /audit`
  - Query: `limit` (1-500, default 50)
- `POST /executePlan`
  - Plan schema:
    - `plan_id` (optional)
    - `steps[]` each with `action` and `params`
  - Supported actions:
    - `spawn`
    - `delete`
    - `set`
    - `screenshot`
- `POST /undo`
  - Reversible operations currently tracked: `spawn`

### Runtime (Experimental)

- `POST /runtime/pair`
  - request: `{"code":"<6-digit-pairing-code>"}`
  - returns short-lived in-memory runtime token
- `GET /health`
  - runtime mode requires token auth
- `GET /caps`
  - runtime-safe capability subset
- `GET /events`
  - token-gated runtime event socket discovery
  - Query: `types` (optional comma-separated type filter for metadata counters)
  - returns `ws_url`, `ws_port`, `clients`, `clients_with_filters`, `pending_events`, `filtered_pending_events`, `supported_types`, `pending_by_type`, `subscription_action`
- `GET /audit`
  - token-gated in-memory runtime audit trail
  - Query: `limit` (1-500, default 50)
- `POST /executePlan`
  - runtime-safe subset currently supports:
    - `spawn`
    - `delete`
    - `set`
  - Runtime `spawn` supports optional `label` (used as requested actor/object name)
- `POST /undo`
  - token-gated runtime undo endpoint
  - currently supports undoing spawn entries recorded by runtime `executePlan`

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
curl -s http://localhost:30010/nova/caps
```

```bash
curl -s http://localhost:30010/nova/events
```

```bash
curl -s "http://localhost:30010/nova/events?types=spawn,error"
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

```bash
curl -s -X POST http://localhost:30010/nova/executePlan \
  -H "Content-Type: application/json" \
  -d '{
    "plan_id":"demo-plan",
    "steps":[
      {"action":"spawn","params":{"type":"PointLight","label":"LaunchSmokeLight","transform":{"location":[0,0,260]}}},
      {"action":"set","params":{"target":"LaunchSmokeLight","props":{"PointLightComponent.Intensity":50000}}},
      {"action":"screenshot","params":{"width":1280,"height":720}}
    ]
  }'
```

```bash
curl -s -X POST http://localhost:30010/nova/undo
```

```bash
curl -s -X POST http://localhost:30020/nova/runtime/pair \
  -H "Content-Type: application/json" \
  -d '{"code":"123456"}'
```

```bash
curl -s "http://localhost:30020/nova/audit?limit=20" \
  -H "X-NovaBridge-Token: <token-from-pair>"
```

```bash
curl -s "http://localhost:30020/nova/events" \
  -H "X-NovaBridge-Token: <token-from-pair>"
```

```bash
curl -s -X POST http://localhost:30020/nova/executePlan \
  -H "Content-Type: application/json" \
  -H "X-NovaBridge-Token: <token-from-pair>" \
  -d '{
    "plan_id":"runtime-plan",
    "steps":[
      {"action":"spawn","params":{"type":"PointLight","label":"RuntimePlanLight","x":0,"y":0,"z":220}},
      {"action":"delete","params":{"name":"RuntimePlanLight"}}
    ]
  }'
```

```bash
curl -s -X POST http://localhost:30020/nova/undo \
  -H "Content-Type: application/json" \
  -H "X-NovaBridge-Token: <token-from-pair>" \
  -d '{}'
```
