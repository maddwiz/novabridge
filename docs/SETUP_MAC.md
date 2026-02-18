# Setup on macOS

## Prerequisites

- Unreal Engine 5.1+
- Xcode command line tools
- Blender 4.0+
- Node.js 18+

## Install Plugin

Copy `NovaBridge` into:

`YourProject/Plugins/NovaBridge`

Build the project/editor.

## Launch UE5

Use Metal and pass a project path:

```bash
UnrealEditor /path/to/YourProject.uproject -metal -RenderOffScreen -nosplash -nosound -unattended -nopause
```

Optional custom port:

```bash
UnrealEditor /path/to/YourProject.uproject -NovaBridgePort=30010 -RenderOffScreen -unattended
```

## Verify

```bash
curl http://localhost:30010/nova/health
curl http://localhost:30010/nova/project/info
```

## Optional API Key

```bash
export NOVABRIDGE_API_KEY=replace-with-secret
UnrealEditor /path/to/YourProject.uproject -NovaBridgeApiKey=replace-with-secret
curl -H "X-API-Key: replace-with-secret" http://localhost:30010/nova/health
```

## Validation Status

- macOS build support exists but must be validated on a real Mac.
- Run `docs/MACOS_SMOKE_TEST.md` before marking macOS as supported in production release notes.

## Blender Path

Default expected binary:

`/Applications/Blender.app/Contents/MacOS/Blender`
