# Build Status

## Last Updated

- Date: 2026-02-25
- Environments:
  - Linux ARM64 host (`aarch64`) validation previously completed.
  - macOS native validation completed on `MacBookPro17,1` (Apple M1, 8 GB RAM), macOS `15.6.1` (`24G90`), Xcode `26.2` (`17C52`), Unreal Engine `5.6.1-44394996`.
  - Windows Win64 native validation completed on `DESKTOP-QNVIB5M`, Unreal Engine `5.7.3` (`C:\Program Files\Epic Games\UE_5.7`), Visual Studio Build Tools 2022 (`17.14.27`), MSVC `14.44.35207`, Windows SDK `10.0.26100.0`.

## NovaBridge Studio Hardening Validation (macOS)

- Date: 2026-02-25
- Working directory:
  - `/Users/desmondpottle/Documents/New project/novabridge/novabridge-studio`
- Commands:
  - `npm test`
  - `npm run build`
  - `npm run tauri -- --version`
- Result:
  - `Succeeded`
- Notes:
  - Vitest suite passed (`3` files / `10` tests): schema validation, policy preflight, provider adapter parsing/prompt helpers.
  - TypeScript + Vite production build completed (`dist/assets/index-Djwfm5Eg.js`).
  - Confirmed Tauri CLI availability (`tauri-cli 2.10.0`).
  - Studio now compiles with live `/nova/events` discovery + WebSocket subscription flow (ACK-gated `subscribe`).
  - Validated studio hardening changes: strict plan validation, provider JSON extraction improvements, API key forwarding, fallback mapping expansion, and per-step execute logging.

## macOS Sequencer-Render Handler Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260225-102319/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeSequencerRenderHandlers.cpp` and moved `POST /nova/sequencer/render` there.
  - Kept shared playback helper (`NovaBridgeSetPlaybackTime`) in `NovaBridgeSequencerHandlers.cpp` and exposed it through `NovaBridgeEditorInternals.h`.
  - `NovaBridgeSequencerHandlers.cpp` reduced to `524` lines; render-specific file is `122` lines.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.
  - Python test refresh passed:
    - `python3 -m unittest discover -s python-sdk/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)
    - `python3 -m unittest discover -s mcp-server/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)

## macOS HTTP-Server Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260225-085030/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeHttpServer.cpp` and moved HTTP bootstrap, route binding, auth, CORS, and JSON response helpers there.
  - `NovaBridgeModule.cpp` line count reduced to `23` (lifecycle-only).
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.
  - Python test refresh passed:
    - `python3 -m unittest discover -s python-sdk/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)
    - `python3 -m unittest discover -s mcp-server/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)

## macOS Runtime HTTP-Helper Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260225-085753/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeRuntimeHttpHelpers.cpp` and moved runtime CORS/auth/request-body/response helper logic there.
  - `NovaBridgeRuntimeModule.cpp` line count reduced to `1809`.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.
  - Python test refresh passed:
    - `python3 -m unittest discover -s python-sdk/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)
    - `python3 -m unittest discover -s mcp-server/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)

## macOS Runtime Event-Handler Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260225-090446/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeRuntimeEventHandlers.cpp` and moved runtime event WebSocket lifecycle, subscription parsing, queue pump, and `/nova/events` metadata handler there.
  - Promoted runtime logging to a shared module log category (`LogNovaBridgeRuntime`) so extracted runtime units keep unified logging.
  - `NovaBridgeRuntimeModule.cpp` line count reduced to `1401`.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.
  - Python test refresh passed:
    - `python3 -m unittest discover -s python-sdk/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)
    - `python3 -m unittest discover -s mcp-server/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)

## macOS Runtime ExecutePlan-Handler Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260225-091303/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeRuntimeExecutePlanHandlers.cpp` and moved runtime `POST /nova/executePlan`, runtime `POST /nova/undo`, plus runtime actor/property utility helpers there.
  - `NovaBridgeRuntimeModule.cpp` line count reduced to `643`.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.
  - Python test refresh passed:
    - `python3 -m unittest discover -s python-sdk/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)
    - `python3 -m unittest discover -s mcp-server/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)

## macOS Runtime Control-Handler Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260225-100235/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeRuntimeControlHandlers.cpp` and moved runtime `GET /nova/health`, `GET /nova/caps`, `GET /nova/audit`, and `POST /nova/runtime/pair` there.
  - Added runtime member helpers `RegisterRuntimeCapabilities()` and `BuildRuntimePermissionsSnapshot()` in support of the control split.
  - `NovaBridgeRuntimeModule.cpp` line count reduced to `366`.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.
  - Python test refresh passed:
    - `python3 -m unittest discover -s python-sdk/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)
    - `python3 -m unittest discover -s mcp-server/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)

## macOS Runtime State-Helper Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260225-100842/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeRuntimeState.cpp` and moved runtime event queue normalization + audit push + undo push/pop helpers there.
  - `NovaBridgeRuntimeModule.cpp` line count reduced to `271`.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.
  - Python test refresh passed:
    - `python3 -m unittest discover -s python-sdk/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)
    - `python3 -m unittest discover -s mcp-server/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)

## macOS Runtime HTTP-Server Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260225-101459/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeRuntimeHttpServer.cpp` and moved runtime server enable/config/bootstrap/route-binding there.
  - `NovaBridgeRuntimeModule.cpp` is now lifecycle-only (`33` lines).
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.
  - Python test refresh passed:
    - `python3 -m unittest discover -s python-sdk/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)
    - `python3 -m unittest discover -s mcp-server/tests -p 'test_*.py'` (`Ran 2 tests ... OK`)

## macOS Editor-Policy-State Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260225-083648/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeEditorPolicyState.cpp` and moved shared role/policy/rate-limit/audit/event-queue state there.
  - Moved `PumpEventSocketQueue` implementation to `NovaBridgeWebSocketHandlers.cpp` and switched it to drain pending events via internals helper.
  - `NovaBridgeModule.cpp` line count reduced to `480`.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

## macOS Editor-Utility Helper Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260225-003903/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeEditorUtilityHelpers.cpp` and moved shared actor/property helper functions there.
  - `NovaBridgeModule.cpp` line count reduced to `872`.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

## macOS WebSocket-Handler Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260225-003150/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeWebSocketHandlers.cpp` and moved stream/events WebSocket server lifecycle + stream ticker/frame pump there.
  - `NovaBridgeModule.cpp` line count reduced to `1316`.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

## macOS Execute-Plan Handler Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260225-000507/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeExecutePlanHandlers.cpp` and moved `POST /nova/executePlan` there.
  - `NovaBridgeModule.cpp` line count reduced to `1835`.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

## macOS Control-Handler Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-234835/artifacts/executeplan-smoke`
- Notes:
  - Added `NovaBridgeControlHandlers.cpp` and moved `/nova/health`, `/nova/project/info`, `/nova/caps`, `/nova/events`, `/nova/audit`, `/nova/undo` there.
  - Added shared internal helper accessors/snapshots in `NovaBridgeEditorInternals.h` to support control-plane decomposition.
  - `NovaBridgeModule.cpp` line count reduced to `2367`.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

## macOS Scene-Mutation Handler Consolidation Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-232643/artifacts/executeplan-smoke`
- Notes:
  - Scene mutation routes (`spawn`, `delete`, `set-property`) now compile from `NovaBridgeSceneHandlers.cpp`.
  - Added `NovaBridgeEditorInternals.h` for shared editor helper declarations used by extracted handler units.
  - `NovaBridgeModule.cpp` line count reduced to `2752`.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

## macOS Mesh+Viewport+PCG Handler Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-231624/artifacts/executeplan-smoke`
- Notes:
  - `NovaBridgeMeshHandlers.cpp`, `NovaBridgeViewportHandlers.cpp`, and `NovaBridgePcgHandlers.cpp` compiled and linked with `NovaBridge` module.
  - Shared `LogNovaBridge` category export/import was required so extracted handler files can continue using unified logging.
  - `NovaBridgeModule.cpp` line count reduced to `2962`.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

## macOS Optimize-Handler Split Validation

- Date: 2026-02-25
- Command:
  - `NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Source project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-221525/artifacts/executeplan-smoke`
- Notes:
  - `NovaBridgeOptimizeHandlers.cpp` compiled and linked as part of `NovaBridge` module.
  - `run-summary.json` reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

## Unit Test Refresh

- Date: 2026-02-25
- Python SDK:
  - `python3 -m unittest discover -s python-sdk/tests -p 'test_*.py'`
  - Result: `Ran 2 tests ... OK`
- MCP server:
  - `python3 -m unittest discover -s mcp-server/tests -p 'test_*.py'`
  - Result: `Ran 2 tests ... OK`
- C++ integration check:
  - `"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" UnrealEditor Mac Development -Project="/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
  - Result: `Succeeded` (includes new `NovaBridgePlanSchemaTests.cpp` in module sources)

## macOS Sequencer-Handler Split Validation

- Date: 2026-02-25
- Source project:
  - `/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Command:
  - `NOVABRIDGE_PROJECT=/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-222905/artifacts/executeplan-smoke`
- Notes:
  - `NovaBridgeSequencerHandlers.cpp` compiled and linked with `NovaBridge` module.
  - Smoke summary reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

## macOS Scene-Handler Split Validation

- Date: 2026-02-25
- Source project:
  - `/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Command:
  - `NOVABRIDGE_PROJECT=/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-223414/artifacts/executeplan-smoke`
- Notes:
  - `NovaBridgeSceneHandlers.cpp` compiled and linked with `NovaBridge` module.
  - `NovaBridgeModule.cpp` line count reduced to `4996` after scene handler extraction.
  - Smoke summary reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

## macOS Blueprint+Stream Handler Split Validation

- Date: 2026-02-25
- Source project:
  - `/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Command:
  - `NOVABRIDGE_PROJECT=/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-225221/artifacts/executeplan-smoke`
- Notes:
  - `NovaBridgeBlueprintBuildHandlers.cpp` and `NovaBridgeStreamHandlers.cpp` compiled and linked with `NovaBridge` module.
  - `NovaBridgeModule.cpp` line count reduced to `4698`.
  - Smoke summary reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

## macOS Material Handler Split Validation

- Date: 2026-02-25
- Source project:
  - `/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Command:
  - `NOVABRIDGE_PROJECT=/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-225726/artifacts/executeplan-smoke`
- Notes:
  - `NovaBridgeMaterialHandlers.cpp` compiled and linked with `NovaBridge` module.
  - `NovaBridgeModule.cpp` line count reduced to `4474`.
  - Smoke summary reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

## macOS Asset Handler Split Validation

- Date: 2026-02-25
- Source project:
  - `/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Command:
  - `NOVABRIDGE_PROJECT=/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Result:
  - `Succeeded` (build + editor/runtime execute-plan smoke)
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-230608/artifacts/executeplan-smoke`
- Notes:
  - `NovaBridgeAssetHandlers.cpp` compiled and linked with `NovaBridge` module.
  - `NovaBridgeModule.cpp` line count reduced to `3953`.
  - Smoke summary reported editor `success_count=2/error_count=0` and runtime `success_count=2/error_count=0`.

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

### macOS Shared Plan-Events Refactor Validation

- Date: 2026-02-25
- Run root:
  - `/tmp/novabridge-smoke-20260224-195531`
- Build command:
  - `"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" UnrealEditor Mac Development -Project="/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
- Result:
  - `Succeeded`
- Notes:
  - Added shared core plan-event builders in `NovaBridgeCore` (`NovaBridgePlanEvents`).
  - Editor and Runtime `executePlan` now use the shared builders for `plan_step`/`error` and `plan_complete` event payloads.

Editor checks (port `31030`, events port `31032`):
- `POST /nova/executePlan` with `spawn` + `delete` returned:
  - `status=ok`, `success_count=2`, `error_count=0`
- Artifacts:
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/plan-events-shared-refresh2/editor-health.json`
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/plan-events-shared-refresh2/editor-execute-summary.json`

Runtime checks (port `31040`, events port `31042`):
- Pairing + token flow succeeded (`POST /nova/runtime/pair`).
- `POST /nova/executePlan` with `spawn` + `delete` returned:
  - `status=ok`, `success_count=2`, `error_count=0`
- Artifacts:
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/plan-events-shared-refresh2/runtime-pair.json`
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/plan-events-shared-refresh2/runtime-execute-summary.json`

### macOS Shared Action-Event Builders Validation

- Date: 2026-02-25
- Run root:
  - `/tmp/novabridge-smoke-20260224-195531`
- Build command:
  - `"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" UnrealEditor Mac Development -Project="/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
- Result:
  - `Succeeded`
- Notes:
  - Added shared typed action event builders in `NovaBridgeCore` (`BuildSpawnEvent`, `BuildDeleteEvent`).
  - Editor and Runtime `executePlan` now route spawn/delete event payload creation through shared core helpers.

Editor checks (port `31110`, events port `31112`):
- `POST /nova/executePlan` with `spawn` + `delete` returned:
  - `status=ok`, `success_count=2`, `error_count=0`
- Artifacts:
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/action-events-shared-1/editor-health.json`
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/action-events-shared-1/editor-execute-summary.json`

Runtime checks (port `31120`, events port `31122`):
- Pairing + token flow succeeded (`POST /nova/runtime/pair`).
- `POST /nova/executePlan` with `spawn` + `delete` returned:
  - `status=ok`, `success_count=2`, `error_count=0`
- Artifacts:
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/action-events-shared-1/runtime-pair.json`
  - `/tmp/novabridge-smoke-20260224-195531/artifacts/action-events-shared-1/runtime-execute-summary.json`

### macOS Automated ExecutePlan Smoke Script Validation

- Date: 2026-02-25
- Command:
  - `./scripts/mac_executeplan_smoke.sh`
- Project:
  - `/Users/desmondpottle/Documents/New project/novabridge/NovaBridgeDefault/NovaBridgeDefault.uproject`
- Result:
  - `status=ok` from generated run summary.
- Script coverage:
  - sync source plugin into project `Plugins/NovaBridge`
  - build `UnrealEditor Mac Development`
  - editor `/nova/executePlan` (`spawn` + `delete`)
  - runtime pairing (`POST /nova/runtime/pair`) + token-gated `/nova/executePlan` (`spawn` + `delete`)
- Artifact root:
  - `/tmp/novabridge-smoke-20260224-215730/artifacts/executeplan-smoke`
- Key artifacts:
  - `/tmp/novabridge-smoke-20260224-215730/artifacts/executeplan-smoke/run-summary.json`
  - `/tmp/novabridge-smoke-20260224-215730/artifacts/executeplan-smoke/editor-execute-summary.json`
  - `/tmp/novabridge-smoke-20260224-215730/artifacts/executeplan-smoke/runtime-pair.json`
  - `/tmp/novabridge-smoke-20260224-215730/artifacts/executeplan-smoke/runtime-execute-summary.json`

### Claude-Issue Fix Validation (Core Utils + Python SDK Auth)

- Date: 2026-02-25
- C++ build command:
  - `"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" UnrealEditor Mac Development -Project="/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject" -WaitMutex -NoHotReloadFromIDE`
- Build result:
  - `Succeeded` (includes `NovaBridgeHttpUtils.cpp` in `NovaBridgeCore`)
- Smoke command:
  - `NOVABRIDGE_PROJECT=/tmp/novabridge-smoke-20260224-195531/NovaBridgeDefault/NovaBridgeDefault.uproject NOVABRIDGE_BUILD=1 ./scripts/mac_executeplan_smoke.sh`
- Smoke result:
  - `status=ok` from run summary.
- Smoke artifact root:
  - `/tmp/novabridge-smoke-20260224-220847/artifacts/executeplan-smoke`
- Key artifacts:
  - `/tmp/novabridge-smoke-20260224-220847/artifacts/executeplan-smoke/run-summary.json`
  - `/tmp/novabridge-smoke-20260224-220847/artifacts/executeplan-smoke/editor-execute-summary.json`
  - `/tmp/novabridge-smoke-20260224-220847/artifacts/executeplan-smoke/runtime-pair.json`
  - `/tmp/novabridge-smoke-20260224-220847/artifacts/executeplan-smoke/runtime-execute-summary.json`
- Python SDK unit tests:
  - Command: `python3 -m unittest discover -s python-sdk/tests -p 'test_*.py'`
  - Result: `Ran 2 tests ... OK`

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
