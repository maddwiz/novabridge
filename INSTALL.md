# NovaBridge Install (Customer)

## Supported Now

- Linux ARM64 (validated)
- Windows Win64 (validated)
- macOS (validated)
- Linux x86_64 (pending native validation)

## 5-Step Install

1. Close Unreal Editor.
2. Copy `NovaBridge/` into your UE project at `Plugins/NovaBridge/`.
3. Open your project in UE5 and let it compile modules.
4. Launch editor with your `.uproject` loaded.
5. Verify with:
   - `curl http://localhost:30010/nova/health`
   - `curl http://localhost:30010/nova/project/info`

## Optional API Key Security

- Set `NOVABRIDGE_API_KEY` before launch, or pass `-NovaBridgeApiKey=<key>` to UnrealEditor.
- Then call API with header `X-API-Key: <key>` or `Authorization: Bearer <key>`.

## If It Fails

- Confirm UE project is open (not project browser).
- Confirm port `30010` is free, or launch with `-NovaBridgePort=<port>`.
- Check UE log for `NovaBridge HTTP server started`.
