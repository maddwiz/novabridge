# Setup on Linux

## Prerequisites

- Unreal Engine 5.1+
- Blender 4.0+
- Node.js 18+ (for OpenClaw extensions)

## Install Plugin

```bash
cp -r NovaBridge /path/to/YourProject/Plugins/NovaBridge
```

Rebuild project/editor.

## Launch UE5

Always pass a project path:

```bash
UnrealEditor /path/to/YourProject.uproject -vulkan -RenderOffScreen -nosplash -nosound -unattended -nopause
```

Optional custom NovaBridge port:

```bash
UnrealEditor /path/to/YourProject.uproject -NovaBridgePort=30010 -RenderOffScreen -unattended
```

## Optional User Service (systemd)

Install and run the provided user service (defaults to `NovaBridgeDefault`):

```bash
./scripts/install_linux_service.sh
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

## Blender Bridge Environment

```bash
export NOVABRIDGE_BLENDER_PATH=/usr/bin/blender
export NOVABRIDGE_PORT=30010
export NOVABRIDGE_HOST=localhost
export NOVABRIDGE_EXPORT_DIR=/tmp/novabridge-exports
export NOVABRIDGE_API_KEY=replace-with-secret
```
