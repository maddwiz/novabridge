# Changelog

## Unreleased

- Upgraded control plane to `v0.9.5-dev` semantics.
- Added capability discovery endpoint: `GET /nova/caps`.
- Added structured plan execution endpoint: `POST /nova/executePlan`.
- Added undo endpoint: `POST /nova/undo` (spawn operations currently tracked).
- Added audit trail endpoint: `GET /nova/audit`.
- Added event channel endpoint: `GET /nova/events` (WebSocket discovery + JSON event stream).
- Added role-aware policy enforcement (`admin`, `automation`, `read_only`) with request-level route checks.
- Added role-aware spawn guardrails (allow-list classes, spawn bounds, per-plan limits).
- Added role/action per-minute rate limiting across routes.
- Added default role configuration (`NOVABRIDGE_DEFAULT_ROLE` or `-NovaBridgeDefaultRole=`).
- Extended `/nova/health` with `mode`, `default_role`, `stream_ws_port`, and `events_ws_port`.
- Added modular plugin structure groundwork with `NovaBridgeCore` and `NovaBridgeRuntime` modules.
- Added shared core capability registry used by Editor and Runtime `GET /nova/caps` responses.
- Extracted shared spawn/plan policy helpers into `NovaBridgeCore` and wired Editor + Runtime to reuse them.
- Added runtime server endpoints (experimental): `POST /nova/runtime/pair`, token-gated `/nova/health`, `/nova/caps`, `/nova/executePlan`.
- Added runtime token-gated audit endpoint: `GET /nova/audit`.
- Added runtime token-gated events discovery endpoint: `GET /nova/events` with runtime event WebSocket stream.
- Enforced localhost-only runtime request policy (`Host` must be loopback).
- Added runtime `executePlan` per-minute rate limiting and pairing-code rotation on successful pair.
- Fixed UE `< 5.7` sequencer scrub fallback to avoid recursion regressions (`NovaBridgeSetPlaybackTime` now uses explicit playback params on pre-5.7).
- Added UE 5.7 compatibility for sequencer playback positioning and asset duplication.
- Avoided hard crash when `WebSocketNetworking` is unavailable by disabling the stream server gracefully.
- Added Windows packaging and packaged-smoke helper scripts.
- Hardened release defaults/documentation for `v0.9.0` (version normalization, localhost-first security guidance, packaging hygiene excludes).

## v0.9.0 - 2026-02-18

- Added `/nova/health` `version` field and `api_key_required` indicator.
- Added optional API key auth (`NOVABRIDGE_API_KEY` or `-NovaBridgeApiKey=`).
- Added support for `X-API-Key` and `Authorization: Bearer <key>`.
- Added Python SDK API key support.
- Added MCP server API key passthrough support.
- Removed hardcoded user paths from OpenClaw extensions.
- Updated packaging to include customer-facing docs only.
- Added customer launch docs (`INSTALL.md`, `BuyerGuide.md`, `SUPPORT.md`, `EULA.txt`).
