# Build Status

## Last Updated

- Date: 2026-02-25
- Environments:
  - Linux ARM64 host (`aarch64`) validation previously completed.
  - macOS native validation completed on `MacBookPro17,1` (Apple M1, 8 GB RAM), macOS `15.6.1` (`24G90`), Xcode `26.2` (`17C52`), Unreal Engine `5.6.1-44394996`.
  - Windows Win64 native validation completed on `DESKTOP-QNVIB5M`, Unreal Engine `5.7.3` (`C:\Program Files\Epic Games\UE_5.7`), Visual Studio Build Tools 2022 (`17.14.27`), MSVC `14.44.35207`, Windows SDK `10.0.26100.0`.

## macOS Validation Refresh (v0.9.5-dev)

- Date: 2026-02-24
- Run root:
  - `/tmp/novabridge-smoke-20260224-181954`
- Source project:
  - `/tmp/novabridge-smoke-20260224-181954/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Build command:
  - `"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" UnrealEditor Mac Development -Project="<...>/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`

Editor-mode checks (port `30110`, events port `30112`):
- `GET /nova/health`, `GET /nova/caps`, `GET /nova/events`, `GET /nova/audit`
- `POST /nova/executePlan` (spawn + delete)
- `POST /nova/undo` (spawn-undo probe)
- Sequencer regression path:
  - `POST /nova/sequencer/create`
  - `POST /nova/sequencer/scrub`
  - `POST /nova/sequencer/stop`
- Result: passed; UE `<5.7` scrub fallback no recursion/crash in UE `5.6.1` smoke.

Runtime-mode checks (port `30120`, events port `30122`):
- Enabled with `-NovaBridgeRuntime=1 -NovaBridgeRuntimePort=30120 -NovaBridgeRuntimeEventsPort=30122`
- Pairing validated via `POST /nova/runtime/pair` (token returned)
- Token-gated checks:
  - `GET /nova/health`, `GET /nova/caps`, `GET /nova/events`, `GET /nova/audit`
  - `POST /nova/executePlan` (spawn)
- Result: passed; runtime `events_ws_port` surfaced in health and `/nova/events` metadata.

### macOS Event Stream Validation Refresh

- Date: 2026-02-24
- Run root:
  - `/tmp/novabridge-smoke-20260224-183509`
- Build command:
  - `"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" UnrealEditor Mac Development -Project="<...>/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`

Editor checks:
- `POST /nova/executePlan` (spawn + delete) succeeded with `success_count=2`, `error_count=0`.
- `GET /nova/events` returned typed metadata:
  - `supported_types`: `audit`, `spawn`, `delete`, `plan_step`, `plan_complete`, `error`
  - `pending_by_type` counters present
- `GET /nova/events?types=spawn,error` returned filtered metadata with `filtered_pending_events`.
- Sequencer regression recheck (`create` + `scrub`) succeeded.

Runtime checks:
- Enabled with `-NovaBridgeRuntime=1 -NovaBridgeRuntimePort=30220 -NovaBridgeRuntimeEventsPort=30222`.
- `POST /nova/executePlan` with runtime spawn `label` + delete by that name succeeded (`success_count=2`, `error_count=0`).
- Runtime `GET /nova/events` and `GET /nova/events?types=spawn,error` returned type-aware metadata and counters.

### macOS Subscription Filter Validation Refresh

- Date: 2026-02-25
- Run root:
  - `/tmp/novabridge-smoke-20260224-190608`
- Build command:
  - `"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" UnrealEditor Mac Development -Project="<...>/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`

Editor checks (port `30470`, events port `30472`):
- WebSocket client receives subscription control handshake:
  - `status=ready`, then `status=ok` after `{"action":"subscribe","types":["spawn"]}`.
- `POST /nova/scene/spawn` + `POST /nova/scene/delete` run after subscription ACK.
- Event stream filter result: only `spawn` events delivered to this client.
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-190608/artifacts/editor-validation-4`

Runtime checks (port `30460`, events port `30462`):
- WebSocket client subscribes to `spawn` and waits for `status=ok` before pairing/execution.
- `POST /nova/runtime/pair` + token-gated `POST /nova/executePlan` (spawn + delete) succeeded.
- Event stream filter result: only `spawn` events delivered to this client.
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-190608/artifacts/runtime-validation-5`

Note:
- As of the 2026-02-25 subscription-gate fix, event delivery is paused until each socket receives subscription `status=ok`.

### macOS ExecutePlan Event + Runtime Undo Validation

- Date: 2026-02-25
- Run root:
  - `/tmp/novabridge-smoke-20260224-190608`
- Build command:
  - `"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" UnrealEditor Mac Development -Project="<...>/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`

Editor checks (port `30510`, events port `30512`):
- WebSocket client subscribed to `spawn` and waited for `status=ok`.
- `POST /nova/executePlan` (spawn + delete) now produced typed `spawn` events for filtered subscribers.
- Validation result: `editor_executeplan_ws_filter_validation=ok`.
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-190608/artifacts/editor-validation-5`

Runtime checks (port `30540`, events port `30542`):
- Pairing + token flow succeeded (`POST /nova/runtime/pair`).
- `GET /nova/caps` includes `undo` capability.
- `POST /nova/executePlan` (spawn) followed by token-gated `POST /nova/undo` succeeded.
- Follow-up delete check confirms actor removal (`Actor not found` after undo).
- Validation result: `runtime_undo_validation=ok`.
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-190608/artifacts/runtime-validation-7`

### macOS ExecutePlan Schema Validation Refresh

- Date: 2026-02-25
- Run root:
  - `/tmp/novabridge-smoke-20260224-190608`

Editor checks (port `30560`):
- Invalid plan with unknown top-level field rejected with HTTP 400.
- Error payload: `Schema validation failed: Unknown plan field: unknown_top`.
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-190608/artifacts/schema-validation-editor`

Runtime checks (port `30570`, events port `30572`):
- Invalid runtime plan with unknown spawn param rejected with HTTP 400.
- Error payload: `Schema validation failed at step 0: Unknown spawn param field: bad_param`.
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-190608/artifacts/schema-validation-runtime`

### macOS Event Subscription Gate Fix Validation

- Date: 2026-02-25
- Run root:
  - `/tmp/novabridge-smoke-20260224-195531`

Editor checks (port `30610`, events port `30612`):
- WebSocket `ready` payload now reports:
  - `subscription_confirmed=false`
  - `events_paused_until_subscribe=true`
- Pre-subscription `POST /nova/scene/spawn` executes successfully but emits no event to that socket.
- After `{"action":"subscribe","types":["spawn","error"]}` and `status=ok`, spawn events are delivered.
- `GET /nova/events` now includes `clients_pending_subscription`.
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/subscription-gating-editor`

Runtime checks (port `30620`, events port `30622`):
- Runtime pairing + token auth validated (`POST /nova/runtime/pair`).
- WebSocket `ready` payload reports paused delivery until subscribe ACK.
- Pre-subscription runtime `POST /nova/executePlan` spawn emits no socket event.
- Post-subscription runtime `POST /nova/executePlan` spawn emits typed `spawn` event.
- `GET /nova/events` (token-gated) includes `clients_pending_subscription`.
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/subscription-gating-runtime`

### macOS Capability Role Filter Validation

- Date: 2026-02-25
- Run root:
  - `/tmp/novabridge-smoke-20260224-195531`

Editor checks (port `30730`, events port `30732`):
- `GET /nova/caps` (default admin role) returns:
  - `spawn`, `delete`, `set`, `screenshot`, `events`, `executePlan`, `undo`
- `GET /nova/caps` with `X-NovaBridge-Role: automation` returns the same allowed action set.
- `GET /nova/caps` with `X-NovaBridge-Role: read_only` returns only:
  - `screenshot`, `events`
- Validation status:
  - `caps_role_filter_validation=ok`
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/caps-role-filter-2`

### macOS Caps Permission Snapshot Validation

- Date: 2026-02-25
- Run root:
  - `/tmp/novabridge-smoke-20260224-195531`

Editor checks (port `30910`, events port `30912`):
- `GET /nova/caps` (admin) includes `permissions` object with:
  - role/mode
  - spawn limits + bounds + class allow-list
  - execute-plan allowed actions + max steps + route rate limits
- `GET /nova/caps` with `X-NovaBridge-Role: read_only` includes:
  - `permissions.executePlan.allowed=false`
  - `permissions.executePlan.allowed_actions=["screenshot"]`
  - `permissions.spawn.allowed=false`
  - zeroed write-route limits in snapshot (`executePlan=0`, `scene_spawn=0`, `scene_delete=0`)

Runtime checks (port `30920`, events port `30922`):
- Runtime pairing + token auth succeeded (`POST /nova/runtime/pair`).
- Token-gated `GET /nova/caps` includes `permissions` object with:
  - `localhost_only=true`
  - `pairing_required=true`
  - `token_required=true`
  - runtime spawn/execute limits and allowed actions

- Artifact root:
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/caps-permissions-1`

### macOS Runtime Command-Dispatch Refactor Validation

- Date: 2026-02-25
- Run root:
  - `/tmp/novabridge-smoke-20260224-195531`

Runtime checks (port `30820`, events port `30822`):
- Pairing + token flow succeeded (`POST /nova/runtime/pair`).
- `POST /nova/executePlan` with steps:
  - `spawn` (`PointLight`, label `RuntimeRouterLight`)
  - `set` (location update)
  - `delete` (cleanup)
- Result: `success_count=3`, `error_count=0`.
- Confirms runtime execute-plan behavior after shared core command-dispatch integration.
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/runtime-command-router`

### macOS Editor Shared Step-Extraction Validation

- Date: 2026-02-25
- Run root:
  - `/tmp/novabridge-smoke-20260224-195531`
- Build command:
  - `"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" UnrealEditor Mac Development -Project="/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
- Result:
  - `Succeeded`
- Notes:
  - Rebuild included `NovaBridgeModule.cpp` changes wiring editor `executePlan` step parsing to `NovaBridgeCore::ExtractPlanStep`.
  - Runtime and core modules also recompiled cleanly in the same pass.

### macOS Editor Command-Router Refactor Validation

- Date: 2026-02-25
- Run root:
  - `/tmp/novabridge-smoke-20260224-195531`

Editor checks (port `30930`, events port `30932`):
- `POST /nova/executePlan` with steps:
  - `spawn` (`PointLight`, label `EditorRouterLight`)
  - `set` (location update)
  - `delete` (cleanup)
- Result: `success_count=3`, `error_count=0`.
- Confirms editor execute-plan behavior after shared core command-router integration.
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/editor-command-router`

### NovaBridge Studio Scaffold Validation

- Date: 2026-02-25
- Path:
  - `/Users/desmondpottle/Documents/New project/novabridge/novabridge-studio`
- Result:
  - `npm install` succeeded.
  - `npm run build` succeeded (`tsc -b && vite build`).
  - Added and compiled plan-permission preflight (`/nova/caps` `permissions`) before execute.
  - Added and compiled Connect/PlanPreview policy UX (snapshot display + inline execute-block reasons).
- Note:
  - `pnpm` is not installed on this validation host; package scripts still include `pnpm dev`, `pnpm build`, `pnpm tauri dev`, and `pnpm tauri build`.

## macOS Validation (Completed)

### Toolchain discovery and setup

- UE editor binary:
  - `/Users/Shared/Epic Games/UE_5.6/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor`
- Xcode and command line tools verified:
  - `xcode-select -p`
  - `xcodebuild -version`
- Required one-time fix on this Mac before Unreal launch:
  - `xcodebuild -downloadComponent MetalToolchain`

### Source plugin smoke (project copy + `Plugins/NovaBridge`)

- Run directory:
  - `artifacts-mac/run-20260218-112826`
- Source test project:
  - `artifacts-mac/run-20260218-112826/MacSmokeSource/NovaBridgeDefault.uproject`
- Build command:
  - `"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" UnrealEditor Mac Development -Project="<...>/MacSmokeSource/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
- Runtime launch command:
  - `UnrealEditor <...>/MacSmokeSource/NovaBridgeDefault.uproject -metal -RenderOffScreen -nosplash -nosound -unattended -nopause -NovaBridgePort=30010 -stdout -FullStdOutLogOutput -log`

Passed runtime checks on port `30010`:
- `GET /nova/health` => `status=ok`, `version=0.9.0`, `routes=30`
- `POST /nova/scene/spawn` => created `PointLight_0` (`MacSmokeLight`)
- `POST /nova/asset/import` with local OBJ + `scale=100` => `status=ok`, asset `/Game/MacSmokeMesh.MacSmokeMesh`
- `GET /nova/viewport/screenshot?format=raw` => PNG bytes (`89 50 4E 47 ...`)
- `POST /nova/scene/delete` cleanup => `status=ok`

Evidence:
- `artifacts-mac/run-20260218-112826/source-validation/health.pretty.json`
- `artifacts-mac/run-20260218-112826/source-validation/spawn.pretty.json`
- `artifacts-mac/run-20260218-112826/source-validation/import.pretty.json`
- `artifacts-mac/run-20260218-112826/source-validation/delete.pretty.json`
- `artifacts-mac/run-20260218-112826/source-validation/screenshot-source.png`
- `artifacts-mac/run-20260218-112826/source-validation/screenshot-magic.txt`
- `artifacts-mac/run-20260218-112826/source-validation/unreal-source.log`

### Golden Path on macOS (Completed)

- Script used (mac-compatible updates applied):
  - `examples/curl/golden_path.sh`
- Command:
  - `NOVABRIDGE_HOST=127.0.0.1 NOVABRIDGE_PORT=30010 bash examples/curl/golden_path.sh`
- Result:
  - Full `[1/6]` through `[6/6]` pass
  - Screenshot output valid PNG

Evidence:
- `artifacts-mac/run-20260218-112826/source-validation/golden-path.txt`
- `artifacts-mac/run-20260218-112826/source-validation/golden-path-screenshot.png`

### Packaging and packaged-plugin retest (Completed)

- Packaging command:
  - `./scripts/package_release.sh v0.9.0`
- Package output:
  - `dist/NovaBridge-v0.9.0.zip`

Packaged-plugin second project validation:
- Second clean project:
  - `artifacts-mac/run-20260218-112826/MacSmokePackaged/NovaBridgeDefault.uproject`
- Packaged plugin installed from:
  - `artifacts-mac/run-20260218-112826/packaged-validation/unzipped/NovaBridge-v0.9.0/NovaBridge`
- Build command:
  - `Build.sh UnrealEditor Mac Development -Project="<...>/MacSmokePackaged/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
- Runtime launch command:
  - `UnrealEditor <...>/MacSmokePackaged/NovaBridgeDefault.uproject -metal -RenderOffScreen -nosplash -nosound -unattended -nopause -NovaBridgePort=30011 -stdout -FullStdOutLogOutput -log`

Passed runtime checks on port `30011`:
- `GET /nova/health` => `status=ok`, `version=0.9.0`, `routes=30`
- `POST /nova/scene/spawn` => created `MacPackagedLight`
- `POST /nova/asset/import` with OBJ + `scale=100` => `status=ok`
- `GET /nova/viewport/screenshot?format=raw` => valid PNG bytes
- `POST /nova/scene/delete` cleanup => `status=ok`

Package hygiene:
- Zip content scanned and verified no `Intermediate/`, `Saved/`, `.log`, or `docs/NOVABRIDGE_HANDOFF.md`.

Evidence:
- `artifacts-mac/run-20260218-112826/packaged-validation/build-packaged.log`
- `artifacts-mac/run-20260218-112826/packaged-validation/health.pretty.json`
- `artifacts-mac/run-20260218-112826/packaged-validation/import.pretty.json`
- `artifacts-mac/run-20260218-112826/packaged-validation/screenshot-packaged.png`
- `artifacts-mac/run-20260218-112826/packaged-validation/screenshot-magic.txt`
- `artifacts-mac/run-20260218-112826/packaged-validation/zip-contents.txt`

## Linux ARM64 Validation (Previously Completed)

- Linux ARM64 clean rebuild completed successfully:
  - `UnrealEditor LinuxArm64 Development -Clean`
  - `UnrealEditor LinuxArm64 Development -SkipPreBuildTargets`
- Runtime validation passed on ARM64:
  - Golden path smoke script succeeded.
  - `nova-ue5-editor.service` active on port `30010`.
  - `/nova/health` returns `status: ok`, `version: 0.9.0`, and 30 routes.
  - `/nova/project/info` returns loaded project fields.
  - `/nova/viewport/screenshot?format=raw` returns `image/png`.
  - `OPTIONS /nova/scene/list` returns expected CORS headers.
  - `POST /nova/asset/import` with OBJ + `scale` succeeds.
  - `POST /nova/scene/set-property` alias path succeeds.
  - Optional API key mode validated (`401` without key, `200` with key).

## Outstanding

- Linux x86_64 native smoke remains blocked on ARM host dependency (`TextureFormatOodle ... liboo2texlinux64.2.9.6.so`).

## Windows Win64 Validation (Completed)

### Toolchain discovery and setup

- UE editor binary:
  - `C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe`
- Visual Studio Build Tools 2022:
  - `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe`
- .NET Framework Developer Pack:
  - `Microsoft.DotNet.Framework.DeveloperPack_4` (4.8.1)

### Source plugin smoke (project copy + `Plugins/NovaBridge`)

- Run directory:
  - `artifacts-win/run-20260221-145844`
- Source test project:
  - `artifacts-win/run-20260221-145844/NovaBridgeDefault.uproject`
- Commit:
  - `7c7f6ce195960eb9d4a9402bacb253883a789776`
- Build command:
  - `Build.bat UnrealEditor Win64 Development -Project="<...>\\NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
- Runtime launch command:
  - `UnrealEditor.exe "<...>\\NovaBridgeDefault.uproject" -dx12 -RenderOffScreen -nosplash -nosound -unattended -nopause -NovaBridgePort=30010`

Passed runtime checks on port `30010`:
- `GET /nova/health` => `status=ok`
- `GET /nova/stream/status` => `ws_url` + defaults present
- `POST /nova/scene/spawn` => created `GoldenPathLight`
- `POST /nova/mesh/primitive` => created `WinSmokeCube`
- `POST /nova/asset/import` with OBJ + `scale=100` => `status=ok`
- `GET /nova/viewport/screenshot?format=raw` => PNG bytes (`89 50 4E 47 ...`)
- Sequencer (UE 5.7-sensitive): create, play (start_time > 0), scrub, stop succeeded
- `POST /nova/scene/delete` cleanup => `status=ok`
- OpenClaw extensions not re-validated in this run
- Build warnings cleared after plugin metadata update (EditorScriptingUtilities, WebSocketNetworking, MovieRenderPipeline)

Evidence:
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/health.json`
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/stream-status.json`
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/scene-spawn.json`
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/mesh-primitive.json`
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/asset-import.json`
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/scene-delete.json`
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/viewport-raw.png`
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/viewport-raw-check.json`
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/sequencer-create.json`
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/sequencer-play.json`
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/sequencer-scrub.json`
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/sequencer-stop.json`
- `artifacts-win/run-20260221-145844/source-validation-20260221-153146/NovaBridgeDefault.log`

### Packaging and packaged-plugin retest (Completed)

- Packaging command:
  - `pwsh scripts/package_release_win.ps1 -Version v0.9.0`
- Package output:
  - `dist/NovaBridge-v0.9.0.zip`

Packaged-plugin second project validation:
- Second clean project:
  - `artifacts-win/run-20260221-145844/packaged-validation-20260221-151836/WinSmokePackaged/NovaBridgeDefault.uproject`
- Packaged plugin installed from:
  - `artifacts-win/run-20260221-145844/packaged-validation-20260221-151836/unzipped` (`NovaBridge.uplugin` + `Source`)
- Build command:
  - `Build.bat UnrealEditor Win64 Development -Project="<...>\\WinSmokePackaged\\NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
- Runtime launch command:
  - `UnrealEditor.exe "<...>\\WinSmokePackaged\\NovaBridgeDefault.uproject" -dx12 -RenderOffScreen -nosplash -nosound -unattended -nopause -NovaBridgePort=30011`

Passed runtime checks on port `30011`:
- `GET /nova/health` => `status=ok`
- `GET /nova/stream/status` => `ws_url` + defaults present
- `POST /nova/scene/spawn` => created `PackagedLight`
- `POST /nova/asset/import` with OBJ + `scale=100` => `status=ok`
- `GET /nova/viewport/screenshot?format=raw` => PNG bytes (`89 50 4E 47 ...`)
- `POST /nova/scene/delete` cleanup => `status=ok`

Package hygiene:
- Zip content scanned and verified no `Intermediate/`, `Saved/`, `.log`, or `DerivedDataCache`.

Evidence:
- `artifacts-win/run-20260221-145844/packaged-validation-20260221-151836/packaged-validation/health.json`
- `artifacts-win/run-20260221-145844/packaged-validation-20260221-151836/packaged-validation/stream-status.json`
- `artifacts-win/run-20260221-145844/packaged-validation-20260221-151836/packaged-validation/scene-spawn.json`
- `artifacts-win/run-20260221-145844/packaged-validation-20260221-151836/packaged-validation/asset-import.json`
- `artifacts-win/run-20260221-145844/packaged-validation-20260221-151836/packaged-validation/scene-delete.json`
- `artifacts-win/run-20260221-145844/packaged-validation-20260221-151836/packaged-validation/viewport-raw.png`
- `artifacts-win/run-20260221-145844/packaged-validation-20260221-151836/packaged-validation/viewport-raw-check.json`
- `artifacts-win/run-20260221-145844/packaged-validation-20260221-151836/packaged-validation/NovaBridgeDefault.log`
