# NovaBridge Smoke Test Checklist

## Golden Path

1. Launch UE with a project loaded.
2. `GET /nova/health` returns `status: ok`.
3. `POST /nova/scene/spawn` creates a primitive actor.
4. `POST /nova/asset/import` imports OBJ with `scale`.
5. `GET /nova/viewport/screenshot?format=raw` returns PNG bytes.
6. `POST /nova/scene/delete` removes test actor.

## CORS

1. `OPTIONS` to any route returns:
   - `Access-Control-Allow-Origin: *`
   - `Access-Control-Allow-Methods: GET, POST, OPTIONS`
   - `Access-Control-Allow-Headers` includes `X-API-Key`

## Optional API Key Mode

1. Launch with `NOVABRIDGE_API_KEY` or `-NovaBridgeApiKey=<key>`.
2. Verify unauthorized request returns HTTP `401`.
3. Verify request with `X-API-Key` succeeds.

