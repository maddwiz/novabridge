# Changelog

## Unreleased

- Added UE 5.7 compatibility for sequencer playback positioning and asset duplication.
- Avoided hard crash when `WebSocketNetworking` is unavailable by disabling the stream server gracefully.
- Added Windows packaging and packaged-smoke helper scripts.
- Documented known issue on UE < 5.7: sequencer scrub path recurses in `NovaBridgeSetPlaybackTime`; fix planned for `v0.9.1`.

## Planned v0.9.1

- Fix infinite recursion in sequencer scrub on UE versions before 5.7 (`NovaBridgeSetPlaybackTime` fallback branch).

## v0.9.0 - 2026-02-18

- Added `/nova/health` `version` field and `api_key_required` indicator.
- Added optional API key auth (`NOVABRIDGE_API_KEY` or `-NovaBridgeApiKey=`).
- Added support for `X-API-Key` and `Authorization: Bearer <key>`.
- Added Python SDK API key support.
- Added MCP server API key passthrough support.
- Removed hardcoded user paths from OpenClaw extensions.
- Updated packaging to include customer-facing docs only.
- Added customer launch docs (`INSTALL.md`, `BuyerGuide.md`, `SUPPORT.md`, `EULA.txt`).
