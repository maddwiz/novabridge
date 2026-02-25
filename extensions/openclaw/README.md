# OpenClaw Extensions Configuration

NovaBridge OpenClaw extensions are configured through environment variables.
Customer-facing setup should avoid machine-specific hardcoded paths.

## UE5 Bridge

- `NOVABRIDGE_HOST` (default `localhost`)
- `NOVABRIDGE_PORT` (default `30010`)
- `NOVABRIDGE_API_KEY` (optional)
- `NOVABRIDGE_SCREENSHOT_DIR` (default OS temp dir)
- `NOVABRIDGE_VOICE_HOST` (optional; defaults to `NOVABRIDGE_HOST`)

## Blender Bridge

- `NOVABRIDGE_BLENDER_PATH` (optional; if unset, `blender` is resolved from `PATH`)
- `NOVABRIDGE_EXPORT_DIR` (optional)
- `NOVABRIDGE_SCRIPTS_DIR` (optional)
- `NOVABRIDGE_OUTPUT_DIR` (optional)
- `NOVABRIDGE_HOST` (default `localhost`)
- `NOVABRIDGE_PORT` (default `30010`)
- `NOVABRIDGE_API_KEY` (optional)
- `NOVABRIDGE_IMPORT_SCALE` (default `100`)

## Notes

- Keep secrets (`NOVABRIDGE_API_KEY`) outside source control.
- Prefer per-user shell profiles or runtime launch scripts for environment injection.
