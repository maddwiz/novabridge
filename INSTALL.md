# NovaBridge Install Guide

## Supported Targets

- macOS (validated)
- Windows Win64 (validated)
- Linux x64 (validated via self-hosted build workflow)
- Linux ARM64 (validated)

## Fastest Install

macOS/Linux:

```bash
./scripts/setup.sh
```

Windows PowerShell:

```powershell
./scripts/setup_win.ps1
```

These setup scripts copy the plugin, print launch commands, and attempt auto-launch when a local UE binary is found.

## Manual Install

1. Close Unreal Editor.
2. Copy `NovaBridge/` into your project at `Plugins/NovaBridge/`.
3. Open the `.uproject` and let UE compile modules.
4. Launch with explicit NovaBridge port:

```bash
UnrealEditor YourProject.uproject -RenderOffScreen -nosplash -unattended -nopause -NovaBridgePort=30010
```

5. Verify:

```bash
curl http://127.0.0.1:30010/nova/health
curl http://127.0.0.1:30010/nova/project/info
```

## Runtime Install (Packaged Build)

Launch packaged game with runtime module enabled:

```bash
YourGame -NovaBridgeRuntime=1 -NovaBridgeRuntimePort=30020
```

Then pair and request token:

```bash
curl -X POST http://127.0.0.1:30020/nova/runtime/pair \
  -H "Content-Type: application/json" \
  -d '{"code":"123456","role":"automation"}'
```

## Optional API-Key Security

Set one of:
- `NOVABRIDGE_API_KEY`
- `-NovaBridgeApiKey=<key>`

Then send either header:
- `X-API-Key: <key>`
- `Authorization: Bearer <key>`

## Docker Evaluation Harness

```bash
docker run --rm -p 8080:8080 ghcr.io/maddwiz/novabridge:latest
```

The Docker image runs a mock NovaBridge API for SDK/MCP integration testing.

## Troubleshooting

- Confirm UE project is open (not the launcher/project browser only).
- Confirm port `30010` is free, or override with `-NovaBridgePort=<port>`.
- Confirm runtime requests are localhost-only in runtime mode.
- Check UE logs for `NovaBridge server listening` startup lines.
