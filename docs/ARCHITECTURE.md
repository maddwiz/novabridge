# NovaBridge Architecture

## System Layers

1. AI agent or application
2. Tool wrapper (OpenClaw extension, SDK, MCP server)
3. NovaBridge UE5 plugin modules
4. Unreal Engine world/assets/viewport

Optional Blender path:

1. AI agent
2. `nova-blender` extension
3. Blender (Python script + export)
4. NovaBridge `asset/import`
5. UE5 asset + scene placement

## Module Layout

- `NovaBridgeCore` (shared constants/types + capability registry + policy helpers)
- `NovaBridge` (editor module, editor-first control surface)
- `NovaBridgeRuntime` (runtime module, disabled by default, token/pairing gated)

## Runtime Model

- HTTP server runs in UE module.
- Route handlers marshal editor work to UE game thread with `AsyncTask`.
- Scene screenshots use a dedicated `SceneCapture2D` actor (`NovaBridge_SceneCapture`).
- Capability discovery is exposed via `GET /nova/caps`.
- Structured multi-step execution is exposed via `POST /nova/executePlan`.
- Reversible operation tracking is exposed via `POST /nova/undo`.
- In-memory audit trail is exposed via `GET /nova/audit`.
- Event WebSocket discovery is exposed via `GET /nova/events` (default socket `ws://localhost:30012`).
- Event discovery supports optional type-filter metadata (`types=spawn,error`) and reports `supported_types` + `pending_by_type`.
- Event sockets support per-client subscription control (`subscribe` / `clear`) with explicit ACK messages and filter-aware client counts.
- Runtime control server defaults to `127.0.0.1:30020` and is enabled with `-NovaBridgeRuntime=1`.
- Runtime request handling enforces localhost host access before auth/dispatch.
- Runtime pairing endpoint: `POST /nova/runtime/pair`.
- Runtime audit trail endpoint: token-gated `GET /nova/audit`.
- Runtime events endpoint: token-gated `GET /nova/events` with WebSocket stream (`ws://localhost:30022` by default).
- Editor and Runtime event streams emit typed events: `audit`, `spawn`, `delete`, `plan_step`, `plan_complete`, `error`.

## Control Policy Layer

- Roles:
  - `admin`
  - `automation`
  - `read_only`
- Role can be set per request via `X-NovaBridge-Role`.
- Default role is configurable via `NOVABRIDGE_DEFAULT_ROLE` or `-NovaBridgeDefaultRole=`.
- Role-aware constraints are enforced before route execution:
  - route access
  - per-minute rate limiting
  - spawn class allow-list (non-admin)
  - spawn transform bounds
  - max spawn count per plan

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
