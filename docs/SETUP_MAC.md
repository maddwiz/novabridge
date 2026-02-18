# Setup on macOS

## Prerequisites

- Unreal Engine 5.1+
- Xcode command line tools
- Xcode Metal toolchain component (required for UE shader compile on fresh Xcode installs):
  - `xcodebuild -downloadComponent MetalToolchain`
- Blender 4.0+
- Node.js 18+

## Install Plugin

Copy `NovaBridge` into:

`YourProject/Plugins/NovaBridge`

Build the project/editor.

## Launch UE5

Use Metal and pass a project path (native UE binary path shown from validated run):

```bash
/Users/Shared/Epic\ Games/UE_5.6/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor /path/to/YourProject.uproject -metal -RenderOffScreen -nosplash -nosound -unattended -nopause -stdout -FullStdOutLogOutput -log
```

Optional custom port:

```bash
UnrealEditor /path/to/YourProject.uproject -NovaBridgePort=30010 -RenderOffScreen -unattended
```

## Verify

```bash
curl http://localhost:30010/nova/health
curl http://localhost:30010/nova/project/info
curl -X POST http://localhost:30010/nova/scene/spawn \
  -H "Content-Type: application/json" \
  -d '{"class":"PointLight","label":"MacSetupLight","x":0,"y":0,"z":250}'
curl -X POST http://localhost:30010/nova/asset/import \
  -H "Content-Type: application/json" \
  -d '{"file_path":"/tmp/test.obj","asset_name":"MacSetupMesh","destination":"/Game","scale":100}'
curl "http://localhost:30010/nova/viewport/screenshot?format=raw&width=960&height=540" -o /tmp/novabridge-mac.png
file /tmp/novabridge-mac.png
```

## Optional API Key

```bash
export NOVABRIDGE_API_KEY=replace-with-secret
UnrealEditor /path/to/YourProject.uproject -NovaBridgeApiKey=replace-with-secret
curl -H "X-API-Key: replace-with-secret" http://localhost:30010/nova/health
```

## Validation Status

- Native macOS smoke and release packaging validated on 2026-02-18.
- Evidence and exact command history are in `docs/BUILD_STATUS.md`.

## Blender Path

Default expected binary:

`/Applications/Blender.app/Contents/MacOS/Blender`
