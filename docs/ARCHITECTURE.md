# NovaBridge Architecture

## System Layers

1. AI agent or application
2. Tool wrapper (OpenClaw extension, SDK, MCP server)
3. NovaBridge UE5 plugin (HTTP server inside editor)
4. Unreal Engine Editor world/assets/viewport

Optional Blender path:

1. AI agent
2. `nova-blender` extension
3. Blender (Python script + export)
4. NovaBridge `asset/import`
5. UE5 asset + scene placement

## Runtime Model

- HTTP server runs in UE module.
- Route handlers marshal editor work to UE game thread with `AsyncTask`.
- Scene screenshots use a dedicated `SceneCapture2D` actor (`NovaBridge_SceneCapture`).

## Current API Scope

- Scene actor lifecycle + transforms
- Property setting via `Component.Property` syntax
- Asset CRUD and import (OBJ/FBX)
- Mesh primitives + mesh creation
- Material creation and parameter edits
- Viewport screenshot and camera control
- Blueprint create/add-component/compile
- Lighting build and console command execution

## Cross-Platform Notes

- Plugin supports Win64, Mac, Linux, LinuxArm64 in descriptor.
- Blender paths are environment-configurable.
- NovaBridge port can be overridden via `-NovaBridgePort=<port>`.
