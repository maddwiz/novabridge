# NovaBridge Assistant Server (Studio + Planner)

Node.js sidecar that adds:

- `assistant_engine.js` for prompt -> plan generation
- `command_catalog.js` risk classification
- Browser Studio UI at `/nova/studio`
- Scene-aware planning using `/nova/scene/list`
- Capability-aware planning using `/nova/caps`

## Support Status

This server is **experimental** and not part of the supported core NovaBridge plugin surface.
It is provided as an optional sidecar and may change without compatibility guarantees.

## Run

```bash
node assistant-server/server.js
```

Default bind: `http://127.0.0.1:30016`
Studio URL: `http://127.0.0.1:30016/nova/studio`

## Environment

- `NOVABRIDGE_ASSISTANT_PORT` (default `30016`)
- `NOVABRIDGE_HOST` (default `127.0.0.1`)
- `NOVABRIDGE_PORT` (default `30010`)
- `NOVABRIDGE_API_KEY` (optional)
- `NOVABRIDGE_ASSISTANT_PROVIDER` (`mock`, `openai`, `anthropic`, `ollama`, `custom`)
- `OPENAI_API_KEY` + `NOVABRIDGE_ASSISTANT_OPENAI_MODEL` (optional)
- `ANTHROPIC_API_KEY` + `NOVABRIDGE_ASSISTANT_ANTHROPIC_MODEL` (optional)
- `OLLAMA_HOST` + `NOVABRIDGE_ASSISTANT_OLLAMA_MODEL` (optional)
- `NOVABRIDGE_ASSISTANT_CUSTOM_URL` (optional)
- `NOVABRIDGE_ASSISTANT_CUSTOM_HEADER_KEY` / `NOVABRIDGE_ASSISTANT_CUSTOM_HEADER_VALUE` (optional)

If provider config is missing, planner falls back to `mock` mode.

## Endpoints

- `GET /assistant/health`
- `GET /assistant/catalog`
- `POST /assistant/plan`
- `POST /assistant/execute`
- `GET /nova/studio`

## Validation

```bash
node --test assistant-server/tests/*.test.js
node --check assistant-server/command_catalog.js
node --check assistant-server/assistant_engine.js
node --check assistant-server/server.js
```
