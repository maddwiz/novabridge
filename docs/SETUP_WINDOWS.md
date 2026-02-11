# Setup on Windows

## Prerequisites

- Unreal Engine 5.1+
- Visual Studio build tools for UE
- Blender 4.0+
- Node.js 18+

## Install Plugin

Copy `NovaBridge` into your project:

`YourProject/Plugins/NovaBridge`

Generate project files and build.

## Launch UE5

Always include a `.uproject` path:

```powershell
UnrealEditor.exe "C:\Projects\YourProject\YourProject.uproject" -dx12 -RenderOffScreen -nosplash -nosound -unattended -nopause
```

Optional custom port:

```powershell
UnrealEditor.exe "C:\Projects\YourProject\YourProject.uproject" -NovaBridgePort=30010 -RenderOffScreen -unattended
```

## Verify

```powershell
curl http://localhost:30010/nova/health
curl http://localhost:30010/nova/project/info
```

## Blender Path

Set `NOVABRIDGE_BLENDER_PATH` if Blender is not in the default location:

`C:\Program Files\Blender Foundation\Blender 4.0\blender.exe`
